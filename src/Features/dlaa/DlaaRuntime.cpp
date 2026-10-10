#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/DlaaCameraValidation.h"
#include "diagnostics/RenderDocCapture.h"

#include "GeneratedUpscalingComposeShader.h"
#include "GeneratedUpscalingMotionRepairShader.h"
#include "GeneratedUpscalingDepthShader.h"
#include "GeneratedUpscalingDepthVS.h"
#include "GeneratedUpscalingDepthPS.h"
#include "Features/volumetric_lighting/VolumetricStateScope.h"
#include "render/Fo4VrDepth.h"
#include "render/DebugResourceName.h"
#include "GeneratedUpscalingReactiveMaskShader.h"

#include "render/ComputeStateScope.h"
#include "render/GpuTimingProfiler.h"
#include "support/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace csah::dlaa
{
    namespace
    {
        struct PendingCameraMap
        {
            ID3D11Resource* resource{};
            UINT subresource{};
            const void* data{};
            UINT byteWidth{};
            D3D11_MAP mapType{};
            bool matchesBoundCamera{};
            bool candidate{};
        };

        thread_local PendingCameraMap pendingCameraMap;

        constexpr UINT kProbeSampleCount = 16;
        constexpr std::uint32_t kMaximumProbePolls = 120;
        constexpr std::uint32_t kDiagnosticProbeAttempts = 5;
        constexpr std::uint64_t kQualificationProbeFrameCadence = 60;
        constexpr std::size_t kDynamicResolutionWidthOffset = 0x1664;
        constexpr std::size_t kDynamicResolutionHeightOffset = 0x1668;
        constexpr std::size_t kDynamicResolutionActiveOffset = 0x1680;
        constexpr std::size_t kDynamicResolutionRegionOffset = 0x1683;
        constexpr std::uint64_t kPerformanceTimingReportSamples = 120;

        struct alignas(16) ComposeConstants
        {
            std::uint32_t destinationLeft{};
            std::uint32_t destinationTop{};
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t eyeStride{};
            float featherPixels{};
            float sharpness{};
            std::uint32_t visualizeCenter{};
        };

        static_assert(sizeof(ComposeConstants) == 32);

        struct alignas(16) MotionRepairConstants
        {
            std::uint32_t width{};
            std::uint32_t height{};
            float nearPlane{};
            float farPlane{};
            std::uint32_t resetHistory{};
            float historyWeight{};
            float farDepthStart{};
            std::uint32_t depthInverted{};
        };

        static_assert(sizeof(MotionRepairConstants) == 32);

        struct alignas(16) ReactiveMaskConstants
        {
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t sourceLeft{};
            std::uint32_t sourceTop{};
        };

        static_assert(sizeof(ReactiveMaskConstants) == 16);

        // A writable native DSV conflicts with any SRV of its resource,
        // including bindings left outside the current shader's used slots.
        // Retain only those aliases and restore them after the DSV is unbound.
        class ScopedDepthAliases final
        {
        public:
            ScopedDepthAliases(ID3D11DeviceContext* context, ID3D11Resource* depth) noexcept : context_(context)
            {
                constexpr std::array getters{
                    &ID3D11DeviceContext::VSGetShaderResources, &ID3D11DeviceContext::PSGetShaderResources,
                    &ID3D11DeviceContext::GSGetShaderResources, &ID3D11DeviceContext::HSGetShaderResources,
                    &ID3D11DeviceContext::DSGetShaderResources, &ID3D11DeviceContext::CSGetShaderResources};
                for (std::size_t stage = 0; stage < getters.size(); ++stage) {
                    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
                    (context_->*getters[stage])(0, static_cast<UINT>(views.size()), views.data());
                    for (UINT slot = 0; slot < views.size(); ++slot) {
                        auto* view = views[slot];
                        if (!view) continue;
                        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
                        view->GetResource(&resource);
                        if (resource.Get() == depth) {
                            aliases_[stage][slot] = view;
                            ID3D11ShaderResourceView* empty{};
                            (context_->*setters_[stage])(slot, 1, &empty);
                        } else view->Release();
                    }
                }
            }
            ~ScopedDepthAliases() noexcept
            {
                for (std::size_t stage = 0; stage < aliases_.size(); ++stage)
                    for (UINT slot = 0; slot < aliases_[stage].size(); ++slot)
                        if (auto* view = aliases_[stage][slot]) {
                            (context_->*setters_[stage])(slot, 1, &view);
                            view->Release();
                        }
            }
            ScopedDepthAliases(const ScopedDepthAliases&) = delete;
            ScopedDepthAliases& operator=(const ScopedDepthAliases&) = delete;
        private:
            inline static constexpr std::array setters_{
                &ID3D11DeviceContext::VSSetShaderResources, &ID3D11DeviceContext::PSSetShaderResources,
                &ID3D11DeviceContext::GSSetShaderResources, &ID3D11DeviceContext::HSSetShaderResources,
                &ID3D11DeviceContext::DSSetShaderResources, &ID3D11DeviceContext::CSSetShaderResources};
            ID3D11DeviceContext* context_{};
            std::array<std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>, 6> aliases_{};
        };

        class ScopedImageSpaceBindings final
        {
        public:
            explicit ScopedImageSpaceBindings(
                ID3D11DeviceContext* context) noexcept :
                context_(context)
            {
                if (!context_) {
                    return;
                }
                context_->PSGetShaderResources(
                    0,
                    static_cast<UINT>(shaderResources_.size()),
                    shaderResources_.data());
                context_->OMGetRenderTargets(
                    static_cast<UINT>(renderTargets_.size()),
                    renderTargets_.data(),
                    &depth_);
                std::array<ID3D11ShaderResourceView*, 5> nullResources{};
                context_->PSSetShaderResources(
                    0,
                    static_cast<UINT>(nullResources.size()),
                    nullResources.data());
                context_->OMSetRenderTargets(0, nullptr, nullptr);
                captured_ = true;
            }

            ~ScopedImageSpaceBindings()
            {
                (void)restore();
            }

            ScopedImageSpaceBindings(const ScopedImageSpaceBindings&) =
                delete;
            ScopedImageSpaceBindings& operator=(
                const ScopedImageSpaceBindings&) = delete;

            [[nodiscard]] bool captured() const noexcept
            {
                return captured_;
            }

            [[nodiscard]] bool renderTargetTexture(
                std::uint32_t slot,
                Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture)
                const noexcept
            {
                texture.Reset();
                if (!captured_ || slot >= renderTargets_.size() ||
                    !renderTargets_[slot]) {
                    return false;
                }
                Microsoft::WRL::ComPtr<ID3D11Resource> resource;
                renderTargets_[slot]->GetResource(resource.GetAddressOf());
                return resource && SUCCEEDED(resource.As(&texture));
            }

            [[nodiscard]] bool restore() noexcept
            {
                if (!captured_ || !context_) {
                    return false;
                }
                context_->OMSetRenderTargets(
                    static_cast<UINT>(renderTargets_.size()),
                    renderTargets_.data(),
                    depth_);
                context_->PSSetShaderResources(
                    0,
                    static_cast<UINT>(shaderResources_.size()),
                    shaderResources_.data());
                captured_ = false;
                release();
                context_ = nullptr;
                return true;
            }

        private:
            void release() noexcept
            {
                for (auto*& resource : shaderResources_) {
                    if (resource) {
                        resource->Release();
                        resource = nullptr;
                    }
                }
                for (auto*& target : renderTargets_) {
                    if (target) {
                        target->Release();
                        target = nullptr;
                    }
                }
                if (depth_) {
                    depth_->Release();
                    depth_ = nullptr;
                }
            }

            ID3D11DeviceContext* context_{};
            std::array<ID3D11ShaderResourceView*, 5> shaderResources_{};
            std::array<
                ID3D11RenderTargetView*,
                D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
                renderTargets_{};
            ID3D11DepthStencilView* depth_{};
            bool captured_{};
        };

        [[nodiscard]] Matrix4 multiply(
            const Matrix4& left,
            const Matrix4& right) noexcept
        {
            Matrix4 result{};
            const auto* a = &left[0].x;
            const auto* b = &right[0].x;
            auto* output = &result[0].x;
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    float value{};
                    for (std::size_t inner = 0; inner < 4; ++inner) {
                        value += a[row * 4 + inner] *
                            b[inner * 4 + column];
                    }
                    output[row * 4 + column] = value;
                }
            }
            return result;
        }

        [[nodiscard]] StreamlineMatrix transposeForStreamline(
            const Matrix4& source) noexcept
        {
            StreamlineMatrix result{};
            const auto* input = &source[0].x;
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    result.values[row * 4 + column] =
                        input[column * 4 + row];
                }
            }
            return result;
        }

        [[nodiscard]] float halton(
            std::uint32_t index,
            std::uint32_t base) noexcept
        {
            float result{};
            float fraction = 1.0f;
            while (index != 0) {
                fraction /= static_cast<float>(base);
                result += fraction * static_cast<float>(index % base);
                index /= base;
            }
            return result;
        }

        [[nodiscard]] bool textureDescription(
            ID3D11Resource* resource,
            D3D11_TEXTURE2D_DESC& description,
            Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture) noexcept
        {
            if (!resource) {
                return false;
            }
            D3D11_RESOURCE_DIMENSION dimension{};
            resource->GetType(&dimension);
            if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                return false;
            }
            texture = static_cast<ID3D11Texture2D*>(resource);
            texture->GetDesc(&description);
            return true;
        }

        [[nodiscard]] const char* resourceName(
            ID3D11DeviceChild* resource,
            std::array<char, 256>& storage) noexcept
        {
            if (!resource) {
                return "";
            }
            UINT bytes = static_cast<UINT>(storage.size());
            if (SUCCEEDED(resource->GetPrivateData(
                    WKPDID_D3DDebugObjectName,
                    &bytes,
                    storage.data())) && bytes != 0) {
                storage[(std::min)(
                    static_cast<std::size_t>(bytes),
                    storage.size() - 1)] = '\0';
                return storage.data();
            }
            return "";
        }

        void logTexture(
            const char* boundary,
            UINT slot,
            ID3D11Resource* resource) noexcept
        {
            if (!resource) {
                return;
            }
            D3D11_RESOURCE_DIMENSION dimension{};
            resource->GetType(&dimension);
            if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                return;
            }
            auto* texture = static_cast<ID3D11Texture2D*>(resource);
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            std::array<char, 256> name{};
            logging::info(
                "DLAA qualification resource: boundary={}, slot={}, resource={}, name='{}', width={}, height={}, format={}, array={}, mipLevels={}, samples={}, bind=0x{:X}, usage={}.",
                boundary,
                slot,
                static_cast<const void*>(resource),
                resourceName(texture, name),
                description.Width,
                description.Height,
                static_cast<std::uint32_t>(description.Format),
                description.ArraySize,
                description.MipLevels,
                description.SampleDesc.Count,
                description.BindFlags,
                static_cast<std::uint32_t>(description.Usage));
        }

        [[nodiscard]] bool finiteMatrix(const Matrix4& matrix) noexcept
        {
            for (const auto& row : matrix) {
                if (!std::isfinite(row.x) || !std::isfinite(row.y) ||
                    !std::isfinite(row.z) || !std::isfinite(row.w)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] float halfToFloat(std::uint16_t value) noexcept
        {
            const auto sign = static_cast<std::uint32_t>(value & 0x8000u)
                << 16;
            auto exponent = static_cast<std::uint32_t>(
                (value >> 10) & 0x1Fu);
            auto mantissa = static_cast<std::uint32_t>(value & 0x03FFu);
            std::uint32_t bits{};
            if (exponent == 0) {
                if (mantissa == 0) {
                    bits = sign;
                } else {
                    exponent = 113;
                    while ((mantissa & 0x0400u) == 0) {
                        mantissa <<= 1;
                        --exponent;
                    }
                    mantissa &= 0x03FFu;
                    bits = sign | (exponent << 23) | (mantissa << 13);
                }
            } else if (exponent == 0x1Fu) {
                bits = sign | 0x7F800000u | (mantissa << 13);
            } else {
                bits = sign | ((exponent + 112u) << 23) |
                    (mantissa << 13);
            }
            return std::bit_cast<float>(bits);
        }

        [[nodiscard]] float decodeUnsignedFloat(
            std::uint32_t value,
            std::uint32_t mantissaBits) noexcept
        {
            const auto mantissaMask = (1u << mantissaBits) - 1u;
            const auto mantissa = value & mantissaMask;
            const auto exponent = (value >> mantissaBits) & 0x1Fu;
            if (exponent == 0) {
                return std::ldexp(
                    static_cast<float>(mantissa),
                    1 - 15 - static_cast<int>(mantissaBits));
            }
            if (exponent == 0x1Fu) {
                return mantissa == 0 ?
                    std::numeric_limits<float>::infinity() :
                    std::numeric_limits<float>::quiet_NaN();
            }
            return std::ldexp(
                1.0f + static_cast<float>(mantissa) /
                    static_cast<float>(1u << mantissaBits),
                static_cast<int>(exponent) - 15);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        {
            std::scoped_lock lock(settingsMutex_);
            pendingSettings_ = safe;
        }
        requested_.store(safe.enabled, std::memory_order_release);
        pendingSettingsRevision_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::setEnabled(bool enabled) noexcept
    {
        {
            std::scoped_lock lock(settingsMutex_);
            pendingSettings_.enabled = enabled;
        }
        requested_.store(enabled, std::memory_order_release);
        pendingSettingsRevision_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::requestRefresh() noexcept
    {
        refreshRequested_.store(true, std::memory_order_release);
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        device_ = device;
        context_ = context;
        deviceReady_.store(
            device_ && context_,
            std::memory_order_release);
        cameraBufferQualified_.store(false, std::memory_order_release);
        cameraBuffer_.Reset();
        worldCameraCapture_.reset();
        worldCameraDiagnosticLogged_ = missingWorldCameraLogged_ = false;
        pendingCameraMap = {};
        cameraBufferIdentity_.store(nullptr, std::memory_order_release);
        qualificationProbes_ = {};
        clearRenderResources(false);
        compositorShader_.Reset();
        compositorConstants_.Reset();
        depthPreparationShader_.Reset();
        depthPreparationConstants_.Reset();
        depthPublicationVertex_.Reset();
        depthPublicationPixel_.Reset();
        depthPublicationConstants_.Reset();
        depthPublicationState_.Reset();
        depthPublicationRaster_.Reset();
        motionRepairShader_.Reset();
        motionRepairConstants_.Reset();
        reactiveMaskShader_.Reset();
        reactiveMaskConstants_.Reset();
        taaMaskViewCache_ = {};
        taaMaskViewCacheReplaceIndex_ = 0;
        compositionSurface_.Reset();
        compositionSurfaceView_.Reset();
        compositionEyeWidth_ = 0;
        compositionHeight_ = 0;
        evaluationGeometry_ = {};
        gpuTimingSlots_ = {};
        gpuTimingWriteIndex_ = 0;
        gpuTimingSamples_ = 0;
        gpuLeftMilliseconds_ = 0.0;
        gpuRightMilliseconds_ = 0.0;
        gpuCommitMilliseconds_ = 0.0;
        gpuTotalMilliseconds_ = 0.0;
        publishedGpuLeftMilliseconds_.store(0.0, std::memory_order_relaxed);
        publishedGpuRightMilliseconds_.store(0.0, std::memory_order_relaxed);
        publishedGpuCommitMilliseconds_.store(0.0, std::memory_order_relaxed);
        publishedGpuTotalMilliseconds_.store(0.0, std::memory_order_relaxed);
        cpuTimingSamples_ = 0;
        cpuCaptureMicroseconds_ = 0.0;
        cpuEvaluateMicroseconds_ = 0.0;
        cpuCommitMicroseconds_ = 0.0;
        cpuRestoreMicroseconds_ = 0.0;
        qualificationProbeAttempts_.store(0, std::memory_order_relaxed);
        renderResourcesQualified_.store(false, std::memory_order_release);
        operational_.store(false, std::memory_order_release);
        qualificationCaptureRequested_.store(true, std::memory_order_release);
        firstCameraQualifiedLogged_.store(false, std::memory_order_relaxed);
        firstCameraValidationFailureLogged_.store(
            false,
            std::memory_order_relaxed);
        firstResourceContractLogged_.store(false, std::memory_order_relaxed);
        firstEvaluationLogged_.store(false, std::memory_order_relaxed);
        firstEvaluationFailureLogged_.store(false, std::memory_order_relaxed);
        firstColorComparisonQueued_.store(false, std::memory_order_relaxed);
        firstActiveColorComparisonQueued_.store(
            false,
            std::memory_order_relaxed);
        lastValidCameraPostCall_.store(0, std::memory_order_relaxed);
        streamlineFrameIndex_ = 0;
        jitterPhase_ = 0;
        resetHistory_ = true;
        renderThreadSettingsRevision_ = 0;
        renderThreadRequested_ = requested_.load(std::memory_order_acquire);
        dynamicResolutionOwned_ = false;
        dynamicResolutionManager_ = nullptr;
        logging::info(
            "DLAA captured the native D3D11 device for stereo resource discovery; vanilla TAA remains active.");
    }

    void Runtime::beginQualificationSession(const char* reason) noexcept
    {
        qualificationSessions_.fetch_add(1, std::memory_order_relaxed);
        qualificationResetRequested_.store(true, std::memory_order_release);
        logging::info("DLAA qualification reset queued by {}.", reason ? reason : "unknown");
    }

    void Runtime::applyQualificationSession() noexcept
    {
        if (!qualificationResetRequested_.exchange(false, std::memory_order_acq_rel)) return;
        const auto hardResetOnLoad = settings_.hardResetOnLoad;
        worldCameraCapture_.reset();
        worldCameraDiagnosticLogged_ = missingWorldCameraLogged_ = false;
        pendingCameraMap = {};
        cameraBufferQualified_.store(false, std::memory_order_release);
        restoreDynamicResolutionIfOwned();
        qualificationProbeAttempts_.store(0, std::memory_order_relaxed);
        qualificationCaptureRequested_.store(true, std::memory_order_release);
        operational_.store(false, std::memory_order_release);
        clearRenderResources(hardResetOnLoad);
        if (hardResetOnLoad) {
            hardResets_.fetch_add(1, std::memory_order_relaxed);
        }
        qualificationProbes_ = {};
        firstColorComparisonQueued_.store(false, std::memory_order_relaxed);
        firstActiveColorComparisonQueued_.store(
            false,
            std::memory_order_relaxed);
        resetHistory_ = true;
        logging::info(
            "DLAA qualification reset applied on the render thread; output remains vanilla until the stereo resource contract is proven.");
    }

    void Runtime::onMapSucceeded(
        ID3D11DeviceContext* context,
        ID3D11Resource* resource,
        UINT subresource,
        D3D11_MAP mapType,
        const D3D11_MAPPED_SUBRESOURCE& mapped) noexcept
    {
        if (!context || context != context_.Get()) return;
        pendingCameraMap = {};
        if (!resource || !mapped.pData ||
            (mapType != D3D11_MAP_WRITE &&
                mapType != D3D11_MAP_WRITE_DISCARD &&
                mapType != D3D11_MAP_WRITE_NO_OVERWRITE)) {
            return;
        }
        D3D11_RESOURCE_DIMENSION dimension{};
        resource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) {
            return;
        }
        auto* buffer = static_cast<ID3D11Buffer*>(resource);
        D3D11_BUFFER_DESC description{};
        buffer->GetDesc(&description);
        // FO4VR allocates b12 as 0x550 bytes in the live renderer. The
        // shader-visible stereo camera contract occupies the verified leading
        // 0x4D0-byte prefix; accept a containing constant buffer and validate
        // the complete consumed prefix before publishing it.
        if ((description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
            description.ByteWidth < sizeof(Fo4VrFrameBuffer) ||
            description.ByteWidth >
                D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u) {
            return;
        }
        const auto* boundCamera = cameraBufferIdentity_.load(
            std::memory_order_acquire);
        cameraMapCandidates_.fetch_add(1, std::memory_order_relaxed);
        if (!boundCamera || resource != boundCamera) {
            return;
        }
        cameraIdentityMatches_.fetch_add(1, std::memory_order_relaxed);
        pendingCameraMap = {
            .resource = resource,
            .subresource = subresource,
            .data = mapped.pData,
            .byteWidth = description.ByteWidth,
            .mapType = mapType,
            .matchesBoundCamera = true,
            .candidate = true,
        };
    }

    void Runtime::onBeforeUnmap(
        ID3D11DeviceContext* context,
        ID3D11Resource* resource,
        UINT subresource) noexcept
    {
        if (!context || context != context_.Get()) return;
        if (!pendingCameraMap.candidate ||
            pendingCameraMap.resource != resource ||
            pendingCameraMap.subresource != subresource ||
            !pendingCameraMap.data) {
            return;
        }
        Fo4VrFrameBuffer candidate{};
        std::memcpy(
            &candidate,
            pendingCameraMap.data,
            sizeof(candidate));
        const auto pending = pendingCameraMap;
        pendingCameraMap = {};
        // Preserve the raw upload for diagnosis at the actual world draw.
        // Staging never publishes a camera: the same validation runs again
        // before a confirmed world snapshot can reach the upscaler.
        worldCameraCapture_.observe(static_cast<ID3D11Buffer*>(resource), candidate,
            postRenderCalls_.load(std::memory_order_relaxed));
        if (!validateCameraFrame(candidate)) {
            cameraValidationFailures_.fetch_add(1, std::memory_order_relaxed);
            if (!cameraBufferQualified_.load(std::memory_order_acquire) &&
                !firstCameraValidationFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "DLAA rejected the first containing camera-map candidate: resource={}, byteWidth={}, mapType={}, matchesBoundB12={}, shaderParameters50=({},{},{},{}), shaderParameters48=({},{},{},{}), finiteProjection={}, finiteCurrentUnjittered={}, finitePreviousUnjittered={}. Vanilla TAA remains active.",
                    static_cast<const void*>(pending.resource),
                    pending.byteWidth,
                    static_cast<std::uint32_t>(pending.mapType),
                    pending.matchesBoundCamera,
                    candidate.shaderParameters50.x,
                    candidate.shaderParameters50.y,
                    candidate.shaderParameters50.z,
                    candidate.shaderParameters50.w,
                    candidate.shaderParameters48.x,
                    candidate.shaderParameters48.y,
                    candidate.shaderParameters48.z,
                    candidate.shaderParameters48.w,
                    finiteMatrix(candidate.projection[0]) &&
                        finiteMatrix(candidate.projection[1]),
                    finiteMatrix(candidate.viewProjectionUnjittered[0]) &&
                        finiteMatrix(candidate.viewProjectionUnjittered[1]),
                    finiteMatrix(
                        candidate.previousViewProjectionUnjittered[0]) &&
                        finiteMatrix(
                            candidate.previousViewProjectionUnjittered[1]));
            }
            return;
        }
        mappedCameraFrames_.fetch_add(1, std::memory_order_relaxed);
    }

    void Runtime::onWorldCompositeDraw(ID3D11DeviceContext* context) noexcept
    {
        if (!context || context != context_.Get() || !qualifiedResources_) return;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
        if (!target) return;
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        target->GetDesc(&view);
        if (view.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
            view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice != 0) return;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        target->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture))) return;
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Width != qualifiedResources_.packedWidth || description.Height != qualifiedResources_.height ||
            description.ArraySize != 1 || description.SampleDesc.Count != 1) return;
        Microsoft::WRL::ComPtr<ID3D11Buffer> camera;
        context->PSGetConstantBuffers(12, 1, camera.GetAddressOf());
        const auto frame = postRenderCalls_.load(std::memory_order_relaxed);
        Fo4VrFrameBuffer candidate{};
        if (!worldCameraCapture_.confirm(camera.Get(), frame, candidate)) {
            if (!missingWorldCameraLogged_) {
                missingWorldCameraLogged_ = true;
                logging::info("DLAA world-camera diagnostic: no current upload matched world b12 (bound={}, tracked={}, frame={}).",
                    static_cast<const void*>(camera.Get()), static_cast<const void*>(cameraBuffer_.Get()), frame);
            }
            cameraBufferQualified_.store(false, std::memory_order_release);
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            return;
        }
        const auto validation = inspectCamera(candidate);
        if (!validation.worldValid()) {
            if (!worldCameraDiagnosticLogged_) {
                worldCameraDiagnosticLogged_ = true;
                logging::warn("DLAA rejected required world-camera data at the world draw (eyeFailures=[0x{:03X},0x{:03X}]); optional first-person inverses do not gate world rendering.",
                    validation.eyeFailures[0], validation.eyeFailures[1]);
            }
            cameraBufferQualified_.store(false, std::memory_order_release);
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            return;
        }
        cameraFrame_ = candidate;
        lastValidCameraPostCall_.store(frame, std::memory_order_release);
        cameraBufferQualified_.store(true, std::memory_order_release);
        if (!firstCameraQualifiedLogged_.exchange(true, std::memory_order_relaxed)) {
            const auto planes = render::projectionPlanes(cameraFrame_.projection[0]);
            logging::info("DLAA confirmed the stereo world camera ({}x{}, near={}, far={}, inverted={}, optionalFirstPersonAvailable=[{},{}]).",
                description.Width, description.Height, planes.nearPlane, planes.farPlane, planes.inverted,
                validation.firstPersonAvailable(0), validation.firstPersonAvailable(1));
        }
    }

    bool Runtime::validateCameraFrame(
        const Fo4VrFrameBuffer& frame) const noexcept
    {
        return inspectCamera(frame).worldValid();
    }

    void Runtime::onPreRender(
        void* dynamicResolutionManager,
        float* jitterX,
        float* jitterY) noexcept
    {
        diagnostics::processRenderDocCapture();
        const auto call = preRenderCalls_.fetch_add(
                              1,
                              std::memory_order_relaxed) +
            1;
        if (!dynamicResolutionManager) {
            return;
        }
        applyPendingSettings();
        applyQualificationSession();
        auto* bytes = static_cast<std::byte*>(
            dynamicResolutionManager);
        float widthRatio{};
        float heightRatio{};
        std::uint8_t active{};
        std::memcpy(
            &widthRatio,
            bytes + kDynamicResolutionWidthOffset,
            sizeof(widthRatio));
        std::memcpy(
            &heightRatio,
            bytes + kDynamicResolutionHeightOffset,
            sizeof(heightRatio));
        std::memcpy(
            &active,
            bytes + kDynamicResolutionActiveOffset,
            sizeof(active));
        if (call == 1) {
            logging::info(
                "DLAA verified pre-render boundary observed FO4VR dynamic-resolution state: widthRatio={}, heightRatio={}, active={} (manager offsets +0x1664/+0x1668/+0x1680).",
                widthRatio,
                heightRatio,
                active != 0);
        }

        const auto activeUpscaling =
            requested_.load(std::memory_order_acquire) &&
            postRenderCalls_.load(std::memory_order_relaxed) + 1 >= nextResourceAttemptPostCall_ &&
            evaluationGeometry_.valid &&
            evaluationGeometry_.inputWidth != 0 &&
            evaluationGeometry_.inputHeight != 0 && jitterX && jitterY;
        if (!activeUpscaling) {
            restoreDynamicResolutionIfOwned();
            return;
        }

        if (!dynamicResolutionOwned_) {
            savedDynamicResolutionWidth_ = widthRatio;
            savedDynamicResolutionHeight_ = heightRatio;
            savedDynamicResolutionActive_ = active;
            dynamicResolutionManager_ = dynamicResolutionManager;
            dynamicResolutionOwned_ = true;
        }
        ownedDynamicResolutionWidth_ = evaluationGeometry_.widthScale;
        ownedDynamicResolutionHeight_ = evaluationGeometry_.heightScale;
        // Preserve FO4VR's active-mode bit. Runtime evidence shows that the
        // engine normally keeps this true even at full 1.0 scale; changing it
        // alters more renderer state than DLAA owns. Only the scale is pinned.
        const auto ownedActive = savedDynamicResolutionActive_;
        std::memcpy(
            bytes + kDynamicResolutionWidthOffset,
            &ownedDynamicResolutionWidth_,
            sizeof(ownedDynamicResolutionWidth_));
        std::memcpy(
            bytes + kDynamicResolutionHeightOffset,
            &ownedDynamicResolutionHeight_,
            sizeof(ownedDynamicResolutionHeight_));
        std::memcpy(
            bytes + kDynamicResolutionActiveOffset,
            &ownedActive,
            sizeof(ownedActive));

        // Native DLAA and the hybrid mode keep FO4VR's own TAA jitter because
        // vanilla TAA remains the upstream temporal owner (and, for hybrid,
        // the published periphery). DLSS changes render resolution and owns a
        // scale-dependent sample sequence instead.
        if (!isDlssMode(settings_.mode)) {
            return;
        }
        const auto phaseCount = computeJitterPhaseCount(
            evaluationGeometry_.inputWidth,
            qualifiedResources_.eyeWidth);
        const auto sampleIndex = jitterPhase_ % phaseCount + 1u;
        ++jitterPhase_;
        const auto sampleX = halton(sampleIndex, 2u) - 0.5f;
        const auto sampleY = halton(sampleIndex, 3u) - 0.5f;
        // 141DAA370 (141DAA534..141DAA605) applies display/floor(render)
        // to these offsets when it builds each eye's jittered camera. Supply
        // display-eye units here: pre-scaling repeats the native conversion,
        // and using the packed width halves the horizontal sample pattern.
        *jitterX = -2.0f * sampleX / static_cast<float>(qualifiedResources_.eyeWidth);
        *jitterY = 2.0f * sampleY / static_cast<float>(qualifiedResources_.height);
    }

    void Runtime::onPostImageSpace() noexcept
    {
        const auto postCall = postRenderCalls_.fetch_add(
                                  1,
                                  std::memory_order_relaxed) +
            1;
        applyPendingSettings();
        applyQualificationSession();
        if (refreshRequested_.exchange(false, std::memory_order_acq_rel)) {
            hardResetTemporalState("explicit refresh");
        }
        const auto requestedNow = requested_.load(std::memory_order_acquire);
        if (requestedNow != renderThreadRequested_) {
            renderThreadRequested_ = requestedNow;
            hardResetTemporalState("enabled transition");
            logging::info(
                "DLAA render-thread transition applied: requested={}; {}.",
                requestedNow,
                requestedNow ?
                    "both eye histories will reset through one private proof before publication" :
                    "evaluation stopped and vanilla TAA owns the published image");
        }
        if (!deviceReady_.load(std::memory_order_acquire)) {
            operational_.store(false, std::memory_order_release);
            return;
        }

        // Qualification is passive resource discovery. Run it even while the
        // user-facing feature is disabled so the TAA resources can be found
        // as soon as the engine starts producing world frames.
        // Evaluation and publication remain gated by requestedNow below.
        if (!requestedNow) {
            operational_.store(false, std::memory_order_release);
        }
        if (!cameraBufferQualified_.load(std::memory_order_acquire)) {
            captureCameraBinding();
        }
        consumeQualificationProbes();
        if (!renderResourcesQualified_.load(std::memory_order_acquire) &&
            postCall % kQualificationProbeFrameCadence == 0) {
            qualificationCaptureRequested_.store(
                true,
                std::memory_order_release);
        }
        if (qualificationCaptureRequested_.exchange(
                false,
                std::memory_order_acq_rel)) {
            captureQualificationResources();
        }
        if (!requestedNow) {
            return;
        }
        if (!renderResourcesQualified_.load(std::memory_order_acquire)) {
            return;
        }
        if (postCall < nextResourceAttemptPostCall_) {
            return;
        }
        const auto geometryWasValid = evaluationGeometry_.valid;
        if (!updateEvaluationGeometry()) {
            operational_.store(false, std::memory_order_release);
            return;
        }
        // A changed DLSS scale must pass through the verified pre-render
        // owner before its first evaluation. Never evaluate a new mode
        // against the previous frame's render extent.
        if (!geometryWasValid) {
            return;
        }
        const auto lastCamera = lastValidCameraPostCall_.load(
            std::memory_order_acquire);
        if (lastCamera + 1 < postCall) {
            const auto wasOperational = operational_.exchange(
                false,
                std::memory_order_acq_rel);
            if (wasOperational && settings_.hardResetOnLoad) {
                hardResetTemporalState("camera upload interruption");
            } else {
                resetHistory_ = true;
            }
            if (!firstEvaluationFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "DLAA rejected a stale stereo camera upload at post boundary {} (last upload observed post {}); vanilla TAA resumes next frame.",
                    postCall,
                    lastCamera);
            }
            return;
        }
        (void)evaluateStereoFrame(postCall);
    }

    void Runtime::captureCameraBinding() noexcept
    {
        if (!context_) {
            return;
        }
        ID3D11Buffer* vertexBuffer{};
        ID3D11Buffer* pixelBuffer{};
        context_->VSGetConstantBuffers(12, 1, &vertexBuffer);
        context_->PSGetConstantBuffers(12, 1, &pixelBuffer);
        auto releaseVertex = Microsoft::WRL::ComPtr<ID3D11Buffer>{};
        auto releasePixel = Microsoft::WRL::ComPtr<ID3D11Buffer>{};
        releaseVertex.Attach(vertexBuffer);
        releasePixel.Attach(pixelBuffer);

        auto* candidate = pixelBuffer ? pixelBuffer : vertexBuffer;
        if (!candidate) {
            return;
        }
        D3D11_BUFFER_DESC description{};
        candidate->GetDesc(&description);
        if ((description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
            description.ByteWidth < sizeof(Fo4VrFrameBuffer) ||
            description.ByteWidth >
                D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u) {
            return;
        }
        if (cameraBufferIdentity_.load(std::memory_order_acquire) ==
            candidate) {
            return;
        }
        cameraBuffer_ = candidate;
        cameraBufferIdentity_.store(candidate, std::memory_order_release);
        std::array<char, 256> name{};
        logging::info(
            "DLAA observed shader camera b12: selectedStage={}, resource={}, VSb12={}, PSb12={}, name='{}', byteWidth={}, usage={}, bind=0x{:X}, cpuAccess=0x{:X}. Future Map candidates will be correlated by exact identity.",
            pixelBuffer ? "PS" : "VS",
            static_cast<const void*>(candidate),
            static_cast<const void*>(vertexBuffer),
            static_cast<const void*>(pixelBuffer),
            resourceName(candidate, name),
            description.ByteWidth,
            static_cast<std::uint32_t>(description.Usage),
            description.BindFlags,
            description.CPUAccessFlags);
    }

    void Runtime::captureQualificationResources() noexcept
    {
        if (!context_) {
            return;
        }
        const auto attempt = qualificationProbeAttempts_.fetch_add(
                                 1,
                                 std::memory_order_relaxed) +
            1;
        const auto diagnostic = attempt <= kDiagnosticProbeAttempts;
        std::array<ID3D11RenderTargetView*,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
        ID3D11DepthStencilView* depthView{};
        context_->OMGetRenderTargets(
            static_cast<UINT>(renderTargets.size()),
            renderTargets.data(),
            &depthView);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> outputColor;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> taaMask;
        D3D11_TEXTURE2D_DESC outputDescription{};
        D3D11_TEXTURE2D_DESC taaMaskDescription{};
        for (UINT slot = 0; slot < renderTargets.size(); ++slot) {
            auto* view = renderTargets[slot];
            if (!view) {
                continue;
            }
            ID3D11Resource* resource{};
            view->GetResource(&resource);
            if (diagnostic) logTexture("OM.RTV", slot, resource);
            if (slot == 0) {
                if (diagnostic) queueQualificationProbe("OM.RTV0", resource);
                (void)textureDescription(
                    resource,
                    taaMaskDescription,
                    taaMask);
            } else if (slot == 1) {
                if (diagnostic) queueQualificationProbe("OM.RTV1", resource);
                (void)textureDescription(
                    resource,
                    outputDescription,
                    outputColor);
            }
            if (resource) {
                resource->Release();
            }
            view->Release();
        }
        ID3D11Resource* depthTargetResource{};
        if (depthView) {
            ID3D11Resource* resource{};
            depthView->GetResource(&resource);
            depthTargetResource = resource;
            if (diagnostic) logTexture("OM.DSV", 0, resource);
        }

        std::array<ID3D11ShaderResourceView*, 32> resources{};
        context_->PSGetShaderResources(
            0,
            static_cast<UINT>(resources.size()),
            resources.data());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneColor;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> motionVectors;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSourceView;
        D3D11_TEXTURE2D_DESC sceneDescription{};
        D3D11_TEXTURE2D_DESC motionDescription{};
        D3D11_TEXTURE2D_DESC depthDescription{};
        for (UINT slot = 0; slot < resources.size(); ++slot) {
            auto* view = resources[slot];
            if (!view) {
                continue;
            }
            ID3D11Resource* resource{};
            view->GetResource(&resource);
            if (diagnostic) logTexture("PS.SRV", slot, resource);
            switch (slot) {
            case 0:
                if (diagnostic) queueQualificationProbe("PS.SRV0", resource);
                (void)textureDescription(
                    resource,
                    sceneDescription,
                    sceneColor);
                break;
            case 1:
                if (diagnostic) queueQualificationProbe("PS.SRV1", resource);
                break;
            case 2:
                if (diagnostic) queueQualificationProbe("PS.SRV2", resource);
                (void)textureDescription(
                    resource,
                    motionDescription,
                    motionVectors);
                break;
            case 3:
                if (diagnostic) queueQualificationProbe("PS.SRV3", resource, view);
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC description{};
                    view->GetDesc(&description);
                    if (description.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS &&
                        description.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
                        description.Texture2D.MostDetailedMip == 0 && description.Texture2D.MipLevels == 1)
                        depthSourceView = view;
                }
                (void)textureDescription(
                    resource,
                    depthDescription,
                    depth);
                break;
            case 4:
                if (diagnostic) queueQualificationProbe("PS.SRV4", resource);
                break;
            default:
                break;
            }
            if (resource) {
                resource->Release();
            }
            view->Release();
        }

        D3D11_VIEWPORT viewport{};
        UINT viewportCount = 1;
        context_->RSGetViewports(&viewportCount, &viewport);
        const auto packedWidth = sceneDescription.Width;
        const auto eyeWidth = packedWidth / 2u;
        const auto commonDescription = [packedWidth, &sceneDescription](
                                           const D3D11_TEXTURE2D_DESC& value) {
            return value.Width == packedWidth &&
                value.Height == sceneDescription.Height &&
                value.ArraySize == 1 && value.MipLevels == 1 &&
                value.SampleDesc.Count == 1;
        };
        const auto resourceTuple = sceneColor && taaMask &&
            outputColor && motionVectors && depth && depthSourceView &&
            packedWidth >= 2 && (packedWidth % 2u) == 0 &&
            sceneDescription.Height != 0 &&
            sceneDescription.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
            taaMaskDescription.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
            outputDescription.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
            motionDescription.Format == DXGI_FORMAT_R16G16_FLOAT &&
            depthDescription.Format == DXGI_FORMAT_R24G8_TYPELESS &&
            commonDescription(taaMaskDescription) &&
            commonDescription(outputDescription) &&
            commonDescription(motionDescription) &&
            commonDescription(depthDescription) &&
            (sceneDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            (taaMaskDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            (outputDescription.BindFlags & D3D11_BIND_RENDER_TARGET) != 0 &&
            (motionDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            (depthDescription.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            viewportCount == 1 && viewport.TopLeftX == 0.0f &&
            viewport.TopLeftY == 0.0f &&
            static_cast<UINT>(viewport.Width) == packedWidth &&
            static_cast<UINT>(viewport.Height) == sceneDescription.Height;
        const auto matchesPinnedTuple = resourceTuple &&
            qualifiedResources_.sceneColor.Get() == sceneColor.Get() &&
            qualifiedResources_.outputColor.Get() == outputColor.Get() &&
            qualifiedResources_.motionVectors.Get() == motionVectors.Get() &&
            qualifiedResources_.depth.Get() == depth.Get() &&
            qualifiedResources_.packedWidth == packedWidth &&
            qualifiedResources_.height == sceneDescription.Height;
        // This verified native post-TAA boundary supplies the stereo resources.
        // Discovery depends on their usable layout, not a shader bytecode pin
        // or the brightness of a handful of diagnostic pixels.
        const auto exactContract = resourceTuple;
        if (exactContract) {
            const auto changed = !matchesPinnedTuple;
            if (changed) {
                for (auto& eye : eyeResources_) {
                    eye = {};
                }
                compositionSurface_.Reset();
                compositionSurfaceView_.Reset();
                compositionEyeWidth_ = 0;
                compositionHeight_ = 0;
                evaluationGeometry_ = {};
                releaseStreamlineDlaaResources();
                resetHistory_ = true;
            }
            qualifiedResources_ = {
                .sceneColor = std::move(sceneColor),
                .outputColor = std::move(outputColor),
                .depth = std::move(depth),
                .depthView = std::move(depthSourceView),
                .motionVectors = std::move(motionVectors),
                .packedWidth = packedWidth,
                .eyeWidth = eyeWidth,
                .height = sceneDescription.Height,
            };
            resourceContractObserved_ = true;
            if (!firstResourceContractLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "DLAA pinned the exact FO4VR stereo TAA resource contract: packed={}x{}, eye={}x{}, scene=R11G11B10_FLOAT, mask=R8G8B8A8_UNORM OM.RTV0, output=R8G8B8A8_UNORM, motion=R16G16_FLOAT, depth=R24G8_TYPELESS.",
                    packedWidth,
                    sceneDescription.Height,
                    eyeWidth,
                    sceneDescription.Height);
            }
        } else if (!resourceTuple) {
            resourceContractObserved_ = false;
        }
        if (depthTargetResource) {
            depthTargetResource->Release();
        }
        if (depthView) {
            depthView->Release();
        }
        updateQualificationState();
        const auto streamline = streamlineSnapshot();
        if (diagnostic) logging::info(
            "DLAA resource discovery attempt {}: exactResources={}, viewportCount={}, viewport=({},{} {}x{} depth {}..{}), cameraReady={}, cameraBinding={}, cameraFrames={}, containingMaps={}, identityMatches={}, validationFailures={}, Streamline initialized={}, deviceBound={}, swapchainUpgraded={}, featureLoaded={}, supported={}, functionsBound={}. Vanilla TAA remains active until the private stereo proof succeeds.",
            attempt,
            exactContract,
            viewportCount,
            viewport.TopLeftX,
            viewport.TopLeftY,
            viewport.Width,
            viewport.Height,
            viewport.MinDepth,
            viewport.MaxDepth,
            cameraBufferQualified_.load(std::memory_order_acquire),
            cameraBufferIdentity_.load(std::memory_order_acquire) != nullptr,
            mappedCameraFrames_.load(std::memory_order_relaxed),
            cameraMapCandidates_.load(std::memory_order_relaxed),
            cameraIdentityMatches_.load(std::memory_order_relaxed),
            cameraValidationFailures_.load(std::memory_order_relaxed),
            streamline.initialized,
            streamline.deviceBound,
            streamline.swapChainUpgraded,
            streamline.featureLoaded,
            streamline.featureSupported,
            streamline.featureFunctionsBound);
    }

    void Runtime::queueQualificationProbe(
        const char* label,
        ID3D11Resource* resource,
        ID3D11ShaderResourceView* depthView) noexcept
    {
        if (!device_ || !context_ || !label || !resource) {
            return;
        }
        D3D11_RESOURCE_DIMENSION dimension{};
        resource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            return;
        }
        auto* source = static_cast<ID3D11Texture2D*>(resource);
        D3D11_TEXTURE2D_DESC sourceDescription{};
        source->GetDesc(&sourceDescription);
        const auto supported =
            sourceDescription.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
            sourceDescription.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            sourceDescription.Format == DXGI_FORMAT_R16G16_FLOAT ||
            sourceDescription.Format == DXGI_FORMAT_R24G8_TYPELESS;
        if (!supported || sourceDescription.Width < 2 ||
            sourceDescription.Height == 0 ||
            sourceDescription.ArraySize != 1 ||
            sourceDescription.SampleDesc.Count != 1) {
            return;
        }
        auto probe = std::find_if(
            qualificationProbes_.begin(),
            qualificationProbes_.end(),
            [label](const QualificationProbe& candidate) {
                return candidate.label &&
                    std::strcmp(candidate.label, label) == 0;
            });
        if (probe != qualificationProbes_.end() && probe->pending) {
            return;
        }
        if (probe == qualificationProbes_.end()) {
            probe = std::find_if(
                qualificationProbes_.begin(),
                qualificationProbes_.end(),
                [](const QualificationProbe& candidate) {
                    return candidate.label == nullptr;
                });
        }
        if (probe == qualificationProbes_.end()) {
            return;
        }

        const auto nativeDepth = sourceDescription.Format == DXGI_FORMAT_R24G8_TYPELESS;
        if (nativeDepth && (!depthView || !ensureDepthPreparationResources())) return;
        if (nativeDepth) {
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
            depthView->GetDesc(&viewDescription);
            if (viewDescription.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
                viewDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
                viewDescription.Texture2D.MostDetailedMip != 0) return;
        }
        const auto probeFormat = nativeDepth ? DXGI_FORMAT_R32_FLOAT : sourceDescription.Format;
        D3D11_TEXTURE2D_DESC probeDescription{};
        probeDescription.Width = kProbeSampleCount;
        probeDescription.Height = 1;
        probeDescription.MipLevels = 1;
        probeDescription.ArraySize = 1;
        probeDescription.Format = probeFormat;
        probeDescription.BindFlags = nativeDepth ? D3D11_BIND_UNORDERED_ACCESS : 0;
        probeDescription.SampleDesc.Count = 1;
        probeDescription.Usage = D3D11_USAGE_DEFAULT;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> transfer;
        if (FAILED(device_->CreateTexture2D(
                &probeDescription,
                nullptr,
                &transfer))) {
            return;
        }
        probeDescription.Usage = D3D11_USAGE_STAGING;
        probeDescription.BindFlags = 0;
        probeDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        if (FAILED(device_->CreateTexture2D(
                &probeDescription,
                nullptr,
                &readback))) {
            return;
        }

        if (nativeDepth) {
            // Cropped depth-stencil copies are rejected by D3D11. Gather the
            // same sixteen samples through the native depth SRV instead.
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
            if (FAILED(device_->CreateUnorderedAccessView(transfer.Get(), nullptr, &output))) return;
            render::ScopedComputeState restore(context_.Get(), {
                .firstShaderResource = 0, .shaderResourceCount = 1,
                .firstUnorderedAccess = 0, .unorderedAccessCount = 1,
                .firstConstantBuffer = 0, .constantBufferCount = 1});
            if (!restore.captured()) return;
            render::DepthPreparationConstants constants{};
            constants.width = kProbeSampleCount;
            constants.height = 1;
            constants.rawProbe = 1;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(depthPreparationConstants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
            if (!mapped.pData) {
                context_->Unmap(depthPreparationConstants_.Get(), 0);
                return;
            }
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context_->Unmap(depthPreparationConstants_.Get(), 0);
            auto* buffer = depthPreparationConstants_.Get();
            auto* uav = output.Get();
            context_->CSSetShader(depthPreparationShader_.Get(), nullptr, 0);
            context_->CSSetShaderResources(0, 1, &depthView);
            context_->CSSetConstantBuffers(0, 1, &buffer);
            context_->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            context_->Dispatch((kProbeSampleCount + 7) / 8, 1, 1);
            if (!restore.restore()) return;
        } else {
            const auto eyeWidth = sourceDescription.Width / 2;
            for (UINT index = 0; index < kProbeSampleCount; ++index) {
                const auto eye = index / (kProbeSampleCount / 2);
                const auto local = index % (kProbeSampleCount / 2);
                const auto column = local % 4;
                const auto row = local / 4;
                const auto x = (std::min)(
                    eye * eyeWidth +
                        static_cast<UINT>(
                            ((2ull * column + 1ull) * eyeWidth) / 8ull),
                    sourceDescription.Width - 1);
                const auto y = (std::min)(
                    static_cast<UINT>(
                        ((2ull * row + 1ull) * sourceDescription.Height) / 4ull),
                    sourceDescription.Height - 1);
                const D3D11_BOX sourceBox{ x, y, 0, x + 1, y + 1, 1 };
                context_->CopySubresourceRegion(
                    transfer.Get(),
                    0,
                    index,
                    0,
                    0,
                    source,
                    0,
                    &sourceBox);
            }
        }
        context_->CopyResource(readback.Get(), transfer.Get());
        *probe = {
            .label = label,
            .format = probeFormat,
            .transfer = std::move(transfer),
            .readback = std::move(readback),
            .polls = 0,
            .pending = true,
        };
    }

    void Runtime::consumeQualificationProbes() noexcept
    {
        if (!context_) {
            return;
        }
        for (auto& probe : qualificationProbes_) {
            if (!probe.pending || !probe.readback) {
                continue;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result = context_->Map(
                probe.readback.Get(),
                0,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &mapped);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
                if (++probe.polls < kMaximumProbePolls) {
                    continue;
                }
                logging::warn(
                    "DLAA qualification readback '{}' exceeded the bounded nonblocking poll budget.",
                    probe.label);
                probe.pending = false;
                continue;
            }
            if (FAILED(result) || !mapped.pData ||
                mapped.RowPitch < kProbeSampleCount * sizeof(std::uint32_t)) {
                logging::warn(
                    "DLAA qualification readback '{}' failed with HRESULT 0x{:08X}.",
                    probe.label,
                    static_cast<std::uint32_t>(result));
                probe.pending = false;
                continue;
            }

            const auto* pixels = static_cast<const std::uint32_t*>(
                mapped.pData);
            if (probe.format == DXGI_FORMAT_R11G11B10_FLOAT ||
                probe.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                float sumR{};
                float sumG{};
                float sumB{};
                float sumAlpha{};
                float peak{};
                std::uint32_t nonBlack{};
                for (UINT index = 0; index < kProbeSampleCount; ++index) {
                    float red{};
                    float green{};
                    float blue{};
                    if (probe.format == DXGI_FORMAT_R11G11B10_FLOAT) {
                        red = decodeUnsignedFloat(pixels[index] & 0x7FFu, 6);
                        green = decodeUnsignedFloat(
                            (pixels[index] >> 11) & 0x7FFu,
                            6);
                        blue = decodeUnsignedFloat(
                            (pixels[index] >> 22) & 0x3FFu,
                            5);
                        sumAlpha += 1.0f;
                    } else {
                        constexpr float scale = 1.0f / 255.0f;
                        red = static_cast<float>(pixels[index] & 0xFFu) * scale;
                        green = static_cast<float>(
                                    (pixels[index] >> 8) & 0xFFu) * scale;
                        blue = static_cast<float>(
                                   (pixels[index] >> 16) & 0xFFu) * scale;
                        sumAlpha += static_cast<float>(
                                        (pixels[index] >> 24) & 0xFFu) * scale;
                    }
                    sumR += red;
                    sumG += green;
                    sumB += blue;
                    peak = (std::max)(peak, (std::max)(red,
                        (std::max)(green, blue)));
                    nonBlack += (std::max)(red,
                                    (std::max)(green, blue)) > 0.000001f;
                }
                constexpr float inverseSamples =
                    1.0f / static_cast<float>(kProbeSampleCount);
                logging::info(
                    "DLAA qualification color readback '{}': format={}, average=({},{},{}), alpha={}, peak={}, nonBlack={}/{}.",
                    probe.label,
                    static_cast<std::uint32_t>(probe.format),
                    sumR * inverseSamples,
                    sumG * inverseSamples,
                    sumB * inverseSamples,
                    sumAlpha * inverseSamples,
                    peak,
                    nonBlack,
                    kProbeSampleCount);
            } else if (probe.format == DXGI_FORMAT_R16G16_FLOAT) {
                float sumAbsoluteX{};
                float sumAbsoluteY{};
                float maximumAbsolute{};
                std::uint32_t zero{};
                std::uint32_t finite{};
                std::uint32_t nonFinite{};
                for (UINT index = 0; index < kProbeSampleCount; ++index) {
                    const auto x = halfToFloat(static_cast<std::uint16_t>(
                        pixels[index] & 0xFFFFu));
                    const auto y = halfToFloat(static_cast<std::uint16_t>(
                        pixels[index] >> 16));
                    if (!std::isfinite(x) || !std::isfinite(y)) {
                        ++nonFinite;
                        continue;
                    }
                    const auto absoluteX = std::abs(x);
                    const auto absoluteY = std::abs(y);
                    sumAbsoluteX += absoluteX;
                    sumAbsoluteY += absoluteY;
                    maximumAbsolute = (std::max)(
                        maximumAbsolute,
                        (std::max)(absoluteX, absoluteY));
                    zero += absoluteX < 0.000001f &&
                        absoluteY < 0.000001f;
                    ++finite;
                }
                const auto inverseSamples = finite == 0 ?
                    0.0f :
                    1.0f / static_cast<float>(finite);
                logging::info(
                    "DLAA qualification motion readback '{}': averageAbs=({},{}), maximumAbs={}, zero={}, finite={}, nonFinite={} ({} samples).",
                    probe.label,
                    sumAbsoluteX * inverseSamples,
                    sumAbsoluteY * inverseSamples,
                    maximumAbsolute,
                    zero,
                    finite,
                    nonFinite,
                    kProbeSampleCount);
            } else {
                float minimum = 1.0f;
                float maximum{};
                std::uint32_t zero{};
                std::uint32_t farRange{};
                for (UINT index = 0; index < kProbeSampleCount; ++index) {
                    const auto depth = std::bit_cast<float>(pixels[index]);
                    minimum = (std::min)(minimum, depth);
                    maximum = (std::max)(maximum, depth);
                    zero += depth <= 0.000001f;
                    farRange += depth > 0.000001f && depth <= 0.01f;
                }
                logging::info(
                    "DLAA qualification depth readback '{}': range={}..{}, zeroSamples={}/{}, lowRangeSamples={}/{}.",
                    probe.label,
                    minimum,
                    maximum,
                    zero,
                    kProbeSampleCount,
                    farRange,
                    kProbeSampleCount);
            }
            context_->Unmap(probe.readback.Get(), 0);
            probe.pending = false;
        }
        updateQualificationState();
    }

    void Runtime::updateQualificationState() noexcept
    {
        const auto streamline = streamlineSnapshot();
        const auto ready = resourceContractObserved_ &&
            cameraBufferQualified_.load(std::memory_order_acquire) &&
            static_cast<bool>(qualifiedResources_) &&
            streamline.initialized && streamline.deviceBound &&
            streamline.swapChainUpgraded && streamline.featureLoaded &&
            streamline.featureSupported && streamline.featureFunctionsBound;
        renderResourcesQualified_.store(ready, std::memory_order_release);
    }

    void Runtime::applyPendingSettings() noexcept
    {
        const auto observedRevision = pendingSettingsRevision_.load(
            std::memory_order_acquire);
        if (observedRevision == renderThreadSettingsRevision_) {
            return;
        }
        Settings next{};
        std::uint64_t acceptedRevision{};
        {
            std::scoped_lock lock(settingsMutex_);
            next = pendingSettings_;
            acceptedRevision = pendingSettingsRevision_.load(
                std::memory_order_relaxed);
        }
        const auto wasEnabled = settings_.enabled;
        settings_ = sanitize(next);
        renderThreadSettingsRevision_ = acceptedRevision;
        renderThreadRequested_ = settings_.enabled;
        hardResetTemporalState("settings transition");
        if (!wasEnabled && settings_.enabled &&
            !renderResourcesQualified_.load(std::memory_order_acquire)) {
            // A disabled startup now qualifies passively. Re-arm the bounded
            // search as a recovery path when enable races that first proof or
            // a session invalidated the previously pinned resource tuple.
            qualificationProbes_ = {};
            qualificationProbeAttempts_.store(0, std::memory_order_relaxed);
            qualificationCaptureRequested_.store(
                true,
                std::memory_order_release);
            logging::info(
                "Upscaling re-armed passive TAA resource qualification for an off-to-on transition.");
        }
        logging::info(
            "Upscaling render-thread settings applied: revision={}, enabled={}, mode={}, modelPreset={}, motionVectorRepair={}, CAS={} at {}, center={}x{}, feather={}px, visualizeCenter={}.",
            acceptedRevision,
            settings_.enabled,
            modeName(settings_.mode),
            static_cast<std::uint32_t>(settings_.modelPreset),
            settings_.motionVectorRepair,
            settings_.sharpening,
            settings_.sharpness,
            settings_.centerWidth,
            settings_.centerHeight,
            settings_.centerFeatherPixels,
            settings_.visualizeCenter);
    }

    void Runtime::hardResetTemporalState(const char* reason) noexcept
    {
        depthPublicationLogged_ = false;
        operational_.store(false, std::memory_order_release);
        nextResourceAttemptPostCall_ = 0;
        restoreDynamicResolutionIfOwned();
        releaseStreamlineDlaaResources();
        for (auto& eye : eyeResources_) {
            eye = {};
        }
        evaluationGeometry_ = {};
        publishedInputWidth_.store(0, std::memory_order_relaxed);
        publishedInputHeight_.store(0, std::memory_order_relaxed);
        publishedOutputWidth_.store(0, std::memory_order_relaxed);
        publishedOutputHeight_.store(0, std::memory_order_relaxed);
        publishedCenterLeft_.store(0, std::memory_order_relaxed);
        publishedCenterTop_.store(0, std::memory_order_relaxed);
        resetHistory_ = true;
        jitterPhase_ = 0;
        firstEvaluationLogged_.store(false, std::memory_order_relaxed);
        firstEvaluationFailureLogged_.store(false, std::memory_order_relaxed);
        hardResets_.fetch_add(1, std::memory_order_relaxed);
        logging::info(
            "Upscaling hard-reset both Streamline eye viewports and private outputs (reason={}).",
            reason ? reason : "unknown");
    }

    bool Runtime::updateEvaluationGeometry() noexcept
    {
        if (evaluationGeometry_.valid) {
            return true;
        }
        if (!qualifiedResources_) {
            return false;
        }
        EvaluationGeometry next{};
        next.outputWidth = qualifiedResources_.eyeWidth;
        next.outputHeight = qualifiedResources_.height;
        if (settings_.mode == Mode::centerDlaa) {
            next.center = computeCenterRegion(
                qualifiedResources_.eyeWidth,
                qualifiedResources_.height,
                settings_.centerWidth,
                settings_.centerHeight);
            next.inputWidth = next.center.width;
            next.inputHeight = next.center.height;
            next.outputWidth = next.center.width;
            next.outputHeight = next.center.height;
        } else if (isDlssMode(settings_.mode)) {
            const auto optimal = queryStreamlineOptimalSettings(
                settings_.mode,
                qualifiedResources_.eyeWidth,
                qualifiedResources_.height);
            if (!optimal.valid || optimal.renderWidth == 0 ||
                optimal.renderHeight == 0 ||
                optimal.renderWidth > qualifiedResources_.eyeWidth ||
                optimal.renderHeight > qualifiedResources_.height) {
                if (!firstEvaluationFailureLogged_.exchange(true, std::memory_order_relaxed)) logging::warn(
                    "Upscaling is waiting for {} because Streamline did not return a valid per-eye input extent for {}x{} output.",
                    modeName(settings_.mode),
                    qualifiedResources_.eyeWidth,
                    qualifiedResources_.height);
                nextResourceAttemptPostCall_ = postRenderCalls_.load(std::memory_order_relaxed) + kQualificationProbeFrameCadence;
                return false;
            }
            next.inputWidth = optimal.renderWidth;
            next.inputHeight = optimal.renderHeight;
        } else {
            next.inputWidth = qualifiedResources_.eyeWidth;
            next.inputHeight = qualifiedResources_.height;
        }
        if (next.inputWidth == 0 || next.inputHeight == 0 ||
            next.outputWidth == 0 || next.outputHeight == 0) {
            return false;
        }
        if (settings_.mode == Mode::centerDlaa) {
            // The hybrid path deliberately keeps the engine's complete
            // vanilla-TAA image at native resolution. Only the neural
            // evaluation is cropped; the peripheral source is never scaled.
            next.widthScale = 1.0f;
            next.heightScale = 1.0f;
        } else {
            next.widthScale = static_cast<float>(next.inputWidth) /
                static_cast<float>(qualifiedResources_.eyeWidth);
            next.heightScale = static_cast<float>(next.inputHeight) /
                static_cast<float>(qualifiedResources_.height);
        }
        next.valid = true;
        evaluationGeometry_ = next;
        publishedInputWidth_.store(next.inputWidth, std::memory_order_relaxed);
        publishedInputHeight_.store(next.inputHeight, std::memory_order_relaxed);
        publishedOutputWidth_.store(next.outputWidth, std::memory_order_relaxed);
        publishedOutputHeight_.store(next.outputHeight, std::memory_order_relaxed);
        publishedCenterLeft_.store(next.center.left, std::memory_order_relaxed);
        publishedCenterTop_.store(next.center.top, std::memory_order_relaxed);
        logging::info(
            "Upscaling geometry prepared: mode={}, input={}x{} per eye, neuralOutput={}x{}, display={}x{} per eye, renderScale={}x{}, center=({},{} {}x{}).",
            modeName(settings_.mode),
            next.inputWidth,
            next.inputHeight,
            next.outputWidth,
            next.outputHeight,
            qualifiedResources_.eyeWidth,
            qualifiedResources_.height,
            next.widthScale,
            next.heightScale,
            next.center.left,
            next.center.top,
            next.center.width,
            next.center.height);
        return true;
    }

    bool Runtime::validateActiveViewport() noexcept
    {
        if (!context_ || !evaluationGeometry_.valid) {
            return false;
        }
        D3D11_VIEWPORT viewport{};
        UINT viewportCount = 1;
        context_->RSGetViewports(&viewportCount, &viewport);
        const auto expectedWidth = static_cast<float>(
            qualifiedResources_.packedWidth) *
            evaluationGeometry_.widthScale;
        const auto expectedHeight = static_cast<float>(
            qualifiedResources_.height) *
            evaluationGeometry_.heightScale;
        float managerWidth{};
        float managerHeight{};
        std::uint8_t dynamicRegion{};
        if (dynamicResolutionManager_) {
            const auto* bytes = static_cast<const std::byte*>(
                dynamicResolutionManager_);
            std::memcpy(
                &managerWidth,
                bytes + kDynamicResolutionWidthOffset,
                sizeof(managerWidth));
            std::memcpy(
                &managerHeight,
                bytes + kDynamicResolutionHeightOffset,
                sizeof(managerHeight));
            std::memcpy(
                &dynamicRegion,
                bytes + kDynamicResolutionRegionOffset,
                sizeof(dynamicRegion));
        }
        constexpr float scaleTolerance = 0.000001f;
        const auto renderScaleOwned = dynamicResolutionOwned_ &&
            dynamicResolutionManager_ &&
            std::abs(managerWidth - ownedDynamicResolutionWidth_) <=
                scaleTolerance &&
            std::abs(managerHeight - ownedDynamicResolutionHeight_) <=
                scaleTolerance;
        const auto dynamicRegionClosed = dynamicResolutionManager_ &&
            dynamicRegion == 0;
        const auto valid = isEvaluationViewportValid(
            settings_.mode,
            viewportCount,
            viewport.TopLeftX,
            viewport.TopLeftY,
            viewport.Width,
            viewport.Height,
            qualifiedResources_.packedWidth,
            qualifiedResources_.height,
            evaluationGeometry_.widthScale,
            evaluationGeometry_.heightScale,
            renderScaleOwned,
            dynamicRegionClosed);
        if (!valid && !firstEvaluationFailureLogged_.load(
                std::memory_order_relaxed)) {
            logging::warn(
                "Upscaling viewport contract mismatch for {}: observed count={}, origin=({},{}), extent={}x{}; expected packed render extent={}x{}, display extent={}x{}, renderScaleOwned={}, dynamicRegionClosed={} at the verified post-image-space boundary.",
                modeName(settings_.mode),
                viewportCount,
                viewport.TopLeftX,
                viewport.TopLeftY,
                viewport.Width,
                viewport.Height,
                expectedWidth,
                expectedHeight,
                qualifiedResources_.packedWidth,
                qualifiedResources_.height,
                renderScaleOwned,
                dynamicRegionClosed);
        }
        return valid;
    }

    bool Runtime::ensureDepthPreparationResources() noexcept
    {
        if (!device_) return false;
        if (!depthPreparationShader_ && FAILED(device_->CreateComputeShader(
                csah_upscaling_depth_cs, sizeof(csah_upscaling_depth_cs), nullptr,
                &depthPreparationShader_))) return false;
        if (!depthPreparationConstants_) {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(render::DepthPreparationConstants);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(&description, nullptr, &depthPreparationConstants_))) return false;
        }

        return true;
    }

    bool Runtime::ensureDepthPublicationResources() noexcept
    {
        HRESULT result = S_OK;
        if (!depthPublicationVertex_) result = device_->CreateVertexShader(
            csah_upscaling_depth_vs, sizeof(csah_upscaling_depth_vs), nullptr, &depthPublicationVertex_);
        if (SUCCEEDED(result) && !depthPublicationPixel_) result = device_->CreatePixelShader(
            csah_upscaling_depth_ps, sizeof(csah_upscaling_depth_ps), nullptr, &depthPublicationPixel_);
        if (SUCCEEDED(result) && !depthPublicationConstants_) {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = 16;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            result = device_->CreateBuffer(&description, nullptr, &depthPublicationConstants_);
        }
        if (SUCCEEDED(result) && !depthPublicationState_) {
            D3D11_DEPTH_STENCIL_DESC description{};
            description.DepthEnable = TRUE;
            description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            description.DepthFunc = D3D11_COMPARISON_ALWAYS;
            description.FrontFace = description.BackFace = {
                D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
            result = device_->CreateDepthStencilState(&description, &depthPublicationState_);
        }
        if (SUCCEEDED(result) && !depthPublicationRaster_) {
            D3D11_RASTERIZER_DESC description{};
            description.FillMode = D3D11_FILL_SOLID;
            description.CullMode = D3D11_CULL_NONE;
            description.DepthClipEnable = TRUE;
            result = device_->CreateRasterizerState(&description, &depthPublicationRaster_);
        }
        if (SUCCEEDED(result) && depthPublicationOwner_ != qualifiedResources_.depth) {
            D3D11_DEPTH_STENCIL_VIEW_DESC description{};
            description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            description.Flags = D3D11_DSV_READ_ONLY_STENCIL;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> target;
            result = device_->CreateDepthStencilView(qualifiedResources_.depth.Get(), &description, &target);
            if (SUCCEEDED(result)) {
                depthPublicationTarget_ = std::move(target);
                depthPublicationOwner_ = qualifiedResources_.depth;
            }
        }
        if (FAILED(result)) logging::error(
            "Upscaling display-depth resource creation failed (HRESULT=0x{:08X}); the render-thread allocation retry remains active.",
            static_cast<std::uint32_t>(result));
        return SUCCEEDED(result);
    }

    void Runtime::publishNativeDepth() noexcept
    {
        // Late native draws retain a DSV to this texture and render display-
        // resolution eye viewports. Updating the private NGX depth alone
        // leaves their occlusion in the reduced-resolution layout. Publish
        // raw native samples in place, retaining stencil and depth encoding.
        ScopedDepthAliases aliases(context_.Get(), qualifiedResources_.depth.Get());
        volumetric_lighting::StateScope restore(context_.Get());
        const std::array<std::uint32_t, 4> extent{
            qualifiedResources_.eyeWidth, qualifiedResources_.height,
            evaluationGeometry_.inputWidth, evaluationGeometry_.inputHeight};
        context_->UpdateSubresource(depthPublicationConstants_.Get(), 0, nullptr, extent.data(), 0, 0);
        context_->SetPredication(nullptr, FALSE);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->HSSetShader(nullptr, nullptr, 0);
        context_->DSSetShader(nullptr, nullptr, 0);
        context_->GSSetShader(nullptr, nullptr, 0);
        context_->VSSetShader(depthPublicationVertex_.Get(), nullptr, 0);
        context_->PSSetShader(depthPublicationPixel_.Get(), nullptr, 0);
        auto* constants = depthPublicationConstants_.Get();
        context_->PSSetConstantBuffers(0, 1, &constants);
        const std::array<ID3D11ShaderResourceView*, 2> sources{
            eyeResources_[0].nativeDepthView.Get(), eyeResources_[1].nativeDepthView.Get()};
        context_->PSSetShaderResources(0, 2, sources.data());
        context_->OMSetRenderTargets(0, nullptr, depthPublicationTarget_.Get());
        context_->OMSetDepthStencilState(depthPublicationState_.Get(), 0);
        context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        context_->RSSetState(depthPublicationRaster_.Get());
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(qualifiedResources_.packedWidth),
            static_cast<float>(qualifiedResources_.height), 0, 1};
        context_->RSSetViewports(1, &viewport);
        context_->Draw(3, 0);
        if (!std::exchange(depthPublicationLogged_, true)) logging::info(
            "Upscaling published native stereo scene depth for late draws: {}x{} input per eye -> {}x{} display per eye; original depth texture={}, native depth ranges retained, stencil unchanged.",
            evaluationGeometry_.inputWidth, evaluationGeometry_.inputHeight,
            qualifiedResources_.eyeWidth, qualifiedResources_.height,
            static_cast<const void*>(qualifiedResources_.depth.Get()));
    }

    bool Runtime::ensureEyeResources(
        std::uint32_t inputWidth,
        std::uint32_t inputHeight,
        std::uint32_t outputWidth,
        std::uint32_t outputHeight) noexcept
    {
        if (!device_ || !context_ || !qualifiedResources_ || inputWidth == 0 ||
            inputHeight == 0 || outputWidth == 0 || outputHeight == 0) {
            return false;
        }
        const auto publishDepth = isDlssMode(settings_.mode);
        if (publishDepth && !ensureDepthPublicationResources()) return false;
        const auto ready = [=](const EyeResources& eye) noexcept {
            return eye.colorInput && eye.depthInput && eye.depthInputView && eye.depthOutput &&
                (!publishDepth || (eye.nativeDepth && eye.nativeDepthView && eye.nativeDepthOutput)) &&
                eye.motionInput && eye.motionInputView && eye.motionHistory &&
                eye.motionHistoryView && eye.motionHistoryUav &&
                eye.motionOutput && eye.motionOutputView &&
                eye.motionOutputUav && eye.biasCurrentColor &&
                eye.biasCurrentColorUav && eye.outputColor && eye.outputView &&
                eye.inputWidth == inputWidth &&
                eye.inputHeight == inputHeight &&
                eye.outputWidth == outputWidth &&
                eye.outputHeight == outputHeight;
        };
        if (ready(eyeResources_[0]) && ready(eyeResources_[1])) {
            return true;
        }

        if (!ensureDepthPreparationResources()) return false;

        if (!motionRepairShader_ && FAILED(device_->CreateComputeShader(
                csah_upscaling_motion_repair_cs,
                sizeof(csah_upscaling_motion_repair_cs),
                nullptr,
                motionRepairShader_.GetAddressOf()))) {
            return false;
        }
        if (!motionRepairConstants_) {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(MotionRepairConstants);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(
                    &description,
                    nullptr,
                    motionRepairConstants_.GetAddressOf()))) {
                return false;
            }
        }
        if (!reactiveMaskShader_ && FAILED(device_->CreateComputeShader(
                csah_upscaling_reactive_mask_cs,
                sizeof(csah_upscaling_reactive_mask_cs),
                nullptr,
                reactiveMaskShader_.GetAddressOf()))) {
            return false;
        }
        if (!reactiveMaskConstants_) {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(ReactiveMaskConstants);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(
                    &description,
                    nullptr,
                    reactiveMaskConstants_.GetAddressOf()))) {
                return false;
            }
        }

        D3D11_TEXTURE2D_DESC colorTemplate{};
        D3D11_TEXTURE2D_DESC depthTemplate{};
        D3D11_TEXTURE2D_DESC motionTemplate{};
        qualifiedResources_.sceneColor->GetDesc(&colorTemplate);
        qualifiedResources_.depth->GetDesc(&depthTemplate);
        depthTemplate.Format = DXGI_FORMAT_R32_FLOAT;
        qualifiedResources_.motionVectors->GetDesc(&motionTemplate);

        const auto createInput = [this, inputWidth, inputHeight](
                                     const D3D11_TEXTURE2D_DESC& source,
                                     Microsoft::WRL::ComPtr<ID3D11Texture2D>&
                                         texture, bool writable = false) noexcept {
            auto description = source;
            description.Width = inputWidth;
            description.Height = inputHeight;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.SampleDesc = { 1, 0 };
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0);
            description.CPUAccessFlags = 0;
            description.MiscFlags = 0;
            return SUCCEEDED(device_->CreateTexture2D(
                &description,
                nullptr,
                texture.GetAddressOf()));
        };
        const auto createMotionSurface = [this, inputWidth, inputHeight](
                                             EyeResources& eye,
                                             bool history) noexcept {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = inputWidth;
            description.Height = inputHeight;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R16G16_FLOAT;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            auto& texture = history ? eye.motionHistory : eye.motionOutput;
            auto& view = history ? eye.motionHistoryView : eye.motionOutputView;
            auto& uav = history ? eye.motionHistoryUav : eye.motionOutputUav;
            return SUCCEEDED(device_->CreateTexture2D(
                       &description,
                       nullptr,
                       texture.GetAddressOf())) &&
                SUCCEEDED(device_->CreateShaderResourceView(
                    texture.Get(),
                    nullptr,
                    view.GetAddressOf())) &&
                SUCCEEDED(device_->CreateUnorderedAccessView(
                    texture.Get(),
                    nullptr,
                    uav.GetAddressOf()));
        };

        std::array<EyeResources, 2> created{};
        for (auto& eye : created) {
            if (!createInput(colorTemplate, eye.colorInput) ||
                !createInput(depthTemplate, eye.depthInput, true) ||
                !createInput(motionTemplate, eye.motionInput)) {
                return false;
            }
            if (publishDepth &&
                (!createInput(depthTemplate, eye.nativeDepth, true) ||
                 FAILED(device_->CreateShaderResourceView(eye.nativeDepth.Get(), nullptr, &eye.nativeDepthView)) ||
                 FAILED(device_->CreateUnorderedAccessView(eye.nativeDepth.Get(), nullptr, &eye.nativeDepthOutput)))) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDescription{};
            depthViewDescription.Format = DXGI_FORMAT_R32_FLOAT;
            depthViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthViewDescription.Texture2D.MipLevels = 1;
            if (FAILED(device_->CreateShaderResourceView(
                    eye.depthInput.Get(),
                    &depthViewDescription,
                    eye.depthInputView.GetAddressOf())) ||
                FAILED(device_->CreateUnorderedAccessView(eye.depthInput.Get(), nullptr, &eye.depthOutput)) ||
                FAILED(device_->CreateShaderResourceView(
                    eye.motionInput.Get(),
                    nullptr,
                    eye.motionInputView.GetAddressOf())) ||
                !createMotionSurface(eye, true) ||
                !createMotionSurface(eye, false)) {
                return false;
            }
            D3D11_TEXTURE2D_DESC biasDescription{};
            biasDescription.Width = inputWidth;
            biasDescription.Height = inputHeight;
            biasDescription.MipLevels = 1;
            biasDescription.ArraySize = 1;
            biasDescription.Format = DXGI_FORMAT_R8_UNORM;
            biasDescription.SampleDesc.Count = 1;
            biasDescription.Usage = D3D11_USAGE_DEFAULT;
            biasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(
                    &biasDescription,
                    nullptr,
                    eye.biasCurrentColor.GetAddressOf())) ||
                FAILED(device_->CreateUnorderedAccessView(
                    eye.biasCurrentColor.Get(),
                    nullptr,
                    eye.biasCurrentColorUav.GetAddressOf()))) {
                return false;
            }
            D3D11_TEXTURE2D_DESC description{};
            description.Width = outputWidth;
            description.Height = outputHeight;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    eye.outputColor.GetAddressOf())) ||
                FAILED(device_->CreateShaderResourceView(
                    eye.outputColor.Get(),
                    nullptr,
                    eye.outputView.GetAddressOf()))) {
                return false;
            }
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> outputView;
            if (FAILED(device_->CreateUnorderedAccessView(
                    eye.outputColor.Get(),
                    nullptr,
                    outputView.GetAddressOf()))) {
                return false;
            }
            constexpr std::array<float, 4> opaqueBlack{
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            context_->ClearUnorderedAccessViewFloat(
                outputView.Get(),
                opaqueBlack.data());
            constexpr std::array<float, 4> zeroMotion{};
            context_->ClearUnorderedAccessViewFloat(
                eye.motionHistoryUav.Get(),
                zeroMotion.data());
            context_->ClearUnorderedAccessViewFloat(
                eye.motionOutputUav.Get(),
                zeroMotion.data());
            context_->ClearUnorderedAccessViewFloat(
                eye.biasCurrentColorUav.Get(),
                zeroMotion.data());
            eye.inputWidth = inputWidth;
            eye.inputHeight = inputHeight;
            eye.outputWidth = outputWidth;
            eye.outputHeight = outputHeight;
        }
        render::nameResource(created[0].depthInput.Get(), "CSAH / Upscaling / Left world-projection depth");
        render::nameResource(created[1].depthInput.Get(), "CSAH / Upscaling / Right world-projection depth");
        eyeResources_ = std::move(created);
        resetHistory_ = true;
        logging::info(
            "Upscaling created symmetric zero-origin eye inputs: input={}x{}, output={}x{}, color/depth/motion/current-color-bias isolated per eye.",
            inputWidth,
            inputHeight,
            outputWidth,
            outputHeight);
        return true;
    }

    ID3D11ShaderResourceView* Runtime::resolveTaaMaskView(
        ID3D11Texture2D* currentTaaMask) noexcept
    {
        if (!device_ || !currentTaaMask || !qualifiedResources_) {
            return nullptr;
        }
        for (const auto& entry : taaMaskViewCache_) {
            if (entry.texture.Get() == currentTaaMask) {
                return entry.view.Get();
            }
        }

        D3D11_TEXTURE2D_DESC description{};
        currentTaaMask->GetDesc(&description);
        if (description.Width != qualifiedResources_.packedWidth ||
            description.Height != qualifiedResources_.height ||
            description.MipLevels != 1 || description.ArraySize != 1 ||
            description.SampleDesc.Count != 1 ||
            description.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
            (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device_->CreateShaderResourceView(
                currentTaaMask,
                nullptr,
                view.GetAddressOf()))) {
            return nullptr;
        }
        auto empty = std::find_if(
            taaMaskViewCache_.begin(),
            taaMaskViewCache_.end(),
            [](const TaaMaskViewCacheEntry& entry) noexcept {
                return !entry.texture;
            });
        auto* destination = empty != taaMaskViewCache_.end() ?
            &*empty :
            &taaMaskViewCache_[
                taaMaskViewCacheReplaceIndex_++ % taaMaskViewCache_.size()];
        destination->texture = currentTaaMask;
        destination->view = std::move(view);
        return destination->view.Get();
    }

    bool Runtime::prepareEyeInputs(
        ID3D11Texture2D* currentTaaMask) noexcept
    {
        if (!context_ || !qualifiedResources_ || !evaluationGeometry_.valid ||
            !depthPreparationShader_ || !depthPreparationConstants_ ||
            !motionRepairShader_ || !motionRepairConstants_ ||
            !reactiveMaskShader_ || !reactiveMaskConstants_) {
            return false;
        }
        const auto cameraValidation = inspectCamera(cameraFrame_);
        const auto sourceTop = settings_.mode == Mode::centerDlaa ?
            evaluationGeometry_.center.top : 0u;
        for (std::uint32_t eye = 0; eye < eyeResources_.size(); ++eye) {
            auto& destination = eyeResources_[eye];
            const auto sourceLeft = settings_.mode == Mode::centerDlaa ?
                eye * qualifiedResources_.eyeWidth +
                    evaluationGeometry_.center.left :
                eye * evaluationGeometry_.inputWidth;
            if (sourceLeft + evaluationGeometry_.inputWidth >
                    qualifiedResources_.packedWidth ||
                sourceTop + evaluationGeometry_.inputHeight >
                    qualifiedResources_.height) {
                return false;
            }
            const D3D11_BOX sourceRegion{
                .left = sourceLeft,
                .top = sourceTop,
                .front = 0,
                .right = sourceLeft + evaluationGeometry_.inputWidth,
                .bottom = sourceTop + evaluationGeometry_.inputHeight,
                .back = 1,
            };
            context_->CopySubresourceRegion(
                destination.colorInput.Get(),
                0,
                0,
                0,
                0,
                qualifiedResources_.sceneColor.Get(),
                0,
                &sourceRegion);
            const auto planes = render::projectionPlanes(cameraFrame_.projection[eye]);
            if (!planes.valid) return false;
            const auto firstPersonAvailable = cameraValidation.firstPersonAvailable(eye);
            render::DepthPreparationConstants depthConstants{
                .width = evaluationGeometry_.inputWidth,
                .height = evaluationGeometry_.inputHeight,
                .sourceLeft = sourceLeft,
                .sourceTop = sourceTop,
                .localUvScaleBias = settings_.mode == Mode::centerDlaa ? Float4{
                    1.0F / qualifiedResources_.eyeWidth, 1.0F / qualifiedResources_.height,
                    static_cast<float>(evaluationGeometry_.center.left) / qualifiedResources_.eyeWidth,
                    static_cast<float>(sourceTop) / qualifiedResources_.height} : Float4{
                    1.0F / evaluationGeometry_.inputWidth, 1.0F / evaluationGeometry_.inputHeight, 0, 0},
                .firstPersonToWorldZ = firstPersonAvailable ? render::rowThroughMatrix(cameraFrame_.projection[eye][2],
                    cameraFrame_.inverseFirstPersonProjection[eye]) : Float4{},
                .firstPersonToWorldW = firstPersonAvailable ? render::rowThroughMatrix(cameraFrame_.projection[eye][3],
                    cameraFrame_.inverseFirstPersonProjection[eye]) : Float4{},
                .farDepth = planes.inverted ? 0.0F : 1.0F,
                .firstPersonProjectionAvailable = firstPersonAvailable ? 1U : 0U,
                .nativeDepthOutputEnabled = destination.nativeDepthOutput ? 1U : 0U,
            };
            D3D11_MAPPED_SUBRESOURCE mappedDepth{};
            if (FAILED(context_->Map(depthPreparationConstants_.Get(), 0,
                    D3D11_MAP_WRITE_DISCARD, 0, &mappedDepth))) return false;
            if (!mappedDepth.pData) {
                context_->Unmap(depthPreparationConstants_.Get(), 0);
                return false;
            }
            std::memcpy(mappedDepth.pData, &depthConstants, sizeof(depthConstants));
            context_->Unmap(depthPreparationConstants_.Get(), 0);
            auto* depthBuffer = depthPreparationConstants_.Get();
            auto* depthSource = qualifiedResources_.depthView.Get();
            std::array<ID3D11UnorderedAccessView*, 2> depthOutputs{
                destination.depthOutput.Get(), destination.nativeDepthOutput.Get()};
            context_->CSSetShader(depthPreparationShader_.Get(), nullptr, 0);
            context_->CSSetConstantBuffers(0, 1, &depthBuffer);
            context_->CSSetShaderResources(0, 1, &depthSource);
            context_->CSSetUnorderedAccessViews(0, 2, depthOutputs.data(), nullptr);
            context_->Dispatch((depthConstants.width + 7) / 8, (depthConstants.height + 7) / 8, 1);
            depthSource = nullptr;
            depthOutputs.fill(nullptr);
            context_->CSSetShaderResources(0, 1, &depthSource);
            context_->CSSetUnorderedAccessViews(0, 2, depthOutputs.data(), nullptr);
            context_->CopySubresourceRegion(
                destination.motionInput.Get(),
                0,
                0,
                0,
                0,
                qualifiedResources_.motionVectors.Get(),
                0,
                &sourceRegion);
        }

        ID3D11Buffer* nullDepthConstants{};
        context_->CSSetConstantBuffers(0, 1, &nullDepthConstants);
        context_->CSSetShader(nullptr, nullptr, 0);

        if (isDlssMode(settings_.mode)) {
            auto* taaMaskView = resolveTaaMaskView(currentTaaMask);
            if (!taaMaskView) {
                return false;
            }
            ID3D11Buffer* constantBuffer = reactiveMaskConstants_.Get();
            context_->CSSetShader(reactiveMaskShader_.Get(), nullptr, 0);
            context_->CSSetConstantBuffers(0, 1, &constantBuffer);
            for (std::uint32_t eye = 0; eye < eyeResources_.size(); ++eye) {
                const auto sourceLeft = eye * evaluationGeometry_.inputWidth;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context_->Map(
                        reactiveMaskConstants_.Get(),
                        0,
                        D3D11_MAP_WRITE_DISCARD,
                        0,
                        &mapped)) ||
                    !mapped.pData) {
                    return false;
                }
                const ReactiveMaskConstants constants{
                    .width = evaluationGeometry_.inputWidth,
                    .height = evaluationGeometry_.inputHeight,
                    .sourceLeft = sourceLeft,
                    .sourceTop = sourceTop,
                };
                std::memcpy(mapped.pData, &constants, sizeof(constants));
                context_->Unmap(reactiveMaskConstants_.Get(), 0);

                ID3D11UnorderedAccessView* output =
                    eyeResources_[eye].biasCurrentColorUav.Get();
                context_->CSSetShaderResources(0, 1, &taaMaskView);
                context_->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
                context_->Dispatch(
                    (evaluationGeometry_.inputWidth + 7u) / 8u,
                    (evaluationGeometry_.inputHeight + 7u) / 8u,
                    1);
                ID3D11ShaderResourceView* nullSource{};
                ID3D11UnorderedAccessView* nullOutput{};
                context_->CSSetShaderResources(0, 1, &nullSource);
                context_->CSSetUnorderedAccessViews(
                    0,
                    1,
                    &nullOutput,
                    nullptr);
            }
            ID3D11Buffer* nullConstant{};
            context_->CSSetConstantBuffers(0, 1, &nullConstant);
            context_->CSSetShader(nullptr, nullptr, 0);
        }

        if (!settings_.motionVectorRepair) {
            return true;
        }
        ID3D11Buffer* constantBuffer = motionRepairConstants_.Get();
        context_->CSSetShader(motionRepairShader_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(0, 1, &constantBuffer);
        for (std::size_t eyeIndex = 0; eyeIndex < eyeResources_.size(); ++eyeIndex) {
            auto& eye = eyeResources_[eyeIndex];
            const auto planes = render::projectionPlanes(cameraFrame_.projection[eyeIndex]);
            if (!planes.valid) return false;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(motionRepairConstants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            if (!mapped.pData) {
                context_->Unmap(motionRepairConstants_.Get(), 0);
                return false;
            }
            const MotionRepairConstants constants{
                .width = evaluationGeometry_.inputWidth,
                .height = evaluationGeometry_.inputHeight,
                .nearPlane = planes.nearPlane,
                .farPlane = planes.farPlane,
                .resetHistory = resetHistory_ ? 1u : 0u,
                .historyWeight = 0.1f,
                .farDepthStart = 10240.0f,
                .depthInverted = planes.inverted ? 1u : 0u,
            };
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context_->Unmap(motionRepairConstants_.Get(), 0);
            std::array<ID3D11ShaderResourceView*, 3> sources{
                eye.motionInputView.Get(),
                eye.depthInputView.Get(),
                eye.motionHistoryView.Get(),
            };
            ID3D11UnorderedAccessView* output = eye.motionOutputUav.Get();
            context_->CSSetShaderResources(
                0,
                static_cast<UINT>(sources.size()),
                sources.data());
            context_->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context_->Dispatch(
                (evaluationGeometry_.inputWidth + 7u) / 8u,
                (evaluationGeometry_.inputHeight + 7u) / 8u,
                1);
            std::array<ID3D11ShaderResourceView*, 3> nullSources{};
            ID3D11UnorderedAccessView* nullOutput{};
            context_->CSSetShaderResources(
                0,
                static_cast<UINT>(nullSources.size()),
                nullSources.data());
            context_->CSSetUnorderedAccessViews(
                0,
                1,
                &nullOutput,
                nullptr);
        }
        ID3D11Buffer* nullConstant{};
        context_->CSSetConstantBuffers(0, 1, &nullConstant);
        context_->CSSetShader(nullptr, nullptr, 0);
        return true;
    }

    bool Runtime::ensureCompositorResources(
        std::uint32_t eyeWidth,
        std::uint32_t height) noexcept
    {
        if (!device_ || !qualifiedResources_ || eyeWidth == 0 || height == 0 ||
            eyeWidth > qualifiedResources_.eyeWidth ||
            height > qualifiedResources_.height) {
            return false;
        }
        if (!compositorShader_ && FAILED(device_->CreateComputeShader(
                csah_upscaling_compose_cs,
                sizeof(csah_upscaling_compose_cs),
                nullptr,
                compositorShader_.GetAddressOf()))) {
            return false;
        }
        if (!compositorConstants_) {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(ComposeConstants);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(
                    &description,
                    nullptr,
                    compositorConstants_.GetAddressOf()))) {
                return false;
            }
        }
        if (!compositionSurface_ || !compositionSurfaceView_ ||
            compositionEyeWidth_ != eyeWidth ||
            compositionHeight_ != height) {
            compositionSurface_.Reset();
            compositionSurfaceView_.Reset();
            compositionEyeWidth_ = 0;
            compositionHeight_ = 0;
            D3D11_TEXTURE2D_DESC description{};
            description.Width = eyeWidth * 2u;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    compositionSurface_.GetAddressOf())) ||
                FAILED(device_->CreateUnorderedAccessView(
                    compositionSurface_.Get(),
                    nullptr,
                    compositionSurfaceView_.GetAddressOf()))) {
                compositionSurface_.Reset();
                compositionSurfaceView_.Reset();
                return false;
            }
            compositionEyeWidth_ = eyeWidth;
            compositionHeight_ = height;
        }
        return compositorShader_ && compositorConstants_ &&
            compositionSurface_ && compositionSurfaceView_;
    }

    bool Runtime::composeStereo(
        std::uint32_t destinationLeft,
        std::uint32_t destinationTop,
        float featherPixels,
        float sharpness,
        bool visualize) noexcept
    {
        const auto eyeWidth = eyeResources_[0].outputWidth;
        const auto height = eyeResources_[0].outputHeight;
        if (!context_ || !eyeResources_[0].outputView ||
            !eyeResources_[1].outputView ||
            eyeResources_[1].outputWidth != eyeWidth ||
            eyeResources_[1].outputHeight != height ||
            !ensureCompositorResources(eyeWidth, height) ||
            destinationLeft + eyeResources_[0].outputWidth >
                qualifiedResources_.eyeWidth ||
            destinationTop + eyeResources_[0].outputHeight >
                qualifiedResources_.height) {
            return false;
        }
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            const D3D11_BOX sourceRegion{
                .left = eye * qualifiedResources_.eyeWidth + destinationLeft,
                .top = destinationTop,
                .front = 0,
                .right = eye * qualifiedResources_.eyeWidth +
                    destinationLeft + eyeWidth,
                .bottom = destinationTop + height,
                .back = 1,
            };
            context_->CopySubresourceRegion(
                compositionSurface_.Get(),
                0,
                eye * eyeWidth,
                0,
                0,
                qualifiedResources_.outputColor.Get(),
                0,
                &sourceRegion);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(
                compositorConstants_.Get(),
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &mapped)) ||
            !mapped.pData) {
            return false;
        }
        const ComposeConstants constants{
            .destinationLeft = 0,
            .destinationTop = 0,
            .width = eyeWidth,
            .height = height,
            .eyeStride = eyeWidth,
            .featherPixels = featherPixels,
            .sharpness = sharpness,
            .visualizeCenter = visualize ? 1u : 0u,
        };
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context_->Unmap(compositorConstants_.Get(), 0);

        std::array<ID3D11ShaderResourceView*, 2> sources{
            eyeResources_[0].outputView.Get(),
            eyeResources_[1].outputView.Get(),
        };
        ID3D11UnorderedAccessView* destination =
            compositionSurfaceView_.Get();
        ID3D11Buffer* constantBuffer = compositorConstants_.Get();
        context_->CSSetShader(compositorShader_.Get(), nullptr, 0);
        context_->CSSetShaderResources(
            0,
            static_cast<UINT>(sources.size()),
            sources.data());
        context_->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
        context_->CSSetConstantBuffers(0, 1, &constantBuffer);
        context_->Dispatch(
            (eyeResources_[0].outputWidth * 2u + 7u) / 8u,
            (eyeResources_[0].outputHeight + 7u) / 8u,
            1);
        std::array<ID3D11ShaderResourceView*, 2> nullSources{};
        ID3D11UnorderedAccessView* nullDestination{};
        ID3D11Buffer* nullConstant{};
        context_->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());
        context_->CSSetUnorderedAccessViews(
            0,
            1,
            &nullDestination,
            nullptr);
        context_->CSSetConstantBuffers(0, 1, &nullConstant);
        context_->CSSetShader(nullptr, nullptr, 0);
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            const D3D11_BOX sourceRegion{
                .left = eye * eyeWidth,
                .top = 0,
                .front = 0,
                .right = (eye + 1u) * eyeWidth,
                .bottom = height,
                .back = 1,
            };
            context_->CopySubresourceRegion(
                qualifiedResources_.outputColor.Get(),
                0,
                eye * qualifiedResources_.eyeWidth + destinationLeft,
                destinationTop,
                0,
                compositionSurface_.Get(),
                0,
                &sourceRegion);
        }
        return true;
    }

    bool Runtime::ensureGpuTimingQueries() noexcept
    {
        if (!device_) {
            return false;
        }
        if (gpuTimingSlots_[0].disjoint) {
            return true;
        }

        std::array<GpuTimingSlot, kGpuTimingSlotCount> created{};
        const D3D11_QUERY_DESC disjointDescription{
            .Query = D3D11_QUERY_TIMESTAMP_DISJOINT,
            .MiscFlags = 0,
        };
        const D3D11_QUERY_DESC timestampDescription{
            .Query = D3D11_QUERY_TIMESTAMP,
            .MiscFlags = 0,
        };
        for (auto& slot : created) {
            if (FAILED(device_->CreateQuery(
                    &disjointDescription,
                    slot.disjoint.GetAddressOf())) ||
                FAILED(device_->CreateQuery(
                    &timestampDescription,
                    slot.start.GetAddressOf())) ||
                FAILED(device_->CreateQuery(
                    &timestampDescription,
                    slot.afterLeft.GetAddressOf())) ||
                FAILED(device_->CreateQuery(
                    &timestampDescription,
                    slot.afterRight.GetAddressOf())) ||
                FAILED(device_->CreateQuery(
                    &timestampDescription,
                    slot.afterCommit.GetAddressOf()))) {
                logging::warn(
                    "DLAA could not allocate the bounded nonblocking GPU timing ring; rendering remains active without timing telemetry.");
                return false;
            }
        }
        gpuTimingSlots_ = std::move(created);
        return true;
    }

    void Runtime::consumeGpuTimingQueries() noexcept
    {
        if (!context_) {
            return;
        }
        for (auto& slot : gpuTimingSlots_) {
            if (!slot.pending) {
                continue;
            }
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            const auto disjointResult = context_->GetData(
                slot.disjoint.Get(),
                &disjoint,
                sizeof(disjoint),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (disjointResult == S_FALSE) {
                continue;
            }
            if (FAILED(disjointResult) || disjoint.Disjoint ||
                disjoint.Frequency == 0) {
                slot.pending = false;
                continue;
            }
            std::array<std::uint64_t, 4> timestamps{};
            const std::array<ID3D11Query*, 4> queries{
                slot.start.Get(),
                slot.afterLeft.Get(),
                slot.afterRight.Get(),
                slot.afterCommit.Get(),
            };
            bool ready = true;
            for (std::size_t index = 0; index < queries.size(); ++index) {
                const auto result = context_->GetData(
                    queries[index],
                    &timestamps[index],
                    sizeof(timestamps[index]),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (result != S_OK) {
                    ready = false;
                    break;
                }
            }
            if (!ready) {
                continue;
            }
            slot.pending = false;
            if (timestamps[0] > timestamps[1] ||
                timestamps[1] > timestamps[2] ||
                timestamps[2] > timestamps[3]) {
                continue;
            }
            const auto milliseconds = [frequency = disjoint.Frequency](
                                          std::uint64_t begin,
                                          std::uint64_t end) noexcept {
                return static_cast<double>(end - begin) * 1000.0 /
                    static_cast<double>(frequency);
            };
            gpuLeftMilliseconds_ += milliseconds(
                timestamps[0],
                timestamps[1]);
            gpuRightMilliseconds_ += milliseconds(
                timestamps[1],
                timestamps[2]);
            gpuCommitMilliseconds_ += milliseconds(
                timestamps[2],
                timestamps[3]);
            gpuTotalMilliseconds_ += milliseconds(
                timestamps[0],
                timestamps[3]);
            ++gpuTimingSamples_;
        }
    }

    Runtime::GpuTimingSlot* Runtime::beginGpuTiming() noexcept
    {
        consumeGpuTimingQueries();
        if (!settings_.verboseDiagnostics &&
            !render::GpuTimingProfiler::groupRequested(render::GpuTimingProfiler::Group::General)) return nullptr;
        if (!context_ || !ensureGpuTimingQueries()) {
            return nullptr;
        }
        auto& slot = gpuTimingSlots_[
            gpuTimingWriteIndex_++ % gpuTimingSlots_.size()];
        if (slot.pending) {
            return nullptr;
        }
        context_->Begin(slot.disjoint.Get());
        context_->End(slot.start.Get());
        slot.pending = true;
        return &slot;
    }

    void Runtime::recordCpuTiming(
        double captureMicroseconds,
        double evaluateMicroseconds,
        double commitMicroseconds,
        double restoreMicroseconds) noexcept
    {
        cpuCaptureMicroseconds_ += captureMicroseconds;
        cpuEvaluateMicroseconds_ += evaluateMicroseconds;
        cpuCommitMicroseconds_ += commitMicroseconds;
        cpuRestoreMicroseconds_ += restoreMicroseconds;
        ++cpuTimingSamples_;
    }

    void Runtime::logPerformanceTimingsIfReady() noexcept
    {
        const auto requiredSamples = settings_.verboseDiagnostics ?
            kPerformanceTimingReportSamples / 4u :
            kPerformanceTimingReportSamples;
        if (gpuTimingSamples_ < requiredSamples ||
            cpuTimingSamples_ == 0) {
            return;
        }
        const auto gpuDivisor = static_cast<double>(gpuTimingSamples_);
        const auto cpuDivisor = static_cast<double>(cpuTimingSamples_);
        const auto leftAverage = gpuLeftMilliseconds_ / gpuDivisor;
        const auto rightAverage = gpuRightMilliseconds_ / gpuDivisor;
        const auto commitAverage = gpuCommitMilliseconds_ / gpuDivisor;
        const auto totalAverage = gpuTotalMilliseconds_ / gpuDivisor;
        publishedGpuLeftMilliseconds_.store(
            leftAverage,
            std::memory_order_relaxed);
        publishedGpuRightMilliseconds_.store(
            rightAverage,
            std::memory_order_relaxed);
        publishedGpuCommitMilliseconds_.store(
            commitAverage,
            std::memory_order_relaxed);
        publishedGpuTotalMilliseconds_.store(
            totalAverage,
            std::memory_order_relaxed);
        logging::info(
            "Upscaling performance timing over {} completed GPU frames: GPU left={:.3f} ms, right={:.3f} ms, commit={:.3f} ms, total={:.3f} ms; CPU state-capture={:.3f} ms, Streamline-submit={:.3f} ms, commit-submit={:.3f} ms, state-restore={:.3f} ms over {} submissions. Timings use timestamp queries with DONOTFLUSH.",
            gpuTimingSamples_,
            leftAverage,
            rightAverage,
            commitAverage,
            totalAverage,
            cpuCaptureMicroseconds_ / cpuDivisor / 1000.0,
            cpuEvaluateMicroseconds_ / cpuDivisor / 1000.0,
            cpuCommitMicroseconds_ / cpuDivisor / 1000.0,
            cpuRestoreMicroseconds_ / cpuDivisor / 1000.0,
            cpuTimingSamples_);
        gpuTimingSamples_ = 0;
        gpuLeftMilliseconds_ = 0.0;
        gpuRightMilliseconds_ = 0.0;
        gpuCommitMilliseconds_ = 0.0;
        gpuTotalMilliseconds_ = 0.0;
        cpuTimingSamples_ = 0;
        cpuCaptureMicroseconds_ = 0.0;
        cpuEvaluateMicroseconds_ = 0.0;
        cpuCommitMicroseconds_ = 0.0;
        cpuRestoreMicroseconds_ = 0.0;
    }

    bool Runtime::evaluateStereoFrame(std::uint64_t postCall) noexcept
    {
        const auto wasOperational = operational_.load(
            std::memory_order_acquire);
        if (!context_ || !evaluationGeometry_.valid) {
            return false;
        }
        const auto viewportValid = validateActiveViewport();
        const auto outputsReady = viewportValid && ensureEyeResources(
            evaluationGeometry_.inputWidth,
            evaluationGeometry_.inputHeight,
            evaluationGeometry_.outputWidth,
            evaluationGeometry_.outputHeight);
        if (!viewportValid || !outputsReady) {
            operational_.store(false, std::memory_order_release);
            // A menu/resize boundary can temporarily leave a different viewport.
            // Retain the geometry so the next pre-render can restore its scale.
            // Only allocation failures need a bounded retry delay.
            if (viewportValid && !outputsReady) {
                nextResourceAttemptPostCall_ = postCall + kQualificationProbeFrameCadence;
                restoreDynamicResolutionIfOwned();
            }
            resetHistory_ = true;
            stereoEvaluationFailures_.fetch_add(1, std::memory_order_relaxed);
            if (!firstEvaluationFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Upscaling skipped this frame for mode {} (activeViewport={}, privateOutputs={}); evaluation will resume automatically when ready.",
                    modeName(settings_.mode),
                    viewportValid,
                    outputsReady);
            }
            return false;
        }

        StreamlineDlaaFrame frame{};
        frame.context = context_.Get();
        frame.frameIndex = streamlineFrameIndex_++;
        frame.inputWidth = evaluationGeometry_.inputWidth;
        frame.inputHeight = evaluationGeometry_.inputHeight;
        frame.outputWidth = evaluationGeometry_.outputWidth;
        frame.outputHeight = evaluationGeometry_.outputHeight;
        frame.mode = settings_.mode;
        frame.modelPreset = settings_.modelPreset;

        const auto inverseView = transposeForStreamline(
            cameraFrame_.inverseView);
        const std::array<float, 3> cameraRight{
            inverseView.values[0],
            inverseView.values[1],
            inverseView.values[2],
        };
        const std::array<float, 3> cameraUp{
            inverseView.values[4],
            inverseView.values[5],
            inverseView.values[6],
        };
        const std::array<float, 3> cameraForward{
            inverseView.values[8],
            inverseView.values[9],
            inverseView.values[10],
        };
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            // b12 uses column-vector matrices. The native camera upload and
            // captured stereo fixture establish P = VP * inverse(V).
            const auto unjitteredProjection = multiply(
                cameraFrame_.viewProjectionUnjittered[eye], cameraFrame_.inverseView);
            const auto crop = [&](const Matrix4& matrix) {
                if (settings_.mode != Mode::centerDlaa) return matrix;
                return render::cropClipMatrix(matrix,
                    static_cast<float>(evaluationGeometry_.center.left) / qualifiedResources_.eyeWidth,
                    static_cast<float>(evaluationGeometry_.center.top) / qualifiedResources_.height,
                    static_cast<float>(evaluationGeometry_.inputWidth) / qualifiedResources_.eyeWidth,
                    static_cast<float>(evaluationGeometry_.inputHeight) / qualifiedResources_.height);
            };
            const auto unjitteredEyeProjection = crop(unjitteredProjection);
            const auto jitteredEyeProjection = crop(cameraFrame_.projection[eye]);
            // Report the jitter actually present in this eye's world camera,
            // in input pixels. This also handles native DLAA and center crops
            // without guessing the engine's scale or stereo packing factor.
            // StreamlineBackend negates these values to screen-space offsets.
            const auto jitterPixelsX = -(jitteredEyeProjection[0].z - unjitteredEyeProjection[0].z) *
                static_cast<float>(evaluationGeometry_.inputWidth) * 0.5f;
            const auto jitterPixelsY = (jitteredEyeProjection[1].z - unjitteredEyeProjection[1].z) *
                static_cast<float>(evaluationGeometry_.inputHeight) * 0.5f;
            const auto projection = transposeForStreamline(unjitteredEyeProjection);
            const auto verticalScale = std::abs(projection.values[5]);
            if (!std::isfinite(verticalScale) || verticalScale < 0.001f) {
                operational_.store(false, std::memory_order_release);
                resetHistory_ = true;
                stereoEvaluationFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return false;
            }
            const auto& position = cameraFrame_.currentEyePosition[eye];
            const auto planes = render::projectionPlanes(cameraFrame_.projection[eye]);
            if (!planes.valid) return false;
            frame.constants[eye] = {
                .cameraViewToClip = projection,
                .currentViewProjection = transposeForStreamline(
                    crop(cameraFrame_.viewProjectionUnjittered[eye])),
                .previousViewProjection = transposeForStreamline(
                    crop(cameraFrame_.previousViewProjectionUnjittered[eye])),
                .cameraRight = cameraRight,
                .cameraUp = cameraUp,
                .cameraForward = cameraForward,
                .cameraPosition = { position.x, position.y, position.z },
                .cameraNear = planes.nearPlane,
                .cameraFar = planes.farPlane,
                .cameraFovRadians = 2.0f * std::atan(1.0f / verticalScale),
                .cameraAspectRatio =
                    settings_.mode == Mode::centerDlaa ?
                        static_cast<float>(evaluationGeometry_.inputWidth) / evaluationGeometry_.inputHeight :
                        static_cast<float>(qualifiedResources_.eyeWidth) / qualifiedResources_.height,
                .jitterPixelsX = jitterPixelsX,
                .jitterPixelsY = jitterPixelsY,
                .motionVectorScaleX =
                    settings_.mode == Mode::centerDlaa ?
                        static_cast<float>(qualifiedResources_.eyeWidth) /
                            static_cast<float>(evaluationGeometry_.inputWidth) :
                        1.0f,
                .motionVectorScaleY =
                    settings_.mode == Mode::centerDlaa ?
                        static_cast<float>(qualifiedResources_.height) /
                            static_cast<float>(evaluationGeometry_.inputHeight) :
                        1.0f,
                .depthInverted = planes.inverted,
                .reset = resetHistory_,
            };
        }

        const bool profiling = settings_.verboseDiagnostics ||
            render::GpuTimingProfiler::groupRequested(render::GpuTimingProfiler::Group::General);
        const auto timingNow = [profiling]() noexcept {
            return profiling ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        };
        const auto captureStart = timingNow();
        render::ScopedComputeState computeState(
            context_.Get(),
            {
                .firstShaderResource = 0,
                .shaderResourceCount =
                    D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = D3D11_PS_CS_UAV_REGISTER_COUNT,
                .firstSampler = 0,
                .samplerCount = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                .firstConstantBuffer = 0,
                .constantBufferCount =
                    D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
            });
        ScopedImageSpaceBindings imageState(context_.Get());
        const auto captureEnd = timingNow();
        if (!computeState.captured() || !imageState.captured()) {
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            stereoEvaluationFailures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> currentTaaMask;
        if (isDlssMode(settings_.mode) &&
            !imageState.renderTargetTexture(0, currentTaaMask)) {
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            stereoEvaluationFailures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        auto* gpuTiming = beginGpuTiming();
        if (gpuTiming) {
            frame.timingAfterEye = {
                gpuTiming->afterLeft.Get(),
                gpuTiming->afterRight.Get(),
            };
        }
        if (!prepareEyeInputs(currentTaaMask.Get())) {
            if (gpuTiming) {
                context_->End(gpuTiming->afterLeft.Get());
                context_->End(gpuTiming->afterRight.Get());
                context_->End(gpuTiming->afterCommit.Get());
                context_->End(gpuTiming->disjoint.Get());
            }
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            stereoEvaluationFailures_.fetch_add(1, std::memory_order_relaxed);
            if (!firstEvaluationFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Upscaling failed closed while isolating zero-origin stereo color/depth/motion inputs; vanilla TAA remains published.");
            }
            return false;
        }
        for (std::uint32_t eye = 0; eye < frame.resources.size(); ++eye) {
            frame.resources[eye] = {
                .colorInput = eyeResources_[eye].colorInput.Get(),
                .colorOutput = eyeResources_[eye].outputColor.Get(),
                .depth = eyeResources_[eye].depthInput.Get(),
                .motionVectors = settings_.motionVectorRepair ?
                    eyeResources_[eye].motionOutput.Get() :
                    eyeResources_[eye].motionInput.Get(),
                .biasCurrentColor = isDlssMode(settings_.mode) ?
                    eyeResources_[eye].biasCurrentColor.Get() : nullptr,
                .inputLeft = 0,
                .inputTop = 0,
            };
        }
        const auto evaluateStart = timingNow();
        const auto evaluated = evaluateStreamlineDlaa(frame);
        const auto evaluateEnd = timingNow();
        if (settings_.motionVectorRepair) {
            for (auto& eye : eyeResources_) {
                std::swap(eye.motionHistory, eye.motionOutput);
                std::swap(eye.motionHistoryView, eye.motionOutputView);
                std::swap(eye.motionHistoryUav, eye.motionOutputUav);
            }
        }
        stereoEvaluations_.fetch_add(1, std::memory_order_relaxed);
        if (evaluated && !wasOperational &&
            !firstColorComparisonQueued_.exchange(
                true,
                std::memory_order_relaxed)) {
            queueQualificationProbe(
                "DLAA.Compare.InputPacked",
                qualifiedResources_.sceneColor.Get());
            queueQualificationProbe(
                "DLAA.Compare.VanillaPacked",
                qualifiedResources_.outputColor.Get());
            queueQualificationProbe(
                "DLAA.Compare.OutputLeft",
                eyeResources_[0].outputColor.Get());
            queueQualificationProbe(
                "DLAA.Compare.OutputRight",
                eyeResources_[1].outputColor.Get());
        }
        const auto commitStart = timingNow();
        bool publicationSucceeded{ true };
        if (evaluated && wasOperational) {
            const auto compose = settings_.mode == Mode::centerDlaa ||
                settings_.sharpening || settings_.visualizeCenter;
            if (compose) {
                publicationSucceeded = composeStereo(
                    settings_.mode == Mode::centerDlaa ?
                        evaluationGeometry_.center.left : 0u,
                    settings_.mode == Mode::centerDlaa ?
                        evaluationGeometry_.center.top : 0u,
                    settings_.mode == Mode::centerDlaa ?
                        settings_.centerFeatherPixels : 0.0f,
                    settings_.sharpening ? settings_.sharpness : 0.0f,
                    settings_.visualizeCenter);
            } else {
                for (std::uint32_t eye = 0; eye < 2; ++eye) {
                    context_->CopySubresourceRegion(
                        qualifiedResources_.outputColor.Get(),
                        0,
                        eye * qualifiedResources_.eyeWidth,
                        0,
                        0,
                        eyeResources_[eye].outputColor.Get(),
                        0,
                        nullptr);
                }
            }
            if (publicationSucceeded && isDlssMode(settings_.mode)) publishNativeDepth();
            if (!firstActiveColorComparisonQueued_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                queueQualificationProbe(
                    "DLAA.Active.InputPacked",
                    qualifiedResources_.sceneColor.Get());
                queueQualificationProbe(
                    "DLAA.Active.CommittedPacked",
                    qualifiedResources_.outputColor.Get());
                queueQualificationProbe(
                    "DLAA.Active.OutputLeft",
                    eyeResources_[0].outputColor.Get());
                queueQualificationProbe(
                    "DLAA.Active.OutputRight",
                    eyeResources_[1].outputColor.Get());
            }
        }
        const auto commitEnd = timingNow();
        if (gpuTiming) {
            context_->End(gpuTiming->afterCommit.Get());
            context_->End(gpuTiming->disjoint.Get());
        }
        const auto restoreStart = timingNow();
        const auto imageRestored = imageState.restore();
        const auto computeRestored = computeState.restore();
        const auto restoreEnd = timingNow();
        const auto microseconds = [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::micro>(end - begin)
                .count();
        };
        if (profiling) {
            recordCpuTiming(
                microseconds(captureStart, captureEnd),
                microseconds(evaluateStart, evaluateEnd),
                microseconds(commitStart, commitEnd),
                microseconds(restoreStart, restoreEnd));
            consumeGpuTimingQueries();
            logPerformanceTimingsIfReady();
        }
        if (!evaluated || !publicationSucceeded || !imageRestored ||
            !computeRestored) {
            operational_.store(false, std::memory_order_release);
            resetHistory_ = true;
            stereoEvaluationFailures_.fetch_add(1, std::memory_order_relaxed);
            releaseStreamlineDlaaResources();
            if (!firstEvaluationFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Upscaling stereo transaction failed closed at post boundary {} (evaluate={}, publish={}, imageRestore={}, computeRestore={}); the next frame returns to the vanilla-TAA path.",
                    postCall,
                    evaluated,
                    publicationSucceeded,
                    imageRestored,
                    computeRestored);
            }
            return false;
        }

        resetHistory_ = false;
        if (wasOperational) {
            committedFrames_.fetch_add(1, std::memory_order_relaxed);
        } else {
            operational_.store(true, std::memory_order_release);
            if (!firstEvaluationLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Upscaling completed the private two-eye proof for {} at input {}x{} and output {}x{} per eye; neither proof output was published. Vanilla TAA remains the required upstream frame-preparation path and the hybrid peripheral source.",
                    modeName(settings_.mode),
                    evaluationGeometry_.inputWidth,
                    evaluationGeometry_.inputHeight,
                    evaluationGeometry_.outputWidth,
                    evaluationGeometry_.outputHeight);
                logging::info("Upscaling stereo camera jitter for {} in input pixels: left=({},{}), right=({},{}); derived from the confirmed per-eye projection, scale={}x{}.",
                    modeName(settings_.mode), frame.constants[0].jitterPixelsX, frame.constants[0].jitterPixelsY,
                    frame.constants[1].jitterPixelsX, frame.constants[1].jitterPixelsY,
                    evaluationGeometry_.widthScale, evaluationGeometry_.heightScale);
            }
        }
        return true;
    }

    void Runtime::clearRenderResources(bool releaseStreamline) noexcept
    {
        operational_.store(false, std::memory_order_release);
        nextResourceAttemptPostCall_ = 0;
        renderResourcesQualified_.store(false, std::memory_order_release);
        if (releaseStreamline) {
            releaseStreamlineDlaaResources();
        }
        qualifiedResources_ = {};
        depthPublicationTarget_.Reset();
        depthPublicationOwner_.Reset();
        depthPublicationLogged_ = false;
        for (auto& eye : eyeResources_) {
            eye = {};
        }
        taaMaskViewCache_ = {};
        taaMaskViewCacheReplaceIndex_ = 0;
        compositionSurface_.Reset();
        compositionSurfaceView_.Reset();
        compositionEyeWidth_ = 0;
        compositionHeight_ = 0;
        evaluationGeometry_ = {};
        publishedInputWidth_.store(0, std::memory_order_relaxed);
        publishedInputHeight_.store(0, std::memory_order_relaxed);
        publishedOutputWidth_.store(0, std::memory_order_relaxed);
        publishedOutputHeight_.store(0, std::memory_order_relaxed);
        publishedCenterLeft_.store(0, std::memory_order_relaxed);
        publishedCenterTop_.store(0, std::memory_order_relaxed);
        resourceContractObserved_ = false;
        resetHistory_ = true;
    }

    void Runtime::restoreDynamicResolutionIfOwned() noexcept
    {
        if (!dynamicResolutionOwned_ || !dynamicResolutionManager_) {
            dynamicResolutionOwned_ = false;
            dynamicResolutionManager_ = nullptr;
            return;
        }
        auto* bytes = static_cast<std::byte*>(dynamicResolutionManager_);
        float width{};
        float height{};
        std::uint8_t active{};
        std::memcpy(
            &width,
            bytes + kDynamicResolutionWidthOffset,
            sizeof(width));
        std::memcpy(
            &height,
            bytes + kDynamicResolutionHeightOffset,
            sizeof(height));
        std::memcpy(
            &active,
            bytes + kDynamicResolutionActiveOffset,
            sizeof(active));
        if (width == ownedDynamicResolutionWidth_ &&
            height == ownedDynamicResolutionHeight_ &&
            active == savedDynamicResolutionActive_) {
            std::memcpy(
                bytes + kDynamicResolutionWidthOffset,
                &savedDynamicResolutionWidth_,
                sizeof(savedDynamicResolutionWidth_));
            std::memcpy(
                bytes + kDynamicResolutionHeightOffset,
                &savedDynamicResolutionHeight_,
                sizeof(savedDynamicResolutionHeight_));
            std::memcpy(
                bytes + kDynamicResolutionActiveOffset,
                &savedDynamicResolutionActive_,
                sizeof(savedDynamicResolutionActive_));
        } else {
            logging::warn(
                "DLAA did not restore dynamic-resolution state because another owner changed it after DLAA acquisition (current={},{},{}).",
                width,
                height,
                active != 0);
        }
        dynamicResolutionOwned_ = false;
        dynamicResolutionManager_ = nullptr;
        ownedDynamicResolutionWidth_ = 1.0f;
        ownedDynamicResolutionHeight_ = 1.0f;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        Settings publishedSettings{};
        {
            std::scoped_lock lock(settingsMutex_);
            publishedSettings = pendingSettings_;
        }
        return {
            .settings = publishedSettings,
            .streamline = streamlineSnapshot(),
            .deviceReady = deviceReady_.load(std::memory_order_acquire),
            .cameraBufferQualified = cameraBufferQualified_.load(
                std::memory_order_acquire),
            .renderResourcesQualified = renderResourcesQualified_.load(
                std::memory_order_acquire),
            .operational = operational_.load(std::memory_order_acquire),
            .cameraBindingObserved = cameraBufferIdentity_.load(
                                         std::memory_order_acquire) != nullptr,
            .mappedCameraFrames = mappedCameraFrames_.load(
                std::memory_order_relaxed),
            .cameraMapCandidates = cameraMapCandidates_.load(
                std::memory_order_relaxed),
            .cameraIdentityMatches = cameraIdentityMatches_.load(
                std::memory_order_relaxed),
            .cameraValidationFailures = cameraValidationFailures_.load(
                std::memory_order_relaxed),
            .preRenderCalls = preRenderCalls_.load(std::memory_order_relaxed),
            .postRenderCalls = postRenderCalls_.load(std::memory_order_relaxed),
            .stereoEvaluations = stereoEvaluations_.load(
                std::memory_order_relaxed),
            .stereoEvaluationFailures = stereoEvaluationFailures_.load(
                std::memory_order_relaxed),
            .committedFrames = committedFrames_.load(
                std::memory_order_relaxed),
            .qualificationSessions = qualificationSessions_.load(
                std::memory_order_relaxed),
            .hardResets = hardResets_.load(std::memory_order_relaxed),
            .inputWidth = publishedInputWidth_.load(
                std::memory_order_relaxed),
            .inputHeight = publishedInputHeight_.load(
                std::memory_order_relaxed),
            .outputWidth = publishedOutputWidth_.load(
                std::memory_order_relaxed),
            .outputHeight = publishedOutputHeight_.load(
                std::memory_order_relaxed),
            .centerLeft = publishedCenterLeft_.load(
                std::memory_order_relaxed),
            .centerTop = publishedCenterTop_.load(
                std::memory_order_relaxed),
            .gpuLeftMilliseconds = publishedGpuLeftMilliseconds_.load(
                std::memory_order_relaxed),
            .gpuRightMilliseconds = publishedGpuRightMilliseconds_.load(
                std::memory_order_relaxed),
            .gpuCommitMilliseconds = publishedGpuCommitMilliseconds_.load(
                std::memory_order_relaxed),
            .gpuTotalMilliseconds = publishedGpuTotalMilliseconds_.load(
                std::memory_order_relaxed),
        };
    }
}
