#include "PCH.h"

#pragma push_macro("MEM_RELEASE")
#pragma push_macro("MAX_PATH")
#pragma push_macro("near")
#pragma push_macro("far")
#undef MEM_RELEASE
#undef MAX_PATH
#undef near
#undef far
#include <RE/Bethesda/BSTArray.h>
#include <RE/Bethesda/BSLock.h>
#include <RE/Bethesda/BSGraphics.h>
#include <RE/Bethesda/BSInputEventUser.h>
#include <RE/Bethesda/PlayerCharacter.h>
#include <RE/Bethesda/TESCamera.h>
#include <RE/Bethesda/TESForms.h>
#include <RE/NetImmerse/NiNode.h>
#pragma pop_macro("far")
#pragma pop_macro("near")
#pragma pop_macro("MAX_PATH")
#pragma pop_macro("MEM_RELEASE")

#include "Features/skylighting/SkylightingRuntime.h"

#include "Features/skylighting/SkylightingNativeHooks.h"
#include "UpdateProbesCS.h"
#include "render/ComputeStateScope.h"
#include "render/DebugResourceName.h"
#include "support/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace csah::skylighting
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::uintptr_t kCubeSizeRva = 0x05A3CFA4;
        constexpr std::uintptr_t kDirectionXRva = 0x05A3CFC8;
        constexpr std::uintptr_t kDirectionYRva = 0x05A3CFCC;
        constexpr std::uintptr_t kDirectionZRva = 0x05A3CFD0;
        constexpr std::ptrdiff_t kPrecipitationCameraOffset = 0x50;
        constexpr std::ptrdiff_t kPrecipitationLastCubeSizeOffset = 0x90;
        constexpr std::size_t kNativeProjectionOffset = 0xED0;
        constexpr std::size_t kNativePrecipitationDepthTarget = 9;
        constexpr float kPrivateDepthClearValue = 1.0f;
        constexpr std::uintptr_t kRendererStateRva = 0x038AC010;
        constexpr std::size_t kDepthTargetMapOffset = 0x15FC;
        constexpr std::size_t kFo4VrDepthStencilTargetsOffset = 0x2588;
        constexpr std::size_t kFo4VrDepthStencilTargetCount = 18;
        constexpr float kCaptureDistance = 10000.0f;
        constexpr float kCaptureHeight = 5000.0f;
        constexpr float kFarCaptureDistance = 32768.0f;
        constexpr float kFarCaptureHeight = 16384.0f;
        constexpr std::uint64_t kFarCaptureInterval = 8;
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kDegreesToRadians =
            0.01745329251994329577f;
        constexpr float kR2X = 0.245122333753f;
        constexpr float kR2Y = 0.430159709002f;
        constexpr std::size_t kDiagnosticStatCount = 10;
        constexpr std::size_t kDiagnosticByteWidth =
            kDiagnosticStatCount * sizeof(std::uint32_t);
        constexpr std::size_t kAmbientDiagnosticStatCount = 34;
        constexpr std::size_t kAmbientDiagnosticByteWidth =
            kAmbientDiagnosticStatCount * sizeof(std::uint32_t);
        constexpr UINT kAmbientDiagnosticUavSlot = 7;

        // FO4VR has 145 render targets before this array. CommonLibF4VR
        // declares 101, so its RendererData::depthStencilTargets field is
        // 0x840 bytes early. Keep the corrected layout local to this port.
        struct Fo4VrDepthStencilTarget
        {
            ID3D11Texture2D* texture{};
            ID3D11DepthStencilView* depthViews[4]{};
            ID3D11DepthStencilView* readOnlyDepthViews[4]{};
            ID3D11DepthStencilView* readOnlyStencilViews[4]{};
            ID3D11DepthStencilView* readOnlyDepthStencilViews[4]{};
            ID3D11ShaderResourceView* depthResource{};
            ID3D11ShaderResourceView* stencilResource{};
        };
        static_assert(sizeof(Fo4VrDepthStencilTarget) == 0x98);
        static_assert(offsetof(Fo4VrDepthStencilTarget, texture) == 0x00);
        static_assert(offsetof(Fo4VrDepthStencilTarget, depthViews) == 0x08);
        static_assert(
            offsetof(Fo4VrDepthStencilTarget, depthResource) == 0x88);

        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            if (begin > (std::numeric_limits<std::uintptr_t>::max)() - size) {
                return false;
            }
            const auto end = begin + size;
            auto cursor = begin;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto region = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (region >
                    (std::numeric_limits<std::uintptr_t>::max)() -
                        information.RegionSize) {
                    return false;
                }
                const auto regionEnd = region + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(regionEnd, end);
            }
            return true;
        }

        class ScopedNativeDepthTarget final
        {
        public:
            ScopedNativeDepthTarget(
                Fo4VrDepthStencilTarget& target,
                ID3D11Texture2D* texture,
                ID3D11DepthStencilView* depthView,
                ID3D11ShaderResourceView* depthResource) noexcept :
                target_(&target),
                originalTexture_(target.texture),
                originalDepthView_(target.depthViews[0]),
                originalDepthResource_(target.depthResource)
            {
                if (!texture || !depthView || !depthResource) {
                    target_ = nullptr;
                    return;
                }
                target.texture = texture;
                target.depthViews[0] = depthView;
                target.depthResource = depthResource;
            }

            ~ScopedNativeDepthTarget() noexcept
            {
                if (!target_) {
                    return;
                }
                target_->texture = originalTexture_;
                target_->depthViews[0] = originalDepthView_;
                target_->depthResource = originalDepthResource_;
            }

            ScopedNativeDepthTarget(const ScopedNativeDepthTarget&) = delete;
            ScopedNativeDepthTarget& operator=(
                const ScopedNativeDepthTarget&) = delete;

            [[nodiscard]] bool active() const noexcept
            {
                return target_ != nullptr;
            }

        private:
            Fo4VrDepthStencilTarget* target_{};
            ID3D11Texture2D* originalTexture_{};
            ID3D11DepthStencilView* originalDepthView_{};
            ID3D11ShaderResourceView* originalDepthResource_{};
        };

        [[nodiscard]] std::uint32_t positiveModulo(
            std::int64_t value,
            std::uint32_t modulus) noexcept
        {
            if (modulus == 0) {
                return 0;
            }
            const auto signedModulus = static_cast<std::int64_t>(modulus);
            auto result = value % signedModulus;
            if (result < 0) {
                result += signedModulus;
            }
            return static_cast<std::uint32_t>(result);
        }

        [[nodiscard]] DXGI_FORMAT typelessDepthFormat(
            DXGI_FORMAT textureFormat,
            DXGI_FORMAT viewFormat) noexcept
        {
            switch (textureFormat) {
            case DXGI_FORMAT_R32_TYPELESS:
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_R16_TYPELESS:
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                return textureFormat;
            default:
                break;
            }
            switch (viewFormat) {
            case DXGI_FORMAT_D32_FLOAT:
                return DXGI_FORMAT_R32_TYPELESS;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                return DXGI_FORMAT_R24G8_TYPELESS;
            case DXGI_FORMAT_D16_UNORM:
                return DXGI_FORMAT_R16_TYPELESS;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                return DXGI_FORMAT_R32G8X24_TYPELESS;
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        [[nodiscard]] DXGI_FORMAT depthResourceFormat(
            DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R32_TYPELESS:
            case DXGI_FORMAT_D32_FLOAT:
                return DXGI_FORMAT_R32_FLOAT;
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            case DXGI_FORMAT_R16_TYPELESS:
            case DXGI_FORMAT_D16_UNORM:
                return DXGI_FORMAT_R16_UNORM;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        [[nodiscard]] bool sameDescription(
            const D3D11_TEXTURE2D_DESC& left,
            const D3D11_TEXTURE2D_DESC& right) noexcept
        {
            return left.Width == right.Width &&
                left.Height == right.Height &&
                left.MipLevels == right.MipLevels &&
                left.ArraySize == right.ArraySize &&
                left.Format == right.Format &&
                left.SampleDesc.Count == right.SampleDesc.Count &&
                left.SampleDesc.Quality == right.SampleDesc.Quality &&
                left.Usage == right.Usage &&
                left.BindFlags == right.BindFlags &&
                left.CPUAccessFlags == right.CPUAccessFlags &&
                left.MiscFlags == right.MiscFlags;
        }

        [[nodiscard]] bool sameDescription(
            const D3D11_DEPTH_STENCIL_VIEW_DESC& left,
            const D3D11_DEPTH_STENCIL_VIEW_DESC& right) noexcept
        {
            return std::memcmp(&left, &right, sizeof(left)) == 0;
        }

        template <class Value>
        [[nodiscard]] Value clampDifference(
            std::int64_t value,
            std::uint32_t dimension) noexcept
        {
            const auto limit = static_cast<std::int64_t>(dimension);
            return static_cast<Value>(std::clamp(value, -limit, limit));
        }
    }

    ScopedAmbientBindings::ScopedAmbientBindings(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* nearProbe,
        ID3D11ShaderResourceView* farProbe,
        ID3D11Buffer* constants,
        ID3D11UnorderedAccessView* diagnostic,
        render::GpuTimingProfiler* drawTiming,
        Runtime* owner) noexcept :
        context_(context),
        drawTiming_(drawTiming ? drawTiming->begin() :
                                 render::GpuTimingProfiler::Scope{}),
        owner_(owner)
    {
        if (!context_) {
            return;
        }
        context_->PSGetShaderResources(
            50,
            static_cast<UINT>(previousProbes_.size()),
            previousProbes_.data());
        context_->PSGetConstantBuffers(13, 1, &previousConstants_);
        const std::array<ID3D11ShaderResourceView*, 2> probes{
            nearProbe,
            farProbe,
        };
        context_->PSSetShaderResources(
            50,
            static_cast<UINT>(probes.size()),
            probes.data());
        context_->PSSetConstantBuffers(13, 1, &constants);
        if (diagnostic && owner_) {
            context_->OMGetRenderTargetsAndUnorderedAccessViews(
                0,
                nullptr,
                nullptr,
                kAmbientDiagnosticUavSlot,
                1,
                &previousDiagnostic_);
            context_->OMSetRenderTargetsAndUnorderedAccessViews(
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
                nullptr,
                nullptr,
                kAmbientDiagnosticUavSlot,
                1,
                &diagnostic,
                nullptr);
            diagnosticCaptured_ = true;
        }
        captured_ = true;
    }

    ScopedAmbientBindings::~ScopedAmbientBindings()
    {
        (void)restore();
    }

    ScopedAmbientBindings::ScopedAmbientBindings(
        ScopedAmbientBindings&& other) noexcept :
        context_(other.context_),
        previousProbes_(other.previousProbes_),
        previousConstants_(other.previousConstants_),
        previousDiagnostic_(other.previousDiagnostic_),
        drawTiming_(std::move(other.drawTiming_)),
        owner_(other.owner_),
        diagnosticCaptured_(other.diagnosticCaptured_),
        captured_(other.captured_)
    {
        other.context_ = nullptr;
        other.previousProbes_.fill(nullptr);
        other.previousConstants_ = nullptr;
        other.previousDiagnostic_ = nullptr;
        other.owner_ = nullptr;
        other.diagnosticCaptured_ = false;
        other.captured_ = false;
    }

    ScopedAmbientBindings& ScopedAmbientBindings::operator=(
        ScopedAmbientBindings&& other) noexcept
    {
        if (this != &other) {
            (void)restore();
            context_ = other.context_;
            previousProbes_ = other.previousProbes_;
            previousConstants_ = other.previousConstants_;
            previousDiagnostic_ = other.previousDiagnostic_;
            drawTiming_ = std::move(other.drawTiming_);
            owner_ = other.owner_;
            diagnosticCaptured_ = other.diagnosticCaptured_;
            captured_ = other.captured_;
            other.context_ = nullptr;
            other.previousProbes_.fill(nullptr);
            other.previousConstants_ = nullptr;
            other.previousDiagnostic_ = nullptr;
            other.owner_ = nullptr;
            other.diagnosticCaptured_ = false;
            other.captured_ = false;
        }
        return *this;
    }

    bool ScopedAmbientBindings::restore() noexcept
    {
        if (!captured_ || !context_) {
            return false;
        }
        if (diagnosticCaptured_) {
            auto* previous = previousDiagnostic_;
            context_->OMSetRenderTargetsAndUnorderedAccessViews(
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
                nullptr,
                nullptr,
                kAmbientDiagnosticUavSlot,
                1,
                &previous,
                nullptr);
            if (previousDiagnostic_) {
                previousDiagnostic_->Release();
                previousDiagnostic_ = nullptr;
            }
            diagnosticCaptured_ = false;
            if (owner_) {
                owner_->submitAmbientDiagnostic();
            }
        }
        context_->PSSetShaderResources(
            50,
            static_cast<UINT>(previousProbes_.size()),
            previousProbes_.data());
        context_->PSSetConstantBuffers(13, 1, &previousConstants_);
        for (auto*& previousProbe : previousProbes_) {
            if (previousProbe) {
                previousProbe->Release();
                previousProbe = nullptr;
            }
        }
        if (previousConstants_) {
            previousConstants_->Release();
            previousConstants_ = nullptr;
        }
        captured_ = false;
        context_ = nullptr;
        owner_ = nullptr;
        return true;
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    Runtime::Dimensions Runtime::dimensionsFor(Quality quality) noexcept
    {
        switch (quality) {
        case Quality::low:
            return { 64, 64, 32 };
        case Quality::medium:
            return { 128, 128, 64 };
        case Quality::high:
            return { 256, 256, 128 };
        }
        return { 256, 256, 128 };
    }

    Runtime::Dimensions Runtime::farDimensionsFor() noexcept
    {
        return { 32, 32, 60 };
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        requestedQuality_.store(
            static_cast<std::uint32_t>(safe.quality),
            std::memory_order_relaxed);
        minimumDiffuseBits_.store(
            std::bit_cast<std::uint32_t>(safe.minimumDiffuseVisibility),
            std::memory_order_relaxed);
        minimumSpecularBits_.store(
            std::bit_cast<std::uint32_t>(safe.minimumSpecularVisibility),
            std::memory_order_relaxed);
        maximumZenithBits_.store(
            std::bit_cast<std::uint32_t>(safe.maximumZenithDegrees),
            std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);

        if (gpuResourcesReady_.load(std::memory_order_acquire) &&
            safe.quality != activeQuality_ &&
            !qualityRestartWarningLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Skylighting quality changed from {} to {}; the requested probe resolution will apply after restart while the active resources remain unchanged.",
                qualityName(activeQuality_),
                qualityName(safe.quality));
        }
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* immediateContext) noexcept
    {
        gpuResourcesReady_.store(false, std::memory_order_release);
        nextResourceAttemptCall_ = 0;
        nearProbes_ = {};
        farProbes_ = {};
        privateDepthTexture_.Reset();
        privateDepthView_.Reset();
        privateDepthResource_.Reset();
        privateDepthDescription_ = {};
        privateDepthViewDescription_ = {};
        diagnosticBuffer_.Reset();
        diagnosticOutput_.Reset();
        diagnosticStaging_.Reset();
        diagnosticCompletion_.Reset();
        ambientDiagnosticBuffer_.Reset();
        ambientDiagnosticOutput_.Reset();
        ambientDiagnosticStaging_.Reset();
        ambientDiagnosticCompletion_.Reset();
        updateShader_.Reset();
        comparisonSampler_.Reset();
        constantsBuffer_.Reset();
        constants_ = {};
        probeDataValid_.store(false, std::memory_order_release);
        privateDepthReady_.store(false, std::memory_order_release);
        exteriorActive_.store(false, std::memory_order_release);

        gpuTiming_.reset();
        ambientDrawGpuTiming_.reset();
        device_ = device;
        context_ = immediateContext;
        if (!device || !immediateContext) {
            logging::error(
                "Skylighting rejected a missing D3D11 device or immediate context.");
            return;
        }
        activeQuality_ = sanitizeQuality(
            requestedQuality_.load(std::memory_order_relaxed));
        nearProbes_.dimensions = dimensionsFor(activeQuality_);
        farProbes_.dimensions = farDimensionsFor();
        qualityRestartWarningLogged_.store(false, std::memory_order_relaxed);
        firstPrerequisiteRejectionLogged_.store(
            false,
            std::memory_order_relaxed);
        firstWorldStateRejectionLogged_.store(
            false,
            std::memory_order_relaxed);
        firstRenderAttemptLogged_.store(false, std::memory_order_relaxed);
        firstPrivateDepthFailureLogged_.store(
            false,
            std::memory_order_relaxed);
        firstActiveAmbientBindLogged_.store(false, std::memory_order_relaxed);
        firstPassProducerSummaryLogged_.store(
            false,
            std::memory_order_relaxed);
        firstFarProbeUpdateLogged_.store(false, std::memory_order_relaxed);
        diagnosticSubmitted_ = false;
        diagnosticPending_ = false;
        diagnosticLogged_ = false;
        ambientDiagnosticSubmitted_ = false;
        ambientDiagnosticPending_ = false;
        ambientDiagnosticLogged_ = false;
        privateRenderActive_.store(false, std::memory_order_relaxed);
        privateCaptureDrawStateLogged_.store(
            false,
            std::memory_order_relaxed);

        if (requested()) (void)ensureGpuResources();
    }

    bool Runtime::ensureGpuResources() noexcept
    {
        if (gpuResourcesReady_.load(std::memory_order_acquire)) return true;
        if (!device_ || !context_) return false;
        const auto call = captureCalls_.load(std::memory_order_relaxed);
        if (call < nextResourceAttemptCall_) return false;
        // Retry transient allocation failures at a bounded render cadence.
        nextResourceAttemptCall_ = call + 60;
        if (!createProbeResources()) {
            nearProbes_ = {};
            farProbes_ = {};
            logging::error(
                "Skylighting GPU resource creation failed; retrying after 60 native callbacks.");
            return false;
        }
        render::nameResource(nearProbes_.probeTexture.Get(), "CSAH / Skylighting / Near visibility");
        render::nameResource(farProbes_.probeTexture.Get(), "CSAH / Skylighting / Far visibility");
        render::nameResource(nearProbes_.accumulationTexture.Get(), "CSAH / Skylighting / Near accumulation");
        render::nameResource(farProbes_.accumulationTexture.Get(), "CSAH / Skylighting / Far accumulation");
        clearProbeResources();
        if (!gpuTiming_.initialize(
                device_.Get(),
                context_.Get(),
                "Skylighting",
                { "native capture", "probe update", nullptr, nullptr },
                2,
                120,
                1,
                render::GpuTimingProfiler::Group::Skylighting)) {
            logging::warn(
                "Skylighting could not allocate image-neutral GPU timing queries; rendering remains active without performance telemetry.");
        }
        if (!ambientDrawGpuTiming_.initialize(
                device_.Get(),
                context_.Get(),
                "Skylighting ambient draw",
                { "DFLight ambient replacement", nullptr, nullptr,
                    nullptr },
                1,
                120,
                30,
                render::GpuTimingProfiler::Group::Skylighting)) {
            logging::warn(
                "Skylighting could not allocate ambient-draw GPU timing queries; rendering remains active without performance telemetry.");
        }
        gpuResourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Skylighting GPU resources are ready at {} quality (near={}x{}x{} over {:.0f} units, far={}x{}x{} over {:.0f} units, shared stereo world-space clipmap).",
            qualityName(activeQuality_),
            nearProbes_.dimensions.width,
            nearProbes_.dimensions.height,
            nearProbes_.dimensions.depth,
            kCaptureDistance,
            farProbes_.dimensions.width,
            farProbes_.dimensions.height,
            farProbes_.dimensions.depth,
            kFarCaptureDistance);
        return true;
    }

    bool Runtime::createProbeLevelResources(
        ProbeResources& level,
        Dimensions dimensions) noexcept
    {
        if (!device_) {
            return false;
        }

        D3D11_TEXTURE3D_DESC probeDescription{};
        probeDescription.Width = dimensions.width;
        probeDescription.Height = dimensions.height;
        probeDescription.Depth = dimensions.depth;
        probeDescription.MipLevels = 1;
        probeDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        probeDescription.Usage = D3D11_USAGE_DEFAULT;
        probeDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        ComPtr<ID3D11Texture3D> probeTexture;
        if (FAILED(device_->CreateTexture3D(
                &probeDescription,
                nullptr,
                probeTexture.GetAddressOf()))) {
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC probeResourceDescription{};
        probeResourceDescription.Format = probeDescription.Format;
        probeResourceDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE3D;
        probeResourceDescription.Texture3D.MostDetailedMip = 0;
        probeResourceDescription.Texture3D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> probeResource;
        if (FAILED(device_->CreateShaderResourceView(
                probeTexture.Get(),
                &probeResourceDescription,
                probeResource.GetAddressOf()))) {
            return false;
        }
        D3D11_UNORDERED_ACCESS_VIEW_DESC probeOutputDescription{};
        probeOutputDescription.Format = probeDescription.Format;
        probeOutputDescription.ViewDimension =
            D3D11_UAV_DIMENSION_TEXTURE3D;
        probeOutputDescription.Texture3D.MipSlice = 0;
        probeOutputDescription.Texture3D.FirstWSlice = 0;
        probeOutputDescription.Texture3D.WSize = dimensions.depth;
        ComPtr<ID3D11UnorderedAccessView> probeOutput;
        if (FAILED(device_->CreateUnorderedAccessView(
                probeTexture.Get(),
                &probeOutputDescription,
                probeOutput.GetAddressOf()))) {
            return false;
        }

        auto accumulationDescription = probeDescription;
        accumulationDescription.Format = DXGI_FORMAT_R8_UINT;
        ComPtr<ID3D11Texture3D> accumulationTexture;
        if (FAILED(device_->CreateTexture3D(
                &accumulationDescription,
                nullptr,
                accumulationTexture.GetAddressOf()))) {
            return false;
        }
        auto accumulationResourceDescription = probeResourceDescription;
        accumulationResourceDescription.Format =
            accumulationDescription.Format;
        ComPtr<ID3D11ShaderResourceView> accumulationResource;
        if (FAILED(device_->CreateShaderResourceView(
                accumulationTexture.Get(),
                &accumulationResourceDescription,
                accumulationResource.GetAddressOf()))) {
            return false;
        }
        auto accumulationOutputDescription = probeOutputDescription;
        accumulationOutputDescription.Format =
            accumulationDescription.Format;
        ComPtr<ID3D11UnorderedAccessView> accumulationOutput;
        if (FAILED(device_->CreateUnorderedAccessView(
                accumulationTexture.Get(),
                &accumulationOutputDescription,
                accumulationOutput.GetAddressOf()))) {
            return false;
        }

        level.dimensions = dimensions;
        level.probeTexture = std::move(probeTexture);
        level.probeResource = std::move(probeResource);
        level.probeOutput = std::move(probeOutput);
        level.accumulationTexture = std::move(accumulationTexture);
        level.accumulationResource = std::move(accumulationResource);
        level.accumulationOutput = std::move(accumulationOutput);
        level.previousCell = {};
        level.previousCellValid = false;
        level.dataValid = false;
        level.updateSliceCursor = 0;
        level.captureQuadrant = 0;
        return true;
    }

    bool Runtime::createDiagnosticResources() noexcept
    {
        D3D11_BUFFER_DESC diagnosticDescription{};
        diagnosticDescription.ByteWidth =
            static_cast<UINT>(kDiagnosticByteWidth);
        diagnosticDescription.Usage = D3D11_USAGE_DEFAULT;
        diagnosticDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        diagnosticDescription.MiscFlags =
            D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        ComPtr<ID3D11Buffer> diagnosticBuffer;
        if (FAILED(device_->CreateBuffer(
                &diagnosticDescription,
                nullptr,
                diagnosticBuffer.GetAddressOf()))) {
            return false;
        }
        D3D11_UNORDERED_ACCESS_VIEW_DESC diagnosticOutputDescription{};
        diagnosticOutputDescription.Format = DXGI_FORMAT_R32_TYPELESS;
        diagnosticOutputDescription.ViewDimension =
            D3D11_UAV_DIMENSION_BUFFER;
        diagnosticOutputDescription.Buffer.NumElements =
            static_cast<UINT>(kDiagnosticStatCount);
        diagnosticOutputDescription.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        ComPtr<ID3D11UnorderedAccessView> diagnosticOutput;
        if (FAILED(device_->CreateUnorderedAccessView(
                diagnosticBuffer.Get(),
                &diagnosticOutputDescription,
                diagnosticOutput.GetAddressOf()))) {
            return false;
        }
        auto stagingDescription = diagnosticDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;
        ComPtr<ID3D11Buffer> diagnosticStaging;
        if (FAILED(device_->CreateBuffer(
                &stagingDescription,
                nullptr,
                diagnosticStaging.GetAddressOf()))) {
            return false;
        }
        D3D11_QUERY_DESC queryDescription{ D3D11_QUERY_EVENT, 0 };
        ComPtr<ID3D11Query> diagnosticCompletion;
        if (FAILED(device_->CreateQuery(
                &queryDescription,
                diagnosticCompletion.GetAddressOf()))) {
            return false;
        }

        auto ambientDiagnosticDescription = diagnosticDescription;
        ambientDiagnosticDescription.ByteWidth =
            static_cast<UINT>(kAmbientDiagnosticByteWidth);
        ComPtr<ID3D11Buffer> ambientDiagnosticBuffer;
        if (FAILED(device_->CreateBuffer(
                &ambientDiagnosticDescription,
                nullptr,
                ambientDiagnosticBuffer.GetAddressOf()))) {
            return false;
        }
        auto ambientDiagnosticOutputDescription = diagnosticOutputDescription;
        ambientDiagnosticOutputDescription.Buffer.NumElements =
            static_cast<UINT>(kAmbientDiagnosticStatCount);
        ComPtr<ID3D11UnorderedAccessView> ambientDiagnosticOutput;
        if (FAILED(device_->CreateUnorderedAccessView(
                ambientDiagnosticBuffer.Get(),
                &ambientDiagnosticOutputDescription,
                ambientDiagnosticOutput.GetAddressOf()))) {
            return false;
        }
        auto ambientDiagnosticStagingDescription =
            ambientDiagnosticDescription;
        ambientDiagnosticStagingDescription.Usage = D3D11_USAGE_STAGING;
        ambientDiagnosticStagingDescription.BindFlags = 0;
        ambientDiagnosticStagingDescription.CPUAccessFlags =
            D3D11_CPU_ACCESS_READ;
        ambientDiagnosticStagingDescription.MiscFlags = 0;
        ComPtr<ID3D11Buffer> ambientDiagnosticStaging;
        if (FAILED(device_->CreateBuffer(
                &ambientDiagnosticStagingDescription,
                nullptr,
                ambientDiagnosticStaging.GetAddressOf()))) {
            return false;
        }
        ComPtr<ID3D11Query> ambientDiagnosticCompletion;
        if (FAILED(device_->CreateQuery(
                &queryDescription,
                ambientDiagnosticCompletion.GetAddressOf()))) {
            return false;
        }

        diagnosticBuffer_ = std::move(diagnosticBuffer);
        diagnosticOutput_ = std::move(diagnosticOutput);
        diagnosticStaging_ = std::move(diagnosticStaging);
        diagnosticCompletion_ = std::move(diagnosticCompletion);
        ambientDiagnosticBuffer_ = std::move(ambientDiagnosticBuffer);
        ambientDiagnosticOutput_ = std::move(ambientDiagnosticOutput);
        ambientDiagnosticStaging_ = std::move(ambientDiagnosticStaging);
        ambientDiagnosticCompletion_ =
            std::move(ambientDiagnosticCompletion);
        return true;
    }

    bool Runtime::createProbeResources() noexcept
    {
        if (!device_ || !context_ ||
            !createProbeLevelResources(
                nearProbes_, dimensionsFor(activeQuality_)) ||
            !createProbeLevelResources(
                farProbes_, farDimensionsFor())) {
            return false;
        }

        ComPtr<ID3D11ComputeShader> updateShader;
        if (FAILED(device_->CreateComputeShader(
                csah_skylighting_update_probes,
                sizeof(csah_skylighting_update_probes),
                nullptr,
                updateShader.GetAddressOf()))) {
            return false;
        }
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter =
            D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc =
            D3D11_COMPARISON_GREATER_EQUAL;
        samplerDescription.MinLOD = 0.0f;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> comparisonSampler;
        if (FAILED(device_->CreateSamplerState(
                &samplerDescription,
                comparisonSampler.GetAddressOf()))) {
            return false;
        }
        D3D11_BUFFER_DESC bufferDescription{};
        bufferDescription.ByteWidth = sizeof(Constants);
        bufferDescription.Usage = D3D11_USAGE_DEFAULT;
        bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constantsBuffer;
        if (FAILED(device_->CreateBuffer(
                &bufferDescription,
                nullptr,
                constantsBuffer.GetAddressOf()))) {
            return false;
        }

        if (!createDiagnosticResources()) {
            logging::warn("Skylighting diagnostic resources unavailable; probe rendering remains active.");
        }
        updateShader_ = std::move(updateShader);
        comparisonSampler_ = std::move(comparisonSampler);
        constantsBuffer_ = std::move(constantsBuffer);
        return true;
    }

    void Runtime::clearProbeResources() noexcept
    {
        if (!context_ || !nearProbes_.probeOutput ||
            !nearProbes_.accumulationOutput || !farProbes_.probeOutput ||
            !farProbes_.accumulationOutput) {
            return;
        }
        constexpr std::array<float, 4> unitVisibility{
            3.54490770181103205460f,
            0.0f,
            0.0f,
            0.0f,
        };
        constexpr std::array<UINT, 4> noAccumulation{};
        const auto clearLevel = [&](ProbeResources& level) noexcept {
            context_->ClearUnorderedAccessViewFloat(
                level.probeOutput.Get(),
                unitVisibility.data());
            context_->ClearUnorderedAccessViewUint(
                level.accumulationOutput.Get(),
                noAccumulation.data());
            level.previousCell = {};
            level.previousCellValid = false;
            level.dataValid = false;
            level.updateSliceCursor = 0;
            level.captureQuadrant = 0;
        };
        clearLevel(nearProbes_);
        clearLevel(farProbes_);
        probeDataValid_.store(false, std::memory_order_release);
        resetRequested_.store(false, std::memory_order_release);
    }

    void Runtime::setNativeHookOwned(bool owned) noexcept
    {
        nativeHookOwned_.store(owned, std::memory_order_release);
        if (!owned) {
            exteriorActive_.store(false, std::memory_order_release);
            probeDataValid_.store(false, std::memory_order_release);
        }
    }

    void Runtime::beginWorldSession() noexcept
    {
        exteriorActive_.store(false, std::memory_order_release);
        probeDataValid_.store(false, std::memory_order_release);
        resetRequested_.store(true, std::memory_order_release);
        ambientDiagnosticSubmitted_ = false;
        ambientDiagnosticPending_ = false;
        ambientDiagnosticLogged_ = false;
    }

    bool Runtime::ensurePrivateDepth(
        ID3D11DepthStencilView* source) noexcept
    {
        const auto logFailure = [this](const char* reason) noexcept {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting private-depth creation failed at '{}'.",
                    reason);
            }
        };
        if (!source || !device_ || !context_) {
            logFailure("missing source DSV, device, or immediate context");
            return false;
        }
        ComPtr<ID3D11Resource> sourceResource;
        source->GetResource(sourceResource.GetAddressOf());
        ComPtr<ID3D11Texture2D> sourceTexture;
        if (!sourceResource || FAILED(sourceResource.As(&sourceTexture))) {
            logFailure("source DSV resource is not a Texture2D");
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDescription{};
        D3D11_DEPTH_STENCIL_VIEW_DESC sourceViewDescription{};
        sourceTexture->GetDesc(&sourceDescription);
        source->GetDesc(&sourceViewDescription);
        if (sourceDescription.SampleDesc.Count != 1 ||
            sourceDescription.ArraySize != 1 ||
            sourceViewDescription.ViewDimension !=
                D3D11_DSV_DIMENSION_TEXTURE2D) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting rejected the native precipitation DSV description: extent={}x{}, textureFormat={}, samples={}, array={}, viewFormat={}, viewDimension={}.",
                    sourceDescription.Width,
                    sourceDescription.Height,
                    static_cast<std::uint32_t>(sourceDescription.Format),
                    sourceDescription.SampleDesc.Count,
                    sourceDescription.ArraySize,
                    static_cast<std::uint32_t>(
                        sourceViewDescription.Format),
                    static_cast<std::uint32_t>(
                        sourceViewDescription.ViewDimension));
            }
            return false;
        }
        if (privateDepthView_ &&
            sameDescription(
                privateDepthDescription_, sourceDescription) &&
            sameDescription(
                privateDepthViewDescription_, sourceViewDescription)) {
            return true;
        }

        auto privateDescription = sourceDescription;
        privateDescription.Format = typelessDepthFormat(
            sourceDescription.Format,
            sourceViewDescription.Format);
        if (privateDescription.Format == DXGI_FORMAT_UNKNOWN) {
            logFailure("unsupported native precipitation depth format");
            return false;
        }
        privateDescription.Usage = D3D11_USAGE_DEFAULT;
        privateDescription.BindFlags =
            D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        privateDescription.CPUAccessFlags = 0;
        privateDescription.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> texture;
        const auto textureResult = device_->CreateTexture2D(
            &privateDescription,
            nullptr,
            texture.GetAddressOf());
        if (FAILED(textureResult)) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting private depth texture creation failed with HRESULT 0x{:08X}.",
                    static_cast<std::uint32_t>(textureResult));
            }
            return false;
        }
        ComPtr<ID3D11DepthStencilView> view;
        const auto viewResult = device_->CreateDepthStencilView(
            texture.Get(),
            &sourceViewDescription,
            view.GetAddressOf());
        if (FAILED(viewResult)) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting private depth DSV creation failed with HRESULT 0x{:08X}.",
                    static_cast<std::uint32_t>(viewResult));
            }
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceDescription{};
        resourceDescription.Format = depthResourceFormat(
            privateDescription.Format);
        resourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceDescription.Texture2D.MostDetailedMip = 0;
        resourceDescription.Texture2D.MipLevels = 1;
        if (resourceDescription.Format == DXGI_FORMAT_UNKNOWN) {
            logFailure("unsupported private depth SRV format");
            return false;
        }
        ComPtr<ID3D11ShaderResourceView> resource;
        const auto resourceResult = device_->CreateShaderResourceView(
            texture.Get(),
            &resourceDescription,
            resource.GetAddressOf());
        if (FAILED(resourceResult)) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting private depth SRV creation failed with HRESULT 0x{:08X}.",
                    static_cast<std::uint32_t>(resourceResult));
            }
            return false;
        }

        privateDepthTexture_ = std::move(texture);
        privateDepthView_ = std::move(view);
        privateDepthResource_ = std::move(resource);
        privateDepthDescription_ = sourceDescription;
        privateDepthViewDescription_ = sourceViewDescription;
        privateDepthReady_.store(true, std::memory_order_release);
        logging::info(
            "Skylighting captured a private precipitation depth contract ({}x{}, format={}, DSV format={}).",
            sourceDescription.Width,
            sourceDescription.Height,
            static_cast<std::uint32_t>(privateDescription.Format),
            static_cast<std::uint32_t>(sourceViewDescription.Format));
        return true;
    }

    bool Runtime::updateRollingVolume(
        ProbeResources& level,
        ProbeLevelConstants& levelConstants,
        float captureDistance,
        float captureHeight,
        float x,
        float y,
        float z) noexcept
    {
        const std::array<float, 3> position{ x, y, z };
        const std::array<float, 3> cellSize{
            captureDistance /
                static_cast<float>(level.dimensions.width),
            captureDistance /
                static_cast<float>(level.dimensions.height),
            captureHeight /
                static_cast<float>(level.dimensions.depth),
        };
        std::array<std::int64_t, 3> cell{};
        std::array<float, 3> offset{};
        for (std::size_t axis = 0; axis < cell.size(); ++axis) {
            cell[axis] = static_cast<std::int64_t>(
                std::llround(position[axis] / cellSize[axis]));
            offset[axis] =
                static_cast<float>(cell[axis]) * cellSize[axis] -
                position[axis];
        }

        levelConstants.arraySize = {
            captureDistance,
            captureDistance,
            captureHeight,
            0.0f,
        };
        levelConstants.cellSize = {
            cellSize[0],
            cellSize[1],
            cellSize[2],
            0.0f,
        };
        levelConstants.positionOffset = {
            offset[0],
            offset[1],
            offset[2],
            0.0f,
        };
        levelConstants.arrayDimensions = {
            level.dimensions.width,
            level.dimensions.height,
            level.dimensions.depth,
            0,
        };
        levelConstants.arrayOrigin = {
            positiveModulo(
                cell[0] -
                    static_cast<std::int64_t>(
                        level.dimensions.width / 2),
                level.dimensions.width),
            positiveModulo(
                cell[1] -
                    static_cast<std::int64_t>(
                        level.dimensions.height / 2),
                level.dimensions.height),
            positiveModulo(
                cell[2] -
                    static_cast<std::int64_t>(
                        level.dimensions.depth / 2),
                level.dimensions.depth),
            0,
        };
        levelConstants.validMargin = level.previousCellValid ?
            Int4{
                clampDifference<std::int32_t>(
                    level.previousCell[0] - cell[0],
                    level.dimensions.width),
                clampDifference<std::int32_t>(
                    level.previousCell[1] - cell[1],
                    level.dimensions.height),
                clampDifference<std::int32_t>(
                    level.previousCell[2] - cell[2],
                    level.dimensions.depth),
                0,
            } :
            Int4{
                static_cast<std::int32_t>(level.dimensions.width),
                static_cast<std::int32_t>(level.dimensions.height),
                static_cast<std::int32_t>(level.dimensions.depth),
                0,
            };
        const auto moved = !level.previousCellValid ||
            level.previousCell != cell;
        level.previousCell = cell;
        level.previousCellValid = true;
        ++captureRevision_;
        return moved;
    }

    void Runtime::publishConstants(bool featureActive) noexcept
    {
        if (!context_ || !constantsBuffer_) {
            return;
        }
        constants_.response = {
            std::bit_cast<float>(
                minimumDiffuseBits_.load(std::memory_order_relaxed)),
            std::bit_cast<float>(
                minimumSpecularBits_.load(std::memory_order_relaxed)),
            featureActive ? 1.0f : 0.0f,
            featureActive && diagnosticBuffer_ && diagnosticOutput_ &&
                    diagnosticStaging_ && diagnosticCompletion_ && !diagnosticSubmitted_ &&
                    !diagnosticPending_ && !diagnosticLogged_ ?
                1.0f :
                0.0f,
        };
        context_->UpdateSubresource(
            constantsBuffer_.Get(),
            0,
            nullptr,
            &constants_,
            0,
            0);
        publishedSettingsRevision_ =
            settingsRevision_.load(std::memory_order_acquire);
        publishedCaptureRevision_ = captureRevision_;
        publishedFeatureActive_ = featureActive;
    }

    void Runtime::dispatchProbeUpdate(
        ProbeResources& level,
        std::uint32_t levelIndex,
        std::uint32_t sliceStart,
        std::uint32_t sliceCount) noexcept
    {
        if (!context_ || !privateDepthResource_ || !level.probeOutput ||
            !level.accumulationOutput || !updateShader_ ||
            !comparisonSampler_ || !constantsBuffer_) {
            return;
        }
        const auto safeSliceStart = (std::min)(
            sliceStart,
            level.dimensions.depth - 1u);
        const auto safeSliceCount = (std::max)(
            1u,
            (std::min)(
                sliceCount,
                level.dimensions.depth - safeSliceStart));
        constants_.updateControl = {
            levelIndex,
            safeSliceStart,
            safeSliceCount,
            0,
        };
        // Publish the final response and slice window together. The capture
        // caller used to upload the same buffer immediately before this one.
        publishConstants(true);
        const auto collectDiagnostic =
            levelIndex == 0 && constants_.response.w > 0.5f;
        if (collectDiagnostic) {
            std::array<std::uint32_t, kDiagnosticStatCount> initial{};
            initial[8] = std::bit_cast<std::uint32_t>(1.0f);
            context_->UpdateSubresource(
                diagnosticBuffer_.Get(),
                0,
                nullptr,
                initial.data(),
                0,
                0);
        }
        {
            render::ScopedComputeState state(
                context_.Get(),
                {
                    .firstShaderResource = 0,
                    .shaderResourceCount = 1,
                    .firstUnorderedAccess = 0,
                    .unorderedAccessCount = 3,
                    .firstSampler = 0,
                    .samplerCount = 1,
                    .firstConstantBuffer = 13,
                    .constantBufferCount = 1,
                });
            if (!state.captured()) {
                return;
            }
            ID3D11ShaderResourceView* resource = privateDepthResource_.Get();
            std::array<ID3D11UnorderedAccessView*, 3> outputs{
                level.probeOutput.Get(),
                level.accumulationOutput.Get(),
                diagnosticOutput_.Get(),
            };
            ID3D11SamplerState* sampler = comparisonSampler_.Get();
            ID3D11Buffer* constants = constantsBuffer_.Get();
            context_->CSSetShaderResources(0, 1, &resource);
            context_->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(outputs.size()),
                outputs.data(),
                nullptr);
            context_->CSSetSamplers(0, 1, &sampler);
            context_->CSSetConstantBuffers(13, 1, &constants);
            context_->CSSetShader(updateShader_.Get(), nullptr, 0);
            context_->Dispatch(
                (level.dimensions.width + 7u) / 8u,
                (level.dimensions.height + 7u) / 8u,
                safeSliceCount);
        }
        if (collectDiagnostic) {
            context_->CopyResource(
                diagnosticStaging_.Get(),
                diagnosticBuffer_.Get());
            context_->End(diagnosticCompletion_.Get());
            diagnosticSubmitted_ = true;
            diagnosticPending_ = true;
        }
        const auto previousDispatches = probeDispatches_.fetch_add(
            1,
            std::memory_order_relaxed);
        level.dataValid = true;
        probeDataValid_.store(
            nearProbes_.dataValid,
            std::memory_order_release);
        if (previousDispatches == 0) {
            logging::info(
                "Skylighting completed its first exterior near-probe update (captures={}, depthBinds={}, slices={} of {}).",
                captureCalls_.load(std::memory_order_relaxed),
                privateDepthBinds_.load(std::memory_order_relaxed),
                safeSliceCount,
                level.dimensions.depth);
        }
        if (levelIndex == 1u &&
            !firstFarProbeUpdateLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Skylighting completed its first far-probe update ({}x{}x{}, slices={} of {}, field={:.0f} units).",
                level.dimensions.width,
                level.dimensions.height,
                level.dimensions.depth,
                safeSliceCount,
                level.dimensions.depth,
                kFarCaptureDistance);
        }
    }

    void Runtime::consumeDiagnosticReadback() noexcept
    {
        if (!diagnosticPending_ || !context_ || !diagnosticStaging_ ||
            !diagnosticCompletion_) {
            return;
        }
        BOOL complete{};
        const auto queryResult = context_->GetData(
            diagnosticCompletion_.Get(),
            &complete,
            sizeof(complete),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (queryResult == S_FALSE) {
            return;
        }
        diagnosticPending_ = false;
        diagnosticLogged_ = true;
        if (FAILED(queryResult)) {
            logging::warn(
                "Skylighting GPU visibility trace completion failed with HRESULT 0x{:08X}.",
                static_cast<std::uint32_t>(queryResult));
            return;
        }
        if (complete != TRUE) {
            logging::warn(
                "Skylighting GPU visibility trace completed without a signaled event.");
            return;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto mapResult = context_->Map(
            diagnosticStaging_.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped);
        if (FAILED(mapResult) || !mapped.pData) {
            logging::warn(
                "Skylighting GPU visibility trace readback failed with HRESULT 0x{:08X}.",
                static_cast<std::uint32_t>(mapResult));
            return;
        }
        std::array<std::uint32_t, kDiagnosticStatCount> stats{};
        std::memcpy(stats.data(), mapped.pData, kDiagnosticByteWidth);
        context_->Unmap(diagnosticStaging_.Get(), 0);
        logging::info(
            "Skylighting GPU visibility trace: sparseSamples={}, finiteProjection={}, insideUv={}, referenceInDepthRange={}, nonClearDepth={}, occluded={}, accumulated={}, changedProbe={}, sampledDepthMin={:.9f}, sampledDepthMax={:.9f}.",
            stats[0],
            stats[1],
            stats[2],
            stats[3],
            stats[4],
            stats[5],
            stats[6],
            stats[7],
            std::bit_cast<float>(stats[8]),
            std::bit_cast<float>(stats[9]));
    }

    void Runtime::submitAmbientDiagnostic() noexcept
    {
        if (ambientDiagnosticSubmitted_ || ambientDiagnosticPending_ ||
            ambientDiagnosticLogged_ || !context_ ||
            !ambientDiagnosticBuffer_ || !ambientDiagnosticStaging_ ||
            !ambientDiagnosticCompletion_ || !constantsBuffer_) {
            return;
        }
        constants_.response.w = 0.0f;
        context_->UpdateSubresource(
            constantsBuffer_.Get(),
            0,
            nullptr,
            &constants_,
            0,
            0);
        context_->CopyResource(
            ambientDiagnosticStaging_.Get(),
            ambientDiagnosticBuffer_.Get());
        context_->End(ambientDiagnosticCompletion_.Get());
        ambientDiagnosticSubmitted_ = true;
        ambientDiagnosticPending_ = true;
    }

    void Runtime::consumeAmbientDiagnosticReadback() noexcept
    {
        if (!ambientDiagnosticPending_ || !context_ ||
            !ambientDiagnosticStaging_ || !ambientDiagnosticCompletion_) {
            return;
        }
        const auto completion = context_->GetData(
            ambientDiagnosticCompletion_.Get(),
            nullptr,
            0,
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (completion == S_FALSE) {
            return;
        }
        ambientDiagnosticPending_ = false;
        ambientDiagnosticLogged_ = true;
        if (FAILED(completion)) {
            logging::warn(
                "Skylighting ambient-consumer diagnostic completion failed with HRESULT 0x{:08X}.",
                static_cast<std::uint32_t>(completion));
            return;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto mapResult = context_->Map(
            ambientDiagnosticStaging_.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped);
        if (FAILED(mapResult) || !mapped.pData) {
            logging::warn(
                "Skylighting ambient-consumer diagnostic readback failed with HRESULT 0x{:08X}.",
                static_cast<std::uint32_t>(mapResult));
            return;
        }
        std::array<std::uint32_t, kAmbientDiagnosticStatCount> stats{};
        std::memcpy(stats.data(), mapped.pData, kAmbientDiagnosticByteWidth);
        context_->Unmap(ambientDiagnosticStaging_.Get(), 0);
        logging::info(
            "Skylighting ambient-consumer trace: sparsePixels={}, depthValid={}, finitePosition={}, insideVolume={}, weightedSample={}, positiveFade={}, diffuseNonNeutral={}, specularNonNeutral={}, diffuseRange={:.6f}..{:.6f}, specularRange={:.6f}..{:.6f}, fadeRange={:.6f}..{:.6f}, sampledDepthRgba=({:.6f}..{:.6f}, {:.6f}..{:.6f}, {:.6f}..{:.6f}, {:.6f}..{:.6f}), screenUv=({:.6f}..{:.6f}, {:.6f}..{:.6f}), loadedDepthRgba=({:.6f}..{:.6f}, {:.6f}..{:.6f}, {:.6f}..{:.6f}, {:.6f}..{:.6f}).",
            stats[0],
            stats[1],
            stats[2],
            stats[3],
            stats[4],
            stats[5],
            stats[6],
            stats[7],
            std::bit_cast<float>(stats[8]),
            std::bit_cast<float>(stats[9]),
            std::bit_cast<float>(stats[10]),
            std::bit_cast<float>(stats[11]),
            std::bit_cast<float>(stats[12]),
            std::bit_cast<float>(stats[13]),
            std::bit_cast<float>(stats[14]),
            std::bit_cast<float>(stats[15]),
            std::bit_cast<float>(stats[16]),
            std::bit_cast<float>(stats[17]),
            std::bit_cast<float>(stats[18]),
            std::bit_cast<float>(stats[19]),
            std::bit_cast<float>(stats[20]),
            std::bit_cast<float>(stats[21]),
            std::bit_cast<float>(stats[22]),
            std::bit_cast<float>(stats[23]),
            std::bit_cast<float>(stats[24]),
            std::bit_cast<float>(stats[25]),
            std::bit_cast<float>(stats[26]),
            std::bit_cast<float>(stats[27]),
            std::bit_cast<float>(stats[28]),
            std::bit_cast<float>(stats[29]),
            std::bit_cast<float>(stats[30]),
            std::bit_cast<float>(stats[31]),
            std::bit_cast<float>(stats[32]),
            std::bit_cast<float>(stats[33]));
    }

    void Runtime::onNativePrecipitationFrame(
        void* precipitation,
        NativePrecipitationRender render,
        NativeProjectionSetup restoreProjection) noexcept
    {
        captureCalls_.fetch_add(1, std::memory_order_relaxed);
        consumeDiagnosticReadback();
        consumeAmbientDiagnosticReadback();
        const auto featureRequested = requested();
        if (!featureRequested) {
            exteriorActive_.store(false, std::memory_order_release);
            return;
        }
        const auto resourcesReady = ensureGpuResources();
        const auto hookOwned =
            nativeHookOwned_.load(std::memory_order_acquire);
        if (!resourcesReady || !hookOwned ||
            !precipitation || !render || !restoreProjection || !context_) {
            if (!firstPrerequisiteRejectionLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting rejected its first native callback prerequisite gate (requested={}, gpuReady={}, hookOwned={}, manager={}, render={}, projection={}, context={}).",
                    featureRequested,
                    resourcesReady,
                    hookOwned,
                    precipitation != nullptr,
                    render != nullptr,
                    restoreProjection != nullptr,
                    context_.Get() != nullptr);
            }
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* playerCell = player ? player->GetParentCell() : nullptr;
        const auto* playerCamera = RE::PlayerCamera::GetSingleton();
        const auto exterior = playerCell && playerCell->IsExterior();
        if (!exterior || !playerCamera || !playerCamera->cameraRoot) {
            if (!firstWorldStateRejectionLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting rejected its first world-state gate (player={}, cell={}, exterior={}, camera={}, cameraRoot={}).",
                    player != nullptr,
                    playerCell != nullptr,
                    exterior,
                    playerCamera != nullptr,
                    playerCamera && playerCamera->cameraRoot != nullptr);
            }
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (resetRequested_.load(std::memory_order_acquire)) {
            clearProbeResources();
        }

        const auto base = REL::Module::get().base();
        auto* rendererData = RE::BSGraphics::RendererData::GetSingleton();
        const auto* mappedTargetEntry = reinterpret_cast<const std::int32_t*>(
            base + kRendererStateRva + kDepthTargetMapOffset +
            kNativePrecipitationDepthTarget * sizeof(std::int32_t));
        if (!rendererData ||
            !isReadableRange(mappedTargetEntry, sizeof(*mappedTargetEntry))) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting could not resolve the FO4VR renderer base or logical depth-target map entry 9.");
            }
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        std::int32_t mappedDepthTarget{};
        std::memcpy(
            &mappedDepthTarget,
            mappedTargetEntry,
            sizeof(mappedDepthTarget));
        if (mappedDepthTarget < 0 ||
            mappedDepthTarget >=
                static_cast<std::int32_t>(
                    kFo4VrDepthStencilTargetCount)) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting logical depth target 9 mapped outside the verified FO4VR range (mapped={}, count={}).",
                    mappedDepthTarget,
                    kFo4VrDepthStencilTargetCount);
            }
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto* nativeDepthTarget = reinterpret_cast<Fo4VrDepthStencilTarget*>(
            reinterpret_cast<std::byte*>(rendererData) +
            kFo4VrDepthStencilTargetsOffset +
            static_cast<std::size_t>(mappedDepthTarget) *
                sizeof(Fo4VrDepthStencilTarget));
        const auto targetReadable = isReadableRange(
            nativeDepthTarget,
            sizeof(*nativeDepthTarget));
        auto* sourceTexture = targetReadable
            ? nativeDepthTarget->texture
            : nullptr;
        auto* sourceDepthView = targetReadable
            ? nativeDepthTarget->depthViews[0]
            : nullptr;
        auto* sourceDepthResource = targetReadable
            ? nativeDepthTarget->depthResource
            : nullptr;
        if (!sourceTexture || !sourceDepthView || !sourceDepthResource) {
            if (!firstPrivateDepthFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting resolved an incomplete FO4VR precipitation depth target (logical=9, mapped={}, target={}, texture={}, dsv0={}, depthSrv={}).",
                    mappedDepthTarget,
                    static_cast<void*>(nativeDepthTarget),
                    static_cast<void*>(sourceTexture),
                    static_cast<void*>(sourceDepthView),
                    static_cast<void*>(sourceDepthResource));
            }
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!ensurePrivateDepth(sourceDepthView) ||
            !privateDepthTexture_ || !privateDepthView_ ||
            !privateDepthResource_) {
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto* cubeSize = reinterpret_cast<float*>(base + kCubeSizeRva);
        auto* directionX = reinterpret_cast<float*>(base + kDirectionXRva);
        auto* directionY = reinterpret_cast<float*>(base + kDirectionYRva);
        auto* directionZ = reinterpret_cast<float*>(base + kDirectionZRva);
        auto* precipitationBytes = static_cast<std::byte*>(precipitation);
        auto* lastCubeSize = reinterpret_cast<float*>(
            precipitationBytes + kPrecipitationLastCubeSizeOffset);
        const auto savedCubeSize = *cubeSize;
        const auto savedDirectionX = *directionX;
        const auto savedDirectionY = *directionY;
        const auto savedDirectionZ = *directionZ;
        const auto savedLastCubeSize = *lastCubeSize;

        const auto frame = frameIndex_.fetch_add(
            1,
            std::memory_order_relaxed);
        const auto updateFarLevel = frame != 0 &&
            frame % kFarCaptureInterval ==
                kFarCaptureInterval - 1;
        auto& targetLevel = updateFarLevel ? farProbes_ : nearProbes_;
        auto& targetConstants = updateFarLevel ?
            constants_.farLevel : constants_.nearLevel;
        const auto targetDistance = updateFarLevel ?
            kFarCaptureDistance : kCaptureDistance;
        const auto targetHeight = updateFarLevel ?
            kFarCaptureHeight : kCaptureHeight;
        auto u = 0.5f + static_cast<float>(frame) * kR2X;
        auto v = 0.5f + static_cast<float>(frame) * kR2Y;
        u -= std::floor(u);
        v -= std::floor(v);
        const auto zenith = std::bit_cast<float>(
            maximumZenithBits_.load(std::memory_order_relaxed));
        const auto radius = std::sqrt(
            (std::max)(0.0f, u * std::sin(zenith * kDegreesToRadians)));
        const auto angle = v * kTwoPi;
        const auto skyX = radius * std::cos(angle);
        const auto skyY = radius * std::sin(angle);
        const auto skyZ = std::sqrt(
            (std::max)(0.0f, 1.0f - radius * radius));

        *cubeSize = targetDistance;
        *lastCubeSize = targetDistance;
        *directionX = -skyX;
        *directionY = -skyY;
        *directionZ = -skyZ;
        constants_.occlusionDirection = { skyX, skyY, skyZ, 0.0f };
        nativeOutput_.fill(std::byte{});
        if (!firstRenderAttemptLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Skylighting entered its first verified exterior private-render transaction with logical depth target 9 mapped to FO4VR target {} and scoped to the private resource.",
                mappedDepthTarget);
        }
        auto timing = gpuTiming_.begin();
        context_->ClearDepthStencilView(
            privateDepthView_.Get(),
            D3D11_CLEAR_DEPTH,
            kPrivateDepthClearValue,
            0);
        const auto privateRenderStart = std::chrono::steady_clock::now();
        auto privateRenderCompleted = false;
        {
            ScopedNativeDepthTarget privateTarget(
                *nativeDepthTarget,
                privateDepthTexture_.Get(),
                privateDepthView_.Get(),
                privateDepthResource_.Get());
            ScopedOcclusionPassProduction passProduction(
                targetLevel.captureQuadrant);
            if (privateTarget.active() && passProduction.active()) {
                privateDepthBinds_.fetch_add(1, std::memory_order_relaxed);
                privateRenderActive_.store(
                    true,
                    std::memory_order_release);
                render(precipitation, nativeOutput_.data());
                privateRenderActive_.store(
                    false,
                    std::memory_order_release);
                privateRenderCompleted = true;
            }
        }
        const auto privateRenderEnd = std::chrono::steady_clock::now();
        timing.mark();
        if (!firstPassProducerSummaryLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            const auto elapsedMilliseconds =
                std::chrono::duration<double, std::milli>(
                    privateRenderEnd - privateRenderStart)
                    .count();
            logging::info(
                "Skylighting first private capture render completed={} in {:.3f} ms without hot-path diagnostics.",
                privateRenderCompleted,
                elapsedMilliseconds);
        }

        std::memcpy(
            constants_.occlusionViewProjection.data(),
            nativeOutput_.data() + kNativeProjectionOffset,
            sizeof(constants_.occlusionViewProjection));
        *cubeSize = savedCubeSize;
        *lastCubeSize = savedLastCubeSize;
        *directionX = savedDirectionX;
        *directionY = savedDirectionY;
        *directionZ = savedDirectionZ;

        auto* camera = *reinterpret_cast<void**>(
            precipitationBytes + kPrecipitationCameraOffset);
        if (camera) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(
                static_cast<std::byte*>(camera) + 8));
            auto* cameraHolder = camera;
            restoreProjection(precipitation, &cameraHolder);
        }
        if (!privateRenderCompleted) {
            exteriorActive_.store(false, std::memory_order_release);
            rejectedCaptures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        targetLevel.captureQuadrant =
            (targetLevel.captureQuadrant + 1u) % 4u;
        const auto& cameraPosition = playerCamera->cameraRoot->world.translate;
        const auto volumeMoved = updateRollingVolume(
            targetLevel,
            targetConstants,
            targetDistance,
            targetHeight,
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z);
        const auto baseStableSlices = updateFarLevel ?
            8u :
            (activeQuality_ == Quality::high ?
                    13u :
                    (activeQuality_ == Quality::medium ? 11u : 8u));
        const auto stableSlices = (std::min)(
            targetLevel.dimensions.depth,
            baseStableSlices * 4u);
        std::uint32_t sliceStart{};
        std::uint32_t sliceCount{};
        if (volumeMoved || !targetLevel.dataValid) {
            sliceCount = targetLevel.dimensions.depth;
            targetLevel.updateSliceCursor = 0;
        } else {
            sliceStart = targetLevel.updateSliceCursor;
            sliceCount = (std::min)(
                stableSlices,
                targetLevel.dimensions.depth - sliceStart);
            targetLevel.updateSliceCursor =
                (sliceStart + sliceCount) %
                targetLevel.dimensions.depth;
        }
        exteriorActive_.store(true, std::memory_order_release);
        dispatchProbeUpdate(
            targetLevel,
            updateFarLevel ? 1u : 0u,
            sliceStart,
            sliceCount);
    }

    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire);
    }

    ScopedAmbientBindings Runtime::scopeAmbientDraw(
        ID3D11DeviceContext* context,
        bool ambientReplacementActive) noexcept
    {
        if (!ambientReplacementActive || !context ||
            context != context_.Get() || !constantsBuffer_) {
            return {};
        }
        const auto active = requested() &&
            gpuResourcesReady_.load(std::memory_order_acquire) &&
            nativeHookOwned_.load(std::memory_order_acquire) &&
            exteriorActive_.load(std::memory_order_acquire) &&
            privateDepthReady_.load(std::memory_order_acquire) &&
            probeDataValid_.load(std::memory_order_acquire) &&
            nearProbes_.probeResource && farProbes_.probeResource;
        const auto settingsRevision =
            settingsRevision_.load(std::memory_order_acquire);
        if (settingsRevision != publishedSettingsRevision_ ||
            captureRevision_ != publishedCaptureRevision_ ||
            active != publishedFeatureActive_) {
            publishConstants(active);
        }
        if (active && !firstActiveAmbientBindLogged_.exchange(
                          true,
                          std::memory_order_relaxed)) {
            logging::info(
                "Skylighting bound its first active world-space probe to DFLight ambient shading.");
        }
        const auto collectAmbientDiagnostic = active &&
            !ambientDiagnosticSubmitted_ && !ambientDiagnosticPending_ &&
            !ambientDiagnosticLogged_ && ambientDiagnosticBuffer_ &&
            ambientDiagnosticOutput_ && ambientDiagnosticStaging_ &&
            ambientDiagnosticCompletion_;
        if (collectAmbientDiagnostic) {
            std::array<std::uint32_t, kAmbientDiagnosticStatCount> initial{};
            initial[8] = std::bit_cast<std::uint32_t>(1.0f);
            initial[10] = std::bit_cast<std::uint32_t>(1.0f);
            initial[12] = std::bit_cast<std::uint32_t>(1.0f);
            const auto diagnosticRangeMinimum =
                std::bit_cast<std::uint32_t>(
                    std::numeric_limits<float>::max());
            for (std::size_t index = 14;
                 index < kAmbientDiagnosticStatCount;
                 index += 2) {
                initial[index] = diagnosticRangeMinimum;
            }
            context_->UpdateSubresource(
                ambientDiagnosticBuffer_.Get(),
                0,
                nullptr,
                initial.data(),
                0,
                0);
            constants_.response.w = 1.0f;
            context_->UpdateSubresource(
                constantsBuffer_.Get(),
                0,
                nullptr,
                &constants_,
                0,
                0);
        }
        ambientBinds_.fetch_add(1, std::memory_order_relaxed);
        return ScopedAmbientBindings(
            context,
            active ? nearProbes_.probeResource.Get() : nullptr,
            active ? farProbes_.probeResource.Get() : nullptr,
            constantsBuffer_.Get(),
            collectAmbientDiagnostic ? ambientDiagnosticOutput_.Get() :
                                       nullptr,
            &ambientDrawGpuTiming_,
            collectAmbientDiagnostic ? this : nullptr);
    }

    void Runtime::observePrivateCaptureDraw(
        ID3D11DeviceContext* context) noexcept
    {
        if (!context || context != context_.Get() ||
            !privateRenderActive_.load(std::memory_order_acquire) ||
            privateCaptureDrawStateLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            return;
        }

        ComPtr<ID3D11DepthStencilView> boundDepth;
        context->OMGetRenderTargets(
            0,
            nullptr,
            boundDepth.GetAddressOf());
        D3D11_DEPTH_STENCIL_VIEW_DESC viewDescription{};
        if (boundDepth) {
            boundDepth->GetDesc(&viewDescription);
        }

        ComPtr<ID3D11DepthStencilState> depthState;
        UINT stencilReference{};
        context->OMGetDepthStencilState(
            depthState.GetAddressOf(),
            &stencilReference);
        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        if (depthState) {
            depthState->GetDesc(&depthDescription);
        } else {
            depthDescription.DepthEnable = TRUE;
            depthDescription.DepthWriteMask =
                D3D11_DEPTH_WRITE_MASK_ALL;
            depthDescription.DepthFunc = D3D11_COMPARISON_LESS;
        }

        D3D11_VIEWPORT viewport{};
        UINT viewportCount = 1;
        context->RSGetViewports(&viewportCount, &viewport);
        logging::info(
            "Skylighting first private draw state: privateDsvBound={}, boundDsv={}, expectedDsv={}, dsvFormat={}, dsvDimension={}, depthEnabled={}, depthWriteMask={}, depthFunc={}, stencilRef={}, viewportCount={}, viewport=({:.3f},{:.3f},{:.3f},{:.3f},{:.6f},{:.6f}).",
            boundDepth.Get() == privateDepthView_.Get(),
            static_cast<void*>(boundDepth.Get()),
            static_cast<void*>(privateDepthView_.Get()),
            static_cast<std::uint32_t>(viewDescription.Format),
            static_cast<std::uint32_t>(viewDescription.ViewDimension),
            depthDescription.DepthEnable != FALSE,
            static_cast<std::uint32_t>(
                depthDescription.DepthWriteMask),
            static_cast<std::uint32_t>(depthDescription.DepthFunc),
            stencilReference,
            viewportCount,
            viewport.TopLeftX,
            viewport.TopLeftY,
            viewport.Width,
            viewport.Height,
            viewport.MinDepth,
            viewport.MaxDepth);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .requested = requested(),
            .gpuResourcesReady =
                gpuResourcesReady_.load(std::memory_order_acquire),
            .nativeHookOwned =
                nativeHookOwned_.load(std::memory_order_acquire),
            .exteriorActive =
                exteriorActive_.load(std::memory_order_acquire),
            .privateDepthReady =
                privateDepthReady_.load(std::memory_order_acquire),
            .probeDataValid =
                probeDataValid_.load(std::memory_order_acquire),
            .requestedQuality = sanitizeQuality(
                requestedQuality_.load(std::memory_order_relaxed)),
            .activeQuality = activeQuality_,
            .probeWidth = nearProbes_.dimensions.width,
            .probeHeight = nearProbes_.dimensions.height,
            .probeDepth = nearProbes_.dimensions.depth,
            .captureCalls = captureCalls_.load(std::memory_order_relaxed),
            .privateDepthBinds =
                privateDepthBinds_.load(std::memory_order_relaxed),
            .probeDispatches =
                probeDispatches_.load(std::memory_order_relaxed),
            .rejectedCaptures =
                rejectedCaptures_.load(std::memory_order_relaxed),
            .ambientBinds = ambientBinds_.load(std::memory_order_relaxed),
        };
    }
}
