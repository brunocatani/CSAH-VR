#include "render/DxbcTextureSlots.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/ibl/IblRuntime.h"

#include "resources.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "render/BSLightingGeometryHook.h"
#include "render/BSDFPrePassShaderHook.h"
#include "support/Logger.h"

#include <Windows.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <ranges>

namespace csah::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        struct IblMaterialShaderDefinition
        {
            const char* name{};
            int resourceId{};
            CaptureProbeContract originalIdentity{};
            CaptureProbeContract replacementIdentity{};
        };

        #include "Features/ibl/GeneratedIblMaterialContracts.inl"

        [[nodiscard]] consteval bool materialDefinitionsMatchCaptureSet()
        {
            if (kIblMaterialShaderDefinitions.size() !=
                kCaptureProbeContracts.size()) {
                return false;
            }
            for (std::size_t index = 0;
                 index < kCaptureProbeContracts.size();
                 ++index) {
                const auto& expected = kCaptureProbeContracts[index];
                const auto& actual =
                    kIblMaterialShaderDefinitions[index].originalIdentity;
                if (expected.bytecodeSize != actual.bytecodeSize ||
                    expected.checksum != actual.checksum) {
                    return false;
                }
            }
            return true;
        }

        static_assert(
            materialDefinitionsMatchCaptureSet(),
            "generated IBL material replacements must match the capture set");

        constexpr std::uint64_t kRuntimePollCadenceMilliseconds = 250;
        constexpr std::uint64_t kEnvironmentCaptureCadenceMilliseconds = 33;
        constexpr std::uint64_t kWorldCaptureProbeSettleMilliseconds = 5000;
        constexpr std::uint64_t kMaterialEnvironmentTransitionMilliseconds =
            16;
        constexpr std::uint64_t
            kMaterialEnvironmentTransitionCadenceMilliseconds = 8;
        constexpr UINT kCaptureShaderResourceCount = 16;
        // Exact DFComposite DXBC samples t5/t6 in the final lighting path and
        // repeatedly samples t10 in the complex path. Live world evidence
        // exposes them at the packed-stereo output resolution. Their
        // production meaning remains untrusted until this probe records
        // non-black stereo radiance; these are evidence sources, not an IBL
        // integration contract.
        constexpr std::array<UINT, 3> kSceneRadianceCandidateSlots{
            5,
            6,
            10,
        };
        constexpr std::uint32_t kSceneRadianceMaximumReadbackPolls = 80;
        constexpr UINT kCaptureRenderTargetCount =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        constexpr std::array<DXGI_FORMAT, 2> kSceneRadianceProbeFormats{
            DXGI_FORMAT_R11G11B10_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
        };

        struct MaterialEnvironmentConstants
        {
            float iblWeight{};
            float complexMaterialWeight{};
            float transitionWeight{};
            float previousEnvironmentAvailable{};
            Float3 publishedProbeOrigin{};
            float publishedProbeOriginValid{};
            Float3 previousPublishedProbeOrigin{};
            float previousPublishedProbeOriginValid{};
            pbr::FrameData pbr{};
        };

        static_assert(sizeof(MaterialEnvironmentConstants) == 112);

        struct TextureViewRange
        {
            UINT firstMip{};
            UINT mipCount{};
            UINT firstSlice{};
            UINT sliceCount{};
        };

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_SHADER_RESOURCE_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_SRV_DIMENSION_TEXTURE2D:
                return {
                    description.Texture2D.MostDetailedMip,
                    description.Texture2D.MipLevels,
                    0,
                    1,
                };
            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MostDetailedMip,
                    description.Texture2DArray.MipLevels,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_SRV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            case D3D11_SRV_DIMENSION_TEXTURECUBE:
                return {
                    description.TextureCube.MostDetailedMip,
                    description.TextureCube.MipLevels,
                    0,
                    6,
                };
            case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
                return {
                    description.TextureCubeArray.MostDetailedMip,
                    description.TextureCubeArray.MipLevels,
                    description.TextureCubeArray.First2DArrayFace,
                    description.TextureCubeArray.NumCubes * 6,
                };
            default:
                return {};
            }
        }

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_RENDER_TARGET_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_RTV_DIMENSION_TEXTURE2D:
                return { description.Texture2D.MipSlice, 1, 0, 1 };
            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MipSlice,
                    1,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_RTV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            default:
                return {};
            }
        }

        [[nodiscard]] TextureViewRange textureViewRange(
            const D3D11_DEPTH_STENCIL_VIEW_DESC& description) noexcept
        {
            switch (description.ViewDimension) {
            case D3D11_DSV_DIMENSION_TEXTURE2D:
                return { description.Texture2D.MipSlice, 1, 0, 1 };
            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                return {
                    description.Texture2DArray.MipSlice,
                    1,
                    description.Texture2DArray.FirstArraySlice,
                    description.Texture2DArray.ArraySize,
                };
            case D3D11_DSV_DIMENSION_TEXTURE2DMS:
                return { 0, 1, 0, 1 };
            case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
                return {
                    0,
                    1,
                    description.Texture2DMSArray.FirstArraySlice,
                    description.Texture2DMSArray.ArraySize,
                };
            default:
                return {};
            }
        }

        void logCaptureView(
            std::uint16_t contractPlusOne,
            std::uint32_t checksumPrefix,
            const char* binding,
            UINT slot,
            ID3D11View* view,
            DXGI_FORMAT viewFormat,
            UINT viewDimension,
            TextureViewRange range) noexcept
        {
            if (!view) {
                return;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource) {
                logging::info(
                    "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: viewFormat={}, viewDimension={}, resource unavailable.",
                    contractPlusOne,
                    checksumPrefix,
                    binding,
                    slot,
                    static_cast<unsigned>(viewFormat),
                    viewDimension);
                return;
            }

            D3D11_RESOURCE_DIMENSION resourceDimension{};
            resource->GetType(&resourceDimension);
            ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED(resource.As(&texture)) && texture) {
                D3D11_TEXTURE2D_DESC textureDescription{};
                texture->GetDesc(&textureDescription);
                logging::info(
                    "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: resource=0x{:X}, viewFormat={}, viewDimension={}, mip={}+{}, slice={}+{}, texture={}x{}, format={}, mips={}, array={}, samples={}, bind=0x{:X}, misc=0x{:X}.",
                    contractPlusOne,
                    checksumPrefix,
                    binding,
                    slot,
                    reinterpret_cast<std::uintptr_t>(resource.Get()),
                    static_cast<unsigned>(viewFormat),
                    viewDimension,
                    range.firstMip,
                    range.mipCount,
                    range.firstSlice,
                    range.sliceCount,
                    textureDescription.Width,
                    textureDescription.Height,
                    static_cast<unsigned>(textureDescription.Format),
                    textureDescription.MipLevels,
                    textureDescription.ArraySize,
                    textureDescription.SampleDesc.Count,
                    textureDescription.BindFlags,
                    textureDescription.MiscFlags);
                return;
            }

            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} {}{}: resource=0x{:X}, viewFormat={}, viewDimension={}, resourceDimension={}.",
                contractPlusOne,
                checksumPrefix,
                binding,
                slot,
                reinterpret_cast<std::uintptr_t>(resource.Get()),
                static_cast<unsigned>(viewFormat),
                viewDimension,
                static_cast<unsigned>(resourceDimension));
        }

        struct EmbeddedShader
        {
            const void* data{};
            std::size_t size{};
        };

        [[nodiscard]] EmbeddedShader loadEmbeddedShader(
            int resourceId) noexcept
        {
            HMODULE module{};
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&loadEmbeddedShader),
                    &module)) {
                return {};
            }
            const auto resource = FindResourceW(
                module,
                MAKEINTRESOURCEW(resourceId),
                RT_RCDATA);
            if (!resource) {
                return {};
            }
            const auto loaded = LoadResource(module, resource);
            if (!loaded) {
                return {};
            }
            const auto size = SizeofResource(module, resource);
            return { LockResource(loaded), static_cast<std::size_t>(size) };
        }

        [[nodiscard]] const char* sceneProbeFormatName(
            DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R11G11B10_FLOAT:
                return "R11G11B10_FLOAT";
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return "R8G8B8A8_UNORM";
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return "R16G16B16A16_FLOAT";
            default:
                return "unsupported";
            }
        }

        [[nodiscard]] bool copySceneProbeSamples(
            ID3D11DeviceContext* context,
            ID3D11Texture2D* source,
            ID3D11Texture2D* destination,
            const D3D11_TEXTURE2D_DESC& sourceDescription) noexcept
        {
            if (!context || !source || !destination ||
                sourceDescription.Width < 4 || sourceDescription.Height < 2 ||
                sourceDescription.MipLevels == 0 ||
                sourceDescription.ArraySize != 1 ||
                sourceDescription.SampleDesc.Count != 1 ||
                (sourceDescription.Format != DXGI_FORMAT_R16G16B16A16_FLOAT &&
                    std::ranges::find(kSceneRadianceProbeFormats, sourceDescription.Format) ==
                        kSceneRadianceProbeFormats.end())) {
                return false;
            }

            const auto coordinates = sceneProbeCoordinates(
                sourceDescription.Width,
                sourceDescription.Height);
            for (std::size_t index = 0; index < coordinates.size(); ++index) {
                const auto& coordinate = coordinates[index];
                const D3D11_BOX sourceBox{
                    coordinate.x,
                    coordinate.y,
                    0,
                    coordinate.x + 1,
                    coordinate.y + 1,
                    1,
                };
                context->CopySubresourceRegion(
                    destination,
                    0,
                    static_cast<UINT>(index),
                    0,
                    0,
                    source,
                    0,
                    &sourceBox);
            }
            return true;
        }

        enum class SceneProbeReadResult
        {
            pending,
            ready,
            failed,
        };

        [[nodiscard]] SceneProbeReadResult readSceneProbeSamples(
            ID3D11DeviceContext* context,
            ID3D11Texture2D* texture,
            DXGI_FORMAT format,
            std::array<SceneProbeRgb, kSceneProbeSampleCount>& samples)
            noexcept
        {
            if (!context || !texture) {
                return SceneProbeReadResult::failed;
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result = context->Map(
                texture,
                0,
                D3D11_MAP_READ,
                D3D11_MAP_FLAG_DO_NOT_WAIT,
                &mapped);
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
                return SceneProbeReadResult::pending;
            }
            const auto pixelBytes = format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8U : 4U;
            if (FAILED(result)) return SceneProbeReadResult::failed;
            if (!mapped.pData || mapped.RowPitch < kSceneProbeSampleCount * pixelBytes) {
                context->Unmap(texture, 0);
                return SceneProbeReadResult::failed;
            }

            const auto* source = static_cast<const std::byte*>(mapped.pData);
            for (std::size_t index = 0; index < samples.size(); ++index) {
                if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                    std::array<std::uint16_t, 4> half{};
                    std::memcpy(half.data(), source + index * pixelBytes, sizeof(half));
                    samples[index] = decodeR16G16B16A16Float(half);
                    continue;
                }
                std::uint32_t packed{};
                std::memcpy(
                    &packed,
                    source + index * sizeof(packed),
                    sizeof(packed));
                switch (format) {
                case DXGI_FORMAT_R11G11B10_FLOAT:
                    samples[index] = decodeR11G11B10Float(packed);
                    break;
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    samples[index] = decodeR8G8B8A8Unorm(packed);
                    break;
                default:
                    context->Unmap(texture, 0);
                    return SceneProbeReadResult::failed;
                }
            }
            context->Unmap(texture, 0);
            return SceneProbeReadResult::ready;
        }

    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    bool Runtime::environmentAcquisitionEnabled() const noexcept
    {
        const auto iblConsumer =
            enabled_.load(std::memory_order_acquire) &&
            (dynamicCubemapsEnabled_.load(std::memory_order_acquire) ||
                diffuseEnabled_.load(std::memory_order_acquire));
        return iblConsumer ||
            sslrConsumerEnabled_.load(std::memory_order_acquire);
    }

    bool Runtime::materialResponseEnabled() const noexcept
    {
        const auto dynamicEnvironment =
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire);
        return dynamicEnvironment ||
            pbrEnabled_.load(std::memory_order_acquire);
    }

    void Runtime::refreshComplexMaterialProducerGate() noexcept
    {
        render::setDFPrePassIblEnabled(materialResponseEnabled());
    }

    void Runtime::beginMaterialEnvironmentTransition(
        std::uint64_t tickMilliseconds) noexcept
    {
        materialEnvironmentTransitionActive_ =
            environmentProvider_.previousEnvironment() &&
            environmentProvider_.previousValidity();
        materialEnvironmentTransitionStartMilliseconds_ = tickMilliseconds;
        nextMaterialEnvironmentTransitionTickMilliseconds_ = 0;
        updateMaterialEnvironmentTransition(tickMilliseconds);
    }

    void Runtime::updateMaterialEnvironmentTransition(
        std::uint64_t tickMilliseconds) noexcept
    {
        const auto pbrRevision = pbrSettingsRevision_.load(
            std::memory_order_acquire);
        const auto pbrSettingsChanged =
            pbrRevision != uploadedPbrSettingsRevision_;
        if ((!materialEnvironmentTransitionActive_ &&
                nextMaterialEnvironmentTransitionTickMilliseconds_ != 0 &&
                !pbrSettingsChanged) ||
            !context_ ||
            !materialEnabledConstants_ ||
            tickMilliseconds <
                nextMaterialEnvironmentTransitionTickMilliseconds_) {
            return;
        }
        nextMaterialEnvironmentTransitionTickMilliseconds_ =
            tickMilliseconds +
            kMaterialEnvironmentTransitionCadenceMilliseconds;

        const auto previousAvailable =
            environmentProvider_.previousEnvironment() &&
            environmentProvider_.previousValidity();
        const auto requestedSession = requestedCaptureProbeSessionId_.load(
            std::memory_order_acquire);
        const auto dynamicMaterialAvailable =
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire) &&
            publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ == requestedSession &&
            environmentProvider_.publishedEnvironment() &&
            environmentProvider_.publishedValidity();
        const auto publishedProbeOrigin =
            environmentProvider_.publishedProbeOrigin();
        const auto previousProbeOrigin =
            environmentProvider_.previousProbeOrigin();
        const auto elapsed = tickMilliseconds >=
                materialEnvironmentTransitionStartMilliseconds_ ?
            tickMilliseconds -
                materialEnvironmentTransitionStartMilliseconds_ :
            kMaterialEnvironmentTransitionMilliseconds;
        const auto transitionWeight = previousAvailable &&
                elapsed < kMaterialEnvironmentTransitionMilliseconds ?
            static_cast<float>(elapsed) /
                static_cast<float>(
                    kMaterialEnvironmentTransitionMilliseconds) :
            1.0F;
        materialEnvironmentTransitionWeight_ = transitionWeight;
        const pbr::Settings pbrSettings{
            .enabled = pbrEnabled_.load(std::memory_order_acquire),
            .legacyMaterials = pbrLegacyMaterials_.load(
                std::memory_order_relaxed),
            .directGgx = pbrDirectGgx_.load(std::memory_order_relaxed),
            .grassGgx = pbrGrassGgx_.load(std::memory_order_relaxed),
            .environmentFresnel = pbrEnvironmentFresnel_.load(
                std::memory_order_relaxed),
            .energyConservation = pbrEnergyConservation_.load(
                std::memory_order_relaxed),
            .multiscatterCompensation =
                pbrMultiscatterCompensation_.load(
                    std::memory_order_relaxed),
            .specularOcclusion = pbrSpecularOcclusion_.load(
                std::memory_order_relaxed),
            .roughnessMultiplier = pbrRoughnessMultiplier_.load(
                std::memory_order_relaxed),
            .specularRoughnessBlend = pbrSpecularRoughnessBlend_.load(
                std::memory_order_relaxed),
            .baseF0Multiplier = pbrBaseF0Multiplier_.load(
                std::memory_order_relaxed),
            .minimumF0 = pbrMinimumF0_.load(
                std::memory_order_relaxed),
            .cubemapToF0Multiplier = pbrCubemapToF0Multiplier_.load(
                std::memory_order_relaxed),
            .complexMaterialF0Multiplier =
                pbrComplexMaterialF0Multiplier_.load(
                    std::memory_order_relaxed),
            .directLightingScale = pbrDirectLightingScale_.load(
                std::memory_order_relaxed),
        };
        const MaterialEnvironmentConstants constants{
            .iblWeight = dynamicMaterialAvailable ? 1.0F : 0.0F,
            .complexMaterialWeight = 1.0F,
            .transitionWeight = transitionWeight,
            .previousEnvironmentAvailable =
                dynamicMaterialAvailable && previousAvailable ? 1.0F : 0.0F,
            .publishedProbeOrigin = publishedProbeOrigin.position,
            .publishedProbeOriginValid =
                publishedProbeOrigin.valid ? 1.0F : 0.0F,
            .previousPublishedProbeOrigin = previousProbeOrigin.position,
            .previousPublishedProbeOriginValid =
                previousProbeOrigin.valid ? 1.0F : 0.0F,
            .pbr = pbr::makeFrameData(pbrSettings),
        };
        context_->UpdateSubresource(
            materialEnabledConstants_.Get(),
            0,
            nullptr,
            &constants,
            0,
            0);
        uploadedPbrSettingsRevision_ = pbrRevision;
        if (transitionWeight >= 1.0F) {
            materialEnvironmentTransitionActive_ = false;
        }
    }

    void Runtime::setEnabled(bool enabled) noexcept
    {
        const auto acquisitionWasEnabled = environmentAcquisitionEnabled();
        enabled_.store(enabled, std::memory_order_release);
        refreshComplexMaterialProducerGate();
        if (!acquisitionWasEnabled && environmentAcquisitionEnabled()) {
            beginWorldCaptureProbeSession();
        }
    }

    void Runtime::setDynamicCubemapsEnabled(bool enabled) noexcept
    {
        const auto acquisitionWasEnabled = environmentAcquisitionEnabled();
        const auto previous = dynamicCubemapsEnabled_.exchange(
            enabled,
            std::memory_order_acq_rel);
        refreshComplexMaterialProducerGate();
        if (!acquisitionWasEnabled && environmentAcquisitionEnabled()) {
            beginWorldCaptureProbeSession();
        }
        if (previous != enabled) {
            logging::info(
                "Dynamic Cubemaps {} live; specular environment substitution is {}, and the shared IBL capture remains active only while a diffuse or specular consumer requires it.",
                enabled ? "enabled" : "disabled",
                enabled ? "armed" : "retired");
        }
    }

    void Runtime::setDiffuseEnabled(bool enabled) noexcept
    {
        const auto acquisitionWasEnabled = environmentAcquisitionEnabled();
        diffuseEnabled_.store(enabled, std::memory_order_release);
        if (!acquisitionWasEnabled && environmentAcquisitionEnabled()) {
            beginWorldCaptureProbeSession();
        }
    }

    void Runtime::setDiffuseLevel(float level) noexcept
    {
        const auto safe = sanitize(Settings{ .diffuseLevel = level });
        diffuseLevelBits_.store(
            std::bit_cast<std::uint32_t>(safe.diffuseLevel),
            std::memory_order_release);
    }

    void Runtime::setSslrConsumerEnabled(const bool enabled) noexcept
    {
        const auto acquisitionWasEnabled = environmentAcquisitionEnabled();
        const auto previous = sslrConsumerEnabled_.exchange(
            enabled,
            std::memory_order_acq_rel);
        if (!acquisitionWasEnabled && environmentAcquisitionEnabled()) {
            beginWorldCaptureProbeSession();
        }
        if (previous != enabled) {
            logging::info(
                "Vanilla Fixes stable-reflection environment consumer {} live; shared IBL capture is {} without changing Dynamic Cubemaps or Diffuse IBL presentation settings.",
                enabled ? "enabled" : "disabled",
                environmentAcquisitionEnabled() ? "required" : "not required");
        }
    }

    bool Runtime::tryGetSslrEnvironment(
        SslrEnvironmentView& view) noexcept
    {
        view = {};
        if (!sslrConsumerEnabled_.load(std::memory_order_acquire) ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return false;
        }
        updateMaterialEnvironmentTransition(GetTickCount64());

        const auto requestedSession =
            requestedCaptureProbeSessionId_.load(std::memory_order_acquire);
        const auto publishedAvailable = publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ == requestedSession &&
            environmentProvider_.publishedEnvironment() &&
            environmentProvider_.publishedValidity() &&
            environmentProvider_.publishedPosition();
        if (!publishedAvailable) {
            return true;
        }

        view.publishedEnvironment =
            environmentProvider_.publishedEnvironment();
        view.publishedValidity = environmentProvider_.publishedValidity();
        view.publishedPosition = environmentProvider_.publishedPosition();
        view.publishedProbeOrigin =
            environmentProvider_.publishedProbeOrigin();
        view.publishedAvailable = true;
        view.transitionWeight = materialEnvironmentTransitionWeight_;

        const auto previousAvailable =
            materialEnvironmentTransitionActive_ &&
            environmentProvider_.previousEnvironment() &&
            environmentProvider_.previousValidity() &&
            environmentProvider_.previousPosition();
        if (previousAvailable) {
            view.previousEnvironment =
                environmentProvider_.previousEnvironment();
            view.previousValidity =
                environmentProvider_.previousValidity();
            view.previousPosition =
                environmentProvider_.previousPosition();
            view.previousProbeOrigin =
                environmentProvider_.previousProbeOrigin();
            view.previousAvailable = true;
        }
        return true;
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        const auto acquisitionWasEnabled = environmentAcquisitionEnabled();
        const auto previousDynamic = dynamicCubemapsEnabled_.load(
            std::memory_order_acquire);
        diffuseLevelBits_.store(
            std::bit_cast<std::uint32_t>(safe.diffuseLevel),
            std::memory_order_release);
        diffuseEnabled_.store(
            safe.diffuseEnabled,
            std::memory_order_release);
        dynamicCubemapsEnabled_.store(
            safe.dynamicCubemapsEnabled,
            std::memory_order_release);
        enabled_.store(safe.enabled, std::memory_order_release);
        refreshComplexMaterialProducerGate();
        if (!acquisitionWasEnabled && environmentAcquisitionEnabled()) {
            beginWorldCaptureProbeSession();
        }
        if (previousDynamic != safe.dynamicCubemapsEnabled) {
            logging::info(
                "Dynamic Cubemaps {} live; specular environment substitution is {}, and the shared IBL capture remains active only while a diffuse or specular consumer requires it.",
                safe.dynamicCubemapsEnabled ? "enabled" : "disabled",
                safe.dynamicCubemapsEnabled ? "armed" : "retired");
        }
    }

    void Runtime::applyPbrSettings(const pbr::Settings& settings) noexcept
    {
        const auto safe = pbr::sanitize(settings);
        pbrEnabled_.store(safe.enabled, std::memory_order_release);
        pbrLegacyMaterials_.store(
            safe.legacyMaterials, std::memory_order_relaxed);
        pbrDirectGgx_.store(safe.directGgx, std::memory_order_relaxed);
        pbrGrassGgx_.store(safe.grassGgx, std::memory_order_relaxed);
        pbrEnvironmentFresnel_.store(
            safe.environmentFresnel, std::memory_order_relaxed);
        pbrEnergyConservation_.store(
            safe.energyConservation, std::memory_order_relaxed);
        pbrMultiscatterCompensation_.store(
            safe.multiscatterCompensation, std::memory_order_relaxed);
        pbrSpecularOcclusion_.store(
            safe.specularOcclusion, std::memory_order_relaxed);
        pbrRoughnessMultiplier_.store(
            safe.roughnessMultiplier, std::memory_order_relaxed);
        pbrSpecularRoughnessBlend_.store(
            safe.specularRoughnessBlend, std::memory_order_relaxed);
        pbrBaseF0Multiplier_.store(
            safe.baseF0Multiplier, std::memory_order_relaxed);
        pbrMinimumF0_.store(safe.minimumF0, std::memory_order_relaxed);
        pbrCubemapToF0Multiplier_.store(
            safe.cubemapToF0Multiplier, std::memory_order_relaxed);
        pbrComplexMaterialF0Multiplier_.store(
            safe.complexMaterialF0Multiplier, std::memory_order_relaxed);
        pbrDirectLightingScale_.store(
            safe.directLightingScale, std::memory_order_relaxed);
        pbrSettingsRevision_.fetch_add(1, std::memory_order_release);
        refreshComplexMaterialProducerGate();
    }

    bool Runtime::tryGetDiffuseAmbient(
        DiffuseAmbientSample& sample) const noexcept
    {
        if (!enabled_.load(std::memory_order_acquire) ||
            !diffuseEnabled_.load(std::memory_order_acquire) ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return false;
        }

        constexpr std::uint32_t kMaximumReadAttempts = 3;
        for (std::uint32_t attempt = 0; attempt < kMaximumReadAttempts;
             ++attempt) {
            const auto before = publishedSequence_.load(
                std::memory_order_acquire);
            if ((before & 1u) != 0) {
                continue;
            }
            DiffuseSH candidate{};
            std::size_t index{};
            for (auto& channel : candidate.rgb) {
                for (auto& coefficient : channel) {
                    coefficient = std::bit_cast<float>(
                        publishedCoefficientBits_[index++].load(
                            std::memory_order_relaxed));
                }
            }
            std::array<float, kEnvironmentCubeFaceCount> faceConfidence{};
            for (std::size_t face = 0; face < faceConfidence.size(); ++face) {
                faceConfidence[face] = std::bit_cast<float>(
                    publishedFaceConfidenceBits_[face].load(
                        std::memory_order_relaxed));
            }
            const auto coverage = std::bit_cast<float>(
                diffuseSHCoverageBits_.load(std::memory_order_relaxed));
            const auto usable = publishedUsable_.load(
                std::memory_order_relaxed);
            const auto generation = publishedGeneration_.load(
                std::memory_order_relaxed);
            const auto session = publishedDiffuseSessionId_.load(
                std::memory_order_relaxed);
            const auto after = publishedSequence_.load(
                std::memory_order_acquire);
            if (before != after || (after & 1u) != 0) {
                continue;
            }
            const auto candidateLevel = std::bit_cast<float>(
                diffuseLevelBits_.load(std::memory_order_acquire));
            const auto requestedSession =
                requestedCaptureProbeSessionId_.load(
                    std::memory_order_acquire);
            if (!usable || session == 0 || session != requestedSession ||
                !validDiffuseSH(candidate) ||
                !std::isfinite(coverage) || coverage < 0.0f ||
                !std::ranges::all_of(
                    faceConfidence,
                    [](float value) {
                        return std::isfinite(value) && value >= 0.0f;
                    }) ||
                !std::isfinite(candidateLevel) || candidateLevel < 0.0f) {
                return false;
            }
            sample = {
                .coefficients = candidate,
                .cubeFaceConfidence = faceConfidence,
                .coverage = coverage,
                .level = candidateLevel,
                .generation = generation,
            };
            return true;
        }
        return false;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* immediateContext,
        CreatePixelShaderFunction createPixelShader) noexcept
    {
        resetResources();
        if (!device || !immediateContext || !createPixelShader) {
            logging::error(
                "IBL initialization rejected a null D3D11 device/context/pixel-shader trampoline.");
            return;
        }
        device_ = device;
        context_ = immediateContext;
        if (!createResources(createPixelShader)) {
            logging::error(
                "IBL resources could not be created; the subsystem remains fail-closed and has no visual effect.");
            resetResources();
            return;
        }
        if (!reflectionFreeDrawTiming_.initialize(
                device,
                immediateContext,
                "Image Based Lighting duplicate draw",
                { "reflection-free DFComposite", nullptr, nullptr,
                    nullptr },
                1,
                5)) {
            logging::warn(
                "IBL could not allocate duplicate-draw GPU timing queries; rendering remains active without performance telemetry.");
        }
        resourcesReady_.store(true, std::memory_order_release);
        if (environmentAcquisitionEnabled()) {
            // Settings, including the Vanilla Fixes SSLR consumer, are
            // published before D3D device creation. resetResources() clears
            // the pre-device session request, so arm a fresh session only
            // after the provider and capture resources are ready.
            beginWorldCaptureProbeSession();
        }
        const auto environment = environmentProvider_.snapshot();
        logging::info(
            "IBL foundation initialized; transactional radiance/validity/position state={}, extent={}, mips={}, position-aware Dynamic Cubemaps updater ready={}, validity-aware diffuse fitting is staged, and 41 exact material replacements remain fail-closed until publication.",
            static_cast<unsigned>(environment.state),
            environment.extent,
            environment.mipCount,
            environmentUpdater_.snapshot().initialized);
    }

    void Runtime::beginWorldCaptureProbeSession() noexcept
    {
        const auto earliestTick =
            GetTickCount64() + kWorldCaptureProbeSettleMilliseconds;
        requestedCaptureProbeEarliestTickMilliseconds_.store(
            earliestTick,
            std::memory_order_release);
        auto sessionId = requestedCaptureProbeSessionId_.fetch_add(
                             1,
                             std::memory_order_release) +
            1;
        if (sessionId == 0) {
            sessionId = 1;
            requestedCaptureProbeSessionId_.store(
                sessionId,
                std::memory_order_release);
        }
        logging::info(
            "IBL world environment session {} armed; diagnostic and provider sampling begin after a {} ms settle interval.",
            sessionId,
            kWorldCaptureProbeSettleMilliseconds);
    }

    bool Runtime::synchronizeWorldCaptureSession() noexcept
    {
        const auto requestedSessionId =
            requestedCaptureProbeSessionId_.load(std::memory_order_acquire);
        if (requestedSessionId == 0) {
            return false;
        }
        if (requestedSessionId != activeCaptureProbeSessionId_) {
            activeCaptureProbeSessionId_ = requestedSessionId;
            activeCaptureProbeEarliestTickMilliseconds_ =
                requestedCaptureProbeEarliestTickMilliseconds_.load(
                    std::memory_order_relaxed);
            captureProbeSessionComplete_ = false;
            for (auto& logged : captureProbeLogged_) {
                logged.store(false, std::memory_order_relaxed);
            }
            for (auto& slot : sceneRadianceReadbackSlots_) {
                slot.sourceWidth = 0;
                slot.sourceHeight = 0;
                slot.contractPlusOne = 0;
                slot.checksumPrefix = 0;
                slot.pendingPolls = 0;
                slot.rollingReady = false;
                slot.reflectionFreeCaptureAttempted = false;
                slot.rollingReflectionFreeCopied = false;
                slot.rollingPixelShaderCopied.fill(false);
                slot.pending = false;
                slot.completed = false;
                slot.reflectionFreeCopied = false;
                slot.pixelShaderCopied.fill(false);
                slot.failureLogged = false;
            }
            completedCaptureProbes_.store(0, std::memory_order_relaxed);
            nextEnvironmentCaptureTickMilliseconds_.store(
                activeCaptureProbeEarliestTickMilliseconds_,
                std::memory_order_release);
            reflectionFreeCaptureDiagnosticReserved_ = false;
            reflectionFreeCaptureProductionReserved_ = false;
            loggedReflectionFreeCaptureFailure_ = false;
            loggedEnvironmentUpdateFailure_ = false;
        }
        return GetTickCount64() >=
            activeCaptureProbeEarliestTickMilliseconds_;
    }

    bool Runtime::activateWorldCaptureProbeSession() noexcept
    {
        return synchronizeWorldCaptureSession() &&
            !captureProbeSessionComplete_;
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        std::size_t bytecodeSize,
        ID3D11PixelShader* shader) noexcept
    {
        const auto binding = classifyCaptureProbeShader(
            bytecode,
            bytecodeSize);
        if (!binding.isDFComposite || !shader) {
            return;
        }

        try {
            const std::scoped_lock lock(captureShaderMutex_);
            const auto firstSlot =
                (reinterpret_cast<std::uintptr_t>(shader) >> 4) %
                captureShaderSlots_.size();
            for (std::size_t probe = 0;
                 probe < captureShaderSlots_.size();
                 ++probe) {
                auto& slot = captureShaderSlots_[
                    (firstSlot + probe) % captureShaderSlots_.size()];
                const auto existing = slot.shader.load(
                    std::memory_order_acquire);
                if (existing == shader) {
                    return;
                }
                if (existing) {
                    continue;
                }
                slot.owner = shader;
                slot.contractPlusOne.store(
                    binding.environmentContractPlusOne,
                    std::memory_order_relaxed);
                slot.tiledDiffuse.store(render::declaresTexture2D(bytecode,bytecodeSize,11),std::memory_order_relaxed);
                slot.deferredDiffuse.store(render::declaresTexture2D(bytecode,bytecodeSize,5),std::memory_order_relaxed);
                slot.shader.store(shader, std::memory_order_release);
                matchingCaptureShaders_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return;
            }
        } catch (...) {
            logging::warn(
                "IBL capture shader registration failed; the diagnostic remains fail-closed.");
            return;
        }

        if (!captureRegistryOverflowLogged_.exchange(
                true,
                std::memory_order_acq_rel)) {
            logging::warn(
                "IBL capture shader registry exhausted its {} fixed slots; later matches will remain unobserved.",
                captureShaderSlots_.size());
        }
    }

    CaptureProbeShaderBinding Runtime::captureProbeBindingForShader(
        ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return {};
        }
        const auto firstSlot =
            (reinterpret_cast<std::uintptr_t>(shader) >> 4) %
            captureShaderSlots_.size();
        for (std::size_t probe = 0;
             probe < captureShaderSlots_.size();
             ++probe) {
            const auto& slot = captureShaderSlots_[
                (firstSlot + probe) % captureShaderSlots_.size()];
            const auto candidate = slot.shader.load(std::memory_order_acquire);
            if (!candidate) {
                return {};
            }
            if (candidate == shader) {
                return {
                    true,
                    slot.contractPlusOne.load(std::memory_order_relaxed),
                    slot.tiledDiffuse.load(std::memory_order_relaxed),
                    slot.deferredDiffuse.load(std::memory_order_relaxed)
                };
            }
        }
        return {};
    }

    Runtime::MaterialPixelShaderSelection Runtime::selectMaterialPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* original) noexcept
    {
        MaterialPixelShaderSelection selection{ original, {} };
        const auto dynamicMaterialAvailable =
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire) &&
            publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ ==
                requestedCaptureProbeSessionId_.load(
                    std::memory_order_acquire) &&
            environmentProvider_.publishedEnvironment() &&
            environmentProvider_.publishedValidity();
        if (!context || context != context_.Get() || !original ||
            (!dynamicMaterialAvailable && !skylighting::Runtime::get().visibilityReady() &&
                !pbrEnabled_.load(std::memory_order_acquire)) ||
            materialConsumptionFailed_ ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return selection;
        }
        const auto capture = captureProbeBindingForShader(original);
        if (capture.environmentContractPlusOne == 0 ||
            capture.environmentContractPlusOne >
                materialReplacementShaders_.size()) {
            return selection;
        }
        const auto index = static_cast<std::size_t>(
            capture.environmentContractPlusOne - 1);
        auto* replacement = materialReplacementShaders_[index].Get();
        if (!replacement || replacement == original) {
            return selection;
        }

        selection.shader = replacement;
        selection.binding = {
            original,
            replacement,
            capture.environmentContractPlusOne,
        };
        materialReplacementBinds_.fetch_add(1, std::memory_order_relaxed);
        if (!loggedFirstMaterialBind_) {
            loggedFirstMaterialBind_ = true;
            const auto provider = environmentProvider_.snapshot();
            logging::info(
                "IBL material consumption activated for exact DFComposite[{:02}] from atomically published radiance/validity generation {}; vanilla t8/s8 remains the per-direction fallback.",
                capture.environmentContractPlusOne,
                provider.publishedGeneration);
        }
        return selection;
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return environmentAcquisitionEnabled() || materialResponseEnabled() ||
            skylighting::Runtime::get().visibilityReady();
    }

    bool Runtime::materialBindingActive(
        MaterialShaderBinding binding) const noexcept
    {
        const auto pbrActive = pbrEnabled_.load(std::memory_order_acquire);
        const auto dynamicMaterialAvailable =
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire) &&
            publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ ==
                requestedCaptureProbeSessionId_.load(
                    std::memory_order_acquire) &&
            environmentProvider_.publishedEnvironment() &&
            environmentProvider_.publishedValidity() &&
            environmentProvider_.publishedPosition();
        return binding &&
            (dynamicMaterialAvailable || skylighting::Runtime::get().visibilityReady() ||
                pbrEnabled_.load(std::memory_order_acquire)) &&
            !materialConsumptionFailed_ &&
            resourcesReady_.load(std::memory_order_acquire) &&
            (!dynamicMaterialAvailable || materialAlbedo_) && (!pbrActive || (materialAlbedo_ && materialProperties_));
    }

    ScopedMaterialBindings Runtime::scopeMaterialBindings(
        ID3D11DeviceContext* context,
        MaterialShaderBinding binding,
        bool enabled) noexcept
    {
        if (!binding || !context || context != context_.Get() ||
            (enabled && !materialBindingActive(binding)) ||
            materialConsumptionFailed_ ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return {};
        }
        const auto dynamicActive = enabled &&
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire) &&
            publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ ==
                requestedCaptureProbeSessionId_.load(
                    std::memory_order_acquire);
        auto* radiance = dynamicActive ?
            environmentProvider_.publishedEnvironment() : nullptr;
        auto* validity = dynamicActive ?
            environmentProvider_.publishedValidity() : nullptr;
        auto* previousRadiance = dynamicActive &&
                materialEnvironmentTransitionActive_ ?
            environmentProvider_.previousEnvironment() : nullptr;
        auto* previousValidity = dynamicActive &&
                materialEnvironmentTransitionActive_ ?
            environmentProvider_.previousValidity() : nullptr;
        auto* position = dynamicActive ?
            environmentProvider_.publishedPosition() : nullptr;
        auto* previousPosition = dynamicActive &&
                materialEnvironmentTransitionActive_ ?
            environmentProvider_.previousPosition() : nullptr;
        auto* albedo = enabled ? materialAlbedo_.Get() : nullptr;
        const auto pbrActive = enabled &&
            pbrEnabled_.load(std::memory_order_acquire);
        const auto materialResponse = pbrActive || (dynamicActive && radiance && validity && albedo);
        auto* constants = materialResponse ? materialEnabledConstants_.Get() : materialDisabledConstants_.Get();
        auto* materialProperties = pbrActive ? materialProperties_.Get() :
                                               nullptr;
        auto* pbrMaterial = pbrActive ? surface_classification::Runtime::get()
                                           .pbrMaterialShaderResourceView() :
                                       nullptr;
        auto* surfaceClass = pbrActive ? surface_classification::Runtime::get()
                                           .shaderResourceView() :
                                       nullptr;
        if (!constants || (materialResponse &&
                (!albedo || (pbrActive && !materialProperties) ||
                    (pbrActive && !surfaceClass) ||
                    (dynamicActive &&
                        (!radiance || !validity || !position))))) {
            materialBindingFailures_.fetch_add(1, std::memory_order_relaxed);
            materialConsumptionFailed_ = true;
            return {};
        }

        auto* style = dynamicActive ? materialStyle_.prepare(context) : nullptr;
        ScopedMaterialBindings scope(
            context,
            albedo,
            radiance,
            validity,
            previousRadiance,
            previousValidity,
            position,
            previousPosition,
            materialProperties,
            pbrMaterial,
            surfaceClass,
            constants,
            style);
        if (!scope.active()) {
            materialBindingFailures_.fetch_add(1, std::memory_order_relaxed);
            materialConsumptionFailed_ = true;
            if (!loggedMaterialBindingFailure_) {
                loggedMaterialBindingFailure_ = true;
                logging::warn(
                    "IBL material binding transaction failed closed (enabled={}, reason={}, contract={}); the draw uses the exact vanilla shader path.",
                    enabled,
                    static_cast<unsigned>(scope.rejection()),
                    binding.contractPlusOne);
            }
        }
        return scope;
    }

    void Runtime::onMaterialBindingsComplete(
        MaterialShaderBinding binding,
        bool enabled,
        bool restored) noexcept
    {
        if (!binding || restored) {
            return;
        }
        materialBindingFailures_.fetch_add(1, std::memory_order_relaxed);
        materialConsumptionFailed_ = true;
        if (!loggedMaterialBindingFailure_) {
            loggedMaterialBindingFailure_ = true;
            logging::warn(
                "IBL material binding restoration failed closed (enabled={}, contract={}); later exact draws fall back to their vanilla shader.",
                enabled,
                binding.contractPlusOne);
        }
    }

    void Runtime::onCaptureProbeDraw(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!environmentAcquisitionEnabled() || !context ||
            context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
            return;
        }
        const auto contractIndex =
            static_cast<std::size_t>(contractPlusOne - 1);
        if (captureProbeLogged_[contractIndex].exchange(
                true,
                std::memory_order_acq_rel)) {
            return;
        }

        std::array<ID3D11ShaderResourceView*, kCaptureShaderResourceCount>
            shaderResources{};
        context->PSGetShaderResources(
            0,
            static_cast<UINT>(shaderResources.size()),
            shaderResources.data());

        std::array<ID3D11RenderTargetView*, kCaptureRenderTargetCount>
            renderTargets{};
        ID3D11DepthStencilView* depthStencil{};
        context->OMGetRenderTargets(
            static_cast<UINT>(renderTargets.size()),
            renderTargets.data(),
            &depthStencil);

        ID3D11Buffer* sceneConstantsRaw{};
        context->PSGetConstantBuffers(12, 1, &sceneConstantsRaw);
        ComPtr<ID3D11Buffer> sceneConstants;
        sceneConstants.Attach(sceneConstantsRaw);

        std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
            viewports{};
        UINT viewportCount = static_cast<UINT>(viewports.size());
        context->RSGetViewports(&viewportCount, viewports.data());

        const auto shaderResourceCount = static_cast<unsigned>(
            std::ranges::count_if(
                shaderResources,
                [] (const auto* view) { return view != nullptr; }));
        const auto renderTargetCount = static_cast<unsigned>(
            std::ranges::count_if(
                renderTargets,
                [] (const auto* view) { return view != nullptr; }));
        const auto& contract = kCaptureProbeContracts[contractIndex];
        const auto checksumPrefix =
            (static_cast<std::uint32_t>(contract.checksum[0]) << 24) |
            (static_cast<std::uint32_t>(contract.checksum[1]) << 16) |
            (static_cast<std::uint32_t>(contract.checksum[2]) << 8) |
            static_cast<std::uint32_t>(contract.checksum[3]);

        logging::info(
            "IBL capture WORLD DFComposite[{:02}] {:08x} draw: PS-SRVs={}, RTVs={}, DSV={}, b12={}, viewports={}; state read only, image unchanged.",
            contractPlusOne,
            checksumPrefix,
            shaderResourceCount,
            renderTargetCount,
            depthStencil != nullptr,
            sceneConstants != nullptr,
            viewportCount);

        if (sceneConstants) {
            D3D11_BUFFER_DESC description{};
            sceneConstants->GetDesc(&description);
            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} PS-b12: resource=0x{:X}, bytes={}, usage={}, bind=0x{:X}, cpu=0x{:X}, misc=0x{:X}.",
                contractPlusOne,
                checksumPrefix,
                reinterpret_cast<std::uintptr_t>(sceneConstants.Get()),
                description.ByteWidth,
                static_cast<unsigned>(description.Usage),
                description.BindFlags,
                description.CPUAccessFlags,
                description.MiscFlags);
        }

        for (UINT viewportIndex = 0; viewportIndex < viewportCount;
             ++viewportIndex) {
            const auto& viewport = viewports[viewportIndex];
            logging::info(
                "IBL capture WORLD DFComposite[{:02}] {:08x} viewport{}: origin=({}, {}), extent={}x{}, depth={}..{}.",
                contractPlusOne,
                checksumPrefix,
                viewportIndex,
                viewport.TopLeftX,
                viewport.TopLeftY,
                viewport.Width,
                viewport.Height,
                viewport.MinDepth,
                viewport.MaxDepth);
        }

        for (std::size_t slot = 0; slot < shaderResources.size(); ++slot) {
            ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(shaderResources[slot]);
            if (!view) {
                continue;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC description{};
            view->GetDesc(&description);
            logCaptureView(
                contractPlusOne,
                checksumPrefix,
                "PS-t",
                static_cast<UINT>(slot),
                view.Get(),
                description.Format,
                static_cast<UINT>(description.ViewDimension),
                textureViewRange(description));
        }
        for (std::size_t slot = 0; slot < renderTargets.size(); ++slot) {
            ComPtr<ID3D11RenderTargetView> view;
            view.Attach(renderTargets[slot]);
            if (!view) {
                continue;
            }
            D3D11_RENDER_TARGET_VIEW_DESC description{};
            view->GetDesc(&description);
            logCaptureView(
                contractPlusOne,
                checksumPrefix,
                "OM-RT",
                static_cast<UINT>(slot),
                view.Get(),
                description.Format,
                static_cast<UINT>(description.ViewDimension),
                textureViewRange(description));
        }
        {
            ComPtr<ID3D11DepthStencilView> view;
            view.Attach(depthStencil);
            if (view) {
                D3D11_DEPTH_STENCIL_VIEW_DESC description{};
                view->GetDesc(&description);
                logCaptureView(
                    contractPlusOne,
                    checksumPrefix,
                    "OM-DS",
                    0,
                    view.Get(),
                    description.Format,
                    static_cast<UINT>(description.ViewDimension),
                    textureViewRange(description));
            }
        }
        completedCaptureProbes_.fetch_add(1, std::memory_order_relaxed);
    }

    ScopedReflectionFreeCapture Runtime::beginReflectionFreeCapture(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return {};
        }
        const auto worldCaptureReady = synchronizeWorldCaptureSession();
        const auto featureEnabled = environmentAcquisitionEnabled();
        const auto now = GetTickCount64();
        const auto diagnosticMayBeRequested = worldCaptureReady &&
            featureEnabled && !captureProbeSessionComplete_;
        const auto productionMayBeRequested = worldCaptureReady &&
            featureEnabled && !environmentUpdater_.snapshot().pending &&
            now >= nextEnvironmentCaptureTickMilliseconds_.load(
                       std::memory_order_acquire);
        if (!diagnosticMayBeRequested && !productionMayBeRequested) {
            return {};
        }

        ID3D11RenderTargetView* outputViewRaw{};
        context->OMGetRenderTargets(1, &outputViewRaw, nullptr);
        ComPtr<ID3D11RenderTargetView> outputView;
        outputView.Attach(outputViewRaw);
        if (!outputView) {
            return {};
        }
        ComPtr<ID3D11Resource> outputResource;
        outputView->GetResource(&outputResource);
        ComPtr<ID3D11Texture2D> outputTexture;
        if (!outputResource || FAILED(outputResource.As(&outputTexture)) ||
            !outputTexture) {
            return {};
        }
        D3D11_TEXTURE2D_DESC outputDescription{};
        outputTexture->GetDesc(&outputDescription);
        const auto readback = std::ranges::find_if(
            sceneRadianceReadbackSlots_,
            [&outputDescription](const auto& slot) {
                return slot.format == outputDescription.Format;
            });
        const auto diagnosticRequested = diagnosticMayBeRequested &&
            !captureProbeSessionComplete_ &&
            readback != sceneRadianceReadbackSlots_.end() &&
            !readback->reflectionFreeCaptureAttempted &&
            !readback->rollingReady && !readback->pending &&
            !readback->completed &&
            readback->rollingReflectionFreeTexture;
        const auto productionRequested = productionMayBeRequested &&
            outputDescription.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
            !environmentUpdater_.snapshot().pending;
        if (!diagnosticRequested && !productionRequested) {
            return {};
        }

        reflectionFreeCaptureDiagnosticReserved_ = diagnosticRequested;
        reflectionFreeCaptureProductionReserved_ = productionRequested;
        // Diagnostics retain full radiance. Alpha-to-coverage also needs the
        // native pixel shader's opacity; a constant mask cannot preserve it.
        // Ordinary acquisition keeps hardware geometry/depth/stencil coverage
        // and evaluates native lighting only at the cubemap taps.
        ComPtr<ID3D11BlendState> captureBlend;
        context->OMGetBlendState(&captureBlend, nullptr, nullptr);
        D3D11_BLEND_DESC captureBlendDescription{};
        if (captureBlend) captureBlend->GetDesc(&captureBlendDescription);
        reflectionFreeCaptureCompactReserved_ = productionRequested && !diagnosticRequested &&
            !captureBlendDescription.AlphaToCoverageEnable;
        if (diagnosticRequested) {
            readback->reflectionFreeCaptureAttempted = true;
        }
        if (productionRequested) {
            nextEnvironmentCaptureTickMilliseconds_.store(
                now + kEnvironmentCaptureCadenceMilliseconds,
                std::memory_order_release);
        }
        if (!reflectionFreeCaptureResources_.prepareScratch(
                outputView.Get(), reflectionFreeCaptureCompactReserved_)) {
            if (diagnosticRequested) {
                readback->completed = true;
                readback->failureLogged = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (diagnosticRequested ||
                !loggedReflectionFreeCaptureFailure_) {
                loggedReflectionFreeCaptureFailure_ = true;
                logging::warn(
                    "IBL reflection-free duplicate rejected an incompatible DFComposite output for format {}; requested diagnostic={}, provider update={}; both remain fail-closed.",
                    sceneProbeFormatName(outputDescription.Format),
                    diagnosticRequested,
                    productionRequested);
            }
            reflectionFreeCaptureDiagnosticReserved_ = false;
            reflectionFreeCaptureProductionReserved_ = false;
            return {};
        }

        reflectionFreeCaptureResources_.clearScratch(context);
        ScopedReflectionFreeCapture capture(
            context,
            reflectionFreeCaptureResources_,
            &reflectionFreeDrawTiming_);
        if (!capture.active()) {
            const auto rejection = capture.rejection();
            if (diagnosticRequested) {
                readback->completed = true;
                readback->failureLogged = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (diagnosticRequested ||
                !loggedReflectionFreeCaptureFailure_) {
                loggedReflectionFreeCaptureFailure_ = true;
                logging::warn(
                    "IBL reflection-free duplicate could not establish its exact fail-closed render-state transaction for format {} (reason={}, code={}, diagnostic={}, providerUpdate={}); no duplicate draw was issued.",
                    sceneProbeFormatName(outputDescription.Format),
                    reflectionFreeCaptureRejectionName(rejection),
                    static_cast<unsigned>(rejection),
                    diagnosticRequested,
                    productionRequested);
            }
            reflectionFreeCaptureDiagnosticReserved_ = false;
            reflectionFreeCaptureProductionReserved_ = false;
        }
        return capture;
    }

    void Runtime::onReflectionFreeCaptureDrawComplete(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne,
        bool stateRestored) noexcept
    {
        if (!context || context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return;
        }
        const auto diagnosticReserved =
            reflectionFreeCaptureDiagnosticReserved_;
        const auto productionReserved =
            reflectionFreeCaptureProductionReserved_;
        const auto compactReserved = reflectionFreeCaptureCompactReserved_;
        reflectionFreeCaptureCompactReserved_ = false;
        reflectionFreeCaptureDiagnosticReserved_ = false;
        reflectionFreeCaptureProductionReserved_ = false;
        if (!diagnosticReserved && !productionReserved) {
            return;
        }
        const auto& scratchDescription =
            reflectionFreeCaptureResources_.scratchDescription();
        const auto readback = std::ranges::find_if(
            sceneRadianceReadbackSlots_,
            [this](const auto& slot) {
                return slot.format == reflectionFreeCaptureResources_.sourceFormat();
            });
        if (!stateRestored) {
            if (diagnosticReserved &&
                readback != sceneRadianceReadbackSlots_.end()) {
                readback->completed = true;
                readback->failureLogged = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (diagnosticReserved ||
                !loggedReflectionFreeCaptureFailure_) {
                loggedReflectionFreeCaptureFailure_ = true;
                logging::warn(
                    "IBL reflection-free duplicate did not complete with exact state restoration for format {}; diagnostic and provider results were discarded.",
                    sceneProbeFormatName(scratchDescription.Format));
            }
            return;
        }

        if (diagnosticReserved &&
            readback != sceneRadianceReadbackSlots_.end() &&
            readback->reflectionFreeCaptureAttempted &&
            !readback->rollingReflectionFreeCopied && !readback->completed) {
            if (copySceneProbeSamples(
                    context,
                    reflectionFreeCaptureResources_.scratchTexture(),
                    readback->rollingReflectionFreeTexture.Get(),
                    scratchDescription)) {
                readback->rollingReflectionFreeCopied = true;
                logging::info(
                    "IBL reflection-free diagnostic DFComposite[{:02}] captured with t8/t14 neutralized and exact render-state restoration.",
                    contractPlusOne);
            } else {
                readback->completed = true;
                readback->failureLogged = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                logging::warn(
                    "IBL reflection-free diagnostic copy failed for format {}; the diagnostic result was discarded.",
                    sceneProbeFormatName(readback->format));
            }
        }

        if (productionReserved && environmentAcquisitionEnabled() &&
            reflectionFreeCaptureResources_.sourceFormat() == DXGI_FORMAT_R11G11B10_FLOAT &&
            (scratchDescription.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                (compactReserved && scratchDescription.Format == DXGI_FORMAT_R8_UNORM))) {
            ID3D11ShaderResourceView* depthRaw{};
            context->PSGetShaderResources(7, 1, &depthRaw);
            ComPtr<ID3D11ShaderResourceView> depth;
            depth.Attach(depthRaw);
            ID3D11Buffer* sceneConstantsRaw{};
            context->PSGetConstantBuffers(12, 1, &sceneConstantsRaw);
            ComPtr<ID3D11Buffer> sceneConstants;
            sceneConstants.Attach(sceneConstantsRaw);
            const auto useHistory = publishedEnvironmentSessionId_ ==
                    activeCaptureProbeSessionId_ &&
                environmentProvider_.publishedEnvironment() &&
                environmentProvider_.publishedValidity() &&
                environmentProvider_.publishedPosition();
            auto* radiance = reflectionFreeCaptureResources_.scratchShaderResource();
            if (compactReserved) {
                if (!compactCapture_.dispatch(context, radiance, contractPlusOne - 1)) {
                    if (!loggedReflectionFreeCaptureFailure_) {
                        loggedReflectionFreeCaptureFailure_ = true;
                        logging::warn("IBL native compact capture could not dispatch; retaining the last validated environment.");
                    }
                    return;
                }
                radiance = compactCapture_.radiance();
            }
            if (environmentUpdater_.dispatchUpdate(
                    context,
                    environmentProvider_,
                    radiance,
                    depth.Get(),
                    sceneConstants.Get(),
                    useHistory,
                    !useHistory || GetTickCount64() >= nextDiffuseFitMilliseconds_)) {
                if (!useHistory || GetTickCount64() >= nextDiffuseFitMilliseconds_)
                    nextDiffuseFitMilliseconds_ = GetTickCount64() + 1000;
                updateMaterialEnvironmentTransition(GetTickCount64());
                pendingEnvironmentUpdateSessionId_ =
                    activeCaptureProbeSessionId_;
                const auto update = environmentUpdater_.snapshot();
                if (update.dispatches == 1 ||
                    (update.dispatches % 300) == 0) {
                    logging::info(
                        "Dynamic Cubemaps stereo generation {} dispatched into the private radiance/validity/position back chain with position-aware history={} and bounded scene-linear GGX filtering; publication awaits nonblocking validation.",
                        update.generation,
                        useHistory);
                }
            } else if (!loggedEnvironmentUpdateFailure_) {
                loggedEnvironmentUpdateFailure_ = true;
                logging::warn(
                    "IBL stereo environment update rejected its exact capture inputs; the private generation was aborted and the previous validated pair remains selected.");
            }
        }
    }

    void Runtime::onCaptureProbeDrawComplete(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!environmentAcquisitionEnabled() || !context ||
            context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
            return;
        }

        ID3D11RenderTargetView* outputViewRaw{};
        context->OMGetRenderTargets(1, &outputViewRaw, nullptr);
        ComPtr<ID3D11RenderTargetView> outputView;
        outputView.Attach(outputViewRaw);
        if (!outputView) {
            return;
        }

        ComPtr<ID3D11Resource> outputResource;
        outputView->GetResource(&outputResource);
        ComPtr<ID3D11Texture2D> outputTexture;
        if (!outputResource || FAILED(outputResource.As(&outputTexture)) ||
            !outputTexture) {
            return;
        }

        D3D11_TEXTURE2D_DESC outputDescription{};
        outputTexture->GetDesc(&outputDescription);
        if (outputDescription.ArraySize != 1 ||
            outputDescription.MipLevels == 0 ||
            outputDescription.SampleDesc.Count != 1) {
            return;
        }

        const auto readback = std::ranges::find_if(
            sceneRadianceReadbackSlots_,
            [&outputDescription](const auto& slot) {
                return slot.format == outputDescription.Format;
            });
        if (readback == sceneRadianceReadbackSlots_.end() ||
            readback->rollingReady || readback->pending ||
            readback->completed ||
            !readback->reflectionFreeCaptureAttempted ||
            !readback->rollingReflectionFreeCopied ||
            !readback->rollingCompositeTexture ||
            !std::ranges::all_of(
                readback->rollingPixelShaderTextures,
                [](const auto& texture) { return texture != nullptr; })) {
            return;
        }

        const auto acquireCandidate = [](ID3D11ShaderResourceView* rawView,
                                          ComPtr<ID3D11Texture2D>& texture,
                                          D3D11_TEXTURE2D_DESC& description) {
            ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(rawView);
            if (!view) {
                return;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (resource && SUCCEEDED(resource.As(&texture)) && texture) {
                texture->GetDesc(&description);
            }
        };
        std::array<ComPtr<ID3D11Texture2D>, kSceneRadianceCandidateCount>
            candidateTextures{};
        std::array<D3D11_TEXTURE2D_DESC, kSceneRadianceCandidateCount>
            candidateDescriptions{};
        for (std::size_t index = 0;
             index < kSceneRadianceCandidateSlots.size();
             ++index) {
            ID3D11ShaderResourceView* candidateView{};
            context->PSGetShaderResources(
                kSceneRadianceCandidateSlots[index],
                1,
                &candidateView);
            acquireCandidate(
                candidateView,
                candidateTextures[index],
                candidateDescriptions[index]);
        }

        const auto candidateCompatible = [&outputDescription](
                                             ID3D11Texture2D* source,
                                             const D3D11_TEXTURE2D_DESC& description) {
            return source &&
                description.Width == outputDescription.Width &&
                description.Height == outputDescription.Height &&
                description.Format == outputDescription.Format &&
                description.ArraySize == 1 && description.MipLevels > 0 &&
                description.SampleDesc.Count == 1;
        };
        if (!copySceneProbeSamples(
                context,
                outputTexture.Get(),
                readback->rollingCompositeTexture.Get(),
                outputDescription)) {
            return;
        }
        for (std::size_t index = 0;
             index < kSceneRadianceCandidateSlots.size();
             ++index) {
            readback->rollingPixelShaderCopied[index] =
                candidateCompatible(
                    candidateTextures[index].Get(),
                    candidateDescriptions[index]) &&
                copySceneProbeSamples(
                    context,
                    candidateTextures[index].Get(),
                    readback->rollingPixelShaderTextures[index].Get(),
                    candidateDescriptions[index]);
        }

        const auto contractIndex = static_cast<std::size_t>(
            contractPlusOne - 1);
        const auto& contract = kCaptureProbeContracts[contractIndex];
        readback->sourceWidth = outputDescription.Width;
        readback->sourceHeight = outputDescription.Height;
        readback->contractPlusOne = contractPlusOne;
        readback->checksumPrefix =
            (static_cast<std::uint32_t>(contract.checksum[0]) << 24) |
            (static_cast<std::uint32_t>(contract.checksum[1]) << 16) |
            (static_cast<std::uint32_t>(contract.checksum[2]) << 8) |
            static_cast<std::uint32_t>(contract.checksum[3]);
        readback->rollingReady = true;
    }

    void Runtime::onCaptureProbePassComplete(
        ID3D11DeviceContext* context,
        std::uint16_t contractPlusOne) noexcept
    {
        if (!environmentAcquisitionEnabled() || !context ||
            context != context_.Get() || contractPlusOne == 0 ||
            contractPlusOne > kCaptureProbeContracts.size() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            !activateWorldCaptureProbeSession()) {
            return;
        }

        for (auto& readback : sceneRadianceReadbackSlots_) {
            if (!readback.rollingReady || readback.pending ||
                readback.completed || !readback.rollingCompositeTexture ||
                !readback.compositeTexture ||
                !readback.rollingReflectionFreeCopied ||
                !readback.rollingReflectionFreeTexture ||
                !readback.reflectionFreeTexture) {
                continue;
            }

            context->CopyResource(
                readback.reflectionFreeTexture.Get(),
                readback.rollingReflectionFreeTexture.Get());
            readback.reflectionFreeCopied = true;
            context->CopyResource(
                readback.compositeTexture.Get(),
                readback.rollingCompositeTexture.Get());
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                readback.pixelShaderCopied[index] =
                    readback.rollingPixelShaderCopied[index] &&
                    readback.rollingPixelShaderTextures[index] &&
                    readback.pixelShaderTextures[index];
                if (readback.pixelShaderCopied[index]) {
                    context->CopyResource(
                        readback.pixelShaderTextures[index].Get(),
                        readback.rollingPixelShaderTextures[index].Get());
                }
            }

            readback.rollingReady = false;
            readback.pendingPolls = 0;
            readback.pending = true;
            sceneRadianceProbeCaptures_.fetch_add(
                1,
                std::memory_order_relaxed);
        }
    }

    bool Runtime::createMaterialResources(
        CreatePixelShaderFunction createPixelShader) noexcept
    {
        if (!device_ || !createPixelShader) {
            return false;
        }

        const auto styleShader = loadEmbeddedShader(IDR_IBL_MATERIAL_STYLE_CS);
        if (!materialStyle_.initialize(device_.Get(), styleShader.data, styleShader.size)) {
            logging::error("Cubemap authored-style compute shader could not be initialized.");
            return false;
        }
        std::array<
            ComPtr<ID3D11PixelShader>,
            kCaptureProbeContracts.size()>
            replacements{};
        for (std::size_t index = 0;
             index < kIblMaterialShaderDefinitions.size();
             ++index) {
            const auto& definition = kIblMaterialShaderDefinitions[index];
            const auto embedded = loadEmbeddedShader(definition.resourceId);
            if (!embedded.data || embedded.size == 0 ||
                FAILED(createPixelShader(
                    device_.Get(),
                    embedded.data,
                    embedded.size,
                    nullptr,
                    &replacements[index])) ||
                !replacements[index]) {
                logging::error(
                    "IBL material replacement '{}' could not be loaded or created on the D3D11 device.",
                    definition.name);
                return false;
            }
        }

        D3D11_BUFFER_DESC disabledDescription{};
        disabledDescription.ByteWidth =
            sizeof(MaterialEnvironmentConstants);
        disabledDescription.Usage = D3D11_USAGE_IMMUTABLE;
        disabledDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        auto enabledDescription = disabledDescription;
        enabledDescription.Usage = D3D11_USAGE_DEFAULT;
        constexpr MaterialEnvironmentConstants disabled{};
        const MaterialEnvironmentConstants enabled{
            .iblWeight = 0.0F,
            .complexMaterialWeight = 1.0F,
            .transitionWeight = 1.0F,
            .pbr = pbr::makeFrameData(pbr::Settings{}),
        };
        D3D11_SUBRESOURCE_DATA disabledData{};
        disabledData.pSysMem = &disabled;
        D3D11_SUBRESOURCE_DATA enabledData{};
        enabledData.pSysMem = &enabled;
        ComPtr<ID3D11Buffer> disabledConstants;
        ComPtr<ID3D11Buffer> enabledConstants;
        if (FAILED(device_->CreateBuffer(
                &disabledDescription,
                &disabledData,
                &disabledConstants)) ||
            FAILED(device_->CreateBuffer(
                &enabledDescription,
                &enabledData,
                &enabledConstants)) ||
            !disabledConstants || !enabledConstants) {
            logging::error(
                "IBL/PBR material b5 disable and transition constants could not be created.");
            return false;
        }

        materialReplacementShaders_ = std::move(replacements);
        materialDisabledConstants_ = std::move(disabledConstants);
        materialEnabledConstants_ = std::move(enabledConstants);
        return true;
    }

    bool Runtime::createResources(
        CreatePixelShaderFunction createPixelShader) noexcept
    {
        if (!createMaterialResources(createPixelShader)) {
            return false;
        }
        const auto coverage = loadEmbeddedShader(IDR_IBL_CAPTURE_COVERAGE_PS);
        if (!coverage.data || FAILED(createPixelShader(device_.Get(), coverage.data, coverage.size, nullptr, &coverageShader_))) return false;
        std::array<CompactCapture::Bytecode, CompactCapture::kShaderCount> compactShaders{};
        for (std::size_t index = 0; index < compactShaders.size(); ++index) {
            const auto code = loadEmbeddedShader(IDR_IBL_COMPACT_CAPTURE_FIRST_CS + static_cast<int>(index));
            compactShaders[index] = {code.data, code.size};
        }
        if (!compactCapture_.initialize(device_.Get(), compactShaders, 128)) return false;
        const auto compactUpdate = loadEmbeddedShader(IDR_IBL_COMPACT_UPDATE_CS);
        const auto updateEmbedded = loadEmbeddedShader(
            IDR_IBL_ENVIRONMENT_UPDATE_CS);
        const auto filterEmbedded = loadEmbeddedShader(
            IDR_IBL_ENVIRONMENT_FILTER_CS);
        const auto reductionEmbedded = loadEmbeddedShader(
            IDR_IBL_ENVIRONMENT_REDUCTION_CS);
        if (!updateEmbedded.data || updateEmbedded.size < 20 ||
            std::memcmp(updateEmbedded.data, "DXBC", 4) != 0 ||
            !filterEmbedded.data || filterEmbedded.size < 20 ||
            std::memcmp(filterEmbedded.data, "DXBC", 4) != 0 ||
            !environmentUpdater_.initialize(
                device_.Get(),
                updateEmbedded.data,
                updateEmbedded.size,
                filterEmbedded.data,
                filterEmbedded.size,
                reductionEmbedded.data,
                reductionEmbedded.size,
                128, compactUpdate.data, compactUpdate.size)) {
            return false;
        }

        if (!reflectionFreeCaptureResources_.initialize(device_.Get()) ||
            !createSceneRadianceProbeResources()) {
            return false;
        }
        if (!environmentProvider_.initialize(device_.Get(), 128)) {
            logging::warn(
                "IBL transactional environment provider could not allocate its paired R11G11B10_FLOAT/R32_FLOAT cube chains; capture diagnostics remain available and visual integration remains fail-closed.");
        }
        return true;
    }

    bool Runtime::createSceneRadianceProbeResources() noexcept
    {
        for (std::size_t index = 0;
             index < sceneRadianceReadbackSlots_.size();
             ++index) {
            auto& slot = sceneRadianceReadbackSlots_[index];
            slot = {};
            slot.format = kSceneRadianceProbeFormats[index];

            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(kSceneProbeSampleCount);
            description.Height = 1;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = slot.format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            for (auto& texture : slot.rollingPixelShaderTextures) {
                if (FAILED(device_->CreateTexture2D(
                        &description,
                        nullptr,
                        &texture))) {
                    return false;
                }
            }
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    &slot.rollingCompositeTexture))) {
                return false;
            }
            auto reflectionDescription = description;
            reflectionDescription.Format = ReflectionFreeCaptureResources::captureFormat(slot.format);
            if (FAILED(device_->CreateTexture2D(
                    &reflectionDescription, nullptr, &slot.rollingReflectionFreeTexture))) {
                return false;
            }

            description.Usage = D3D11_USAGE_STAGING;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (auto& texture : slot.pixelShaderTextures) {
                if (FAILED(device_->CreateTexture2D(
                        &description,
                        nullptr,
                        &texture))) {
                    return false;
                }
            }
            if (FAILED(device_->CreateTexture2D(
                    &description,
                    nullptr,
                    &slot.compositeTexture))) {
                return false;
            }
            reflectionDescription.Usage = D3D11_USAGE_STAGING;
            reflectionDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(
                    &reflectionDescription,
                    nullptr,
                    &slot.reflectionFreeTexture))) {
                return false;
            }
        }
        return true;
    }

    void Runtime::consumeSceneRadianceProbeReadbacks() noexcept
    {
        for (auto& slot : sceneRadianceReadbackSlots_) {
            if (!slot.pending) {
                continue;
            }

            std::array<
                std::array<SceneProbeRgb, kSceneProbeSampleCount>,
                kSceneRadianceCandidateCount>
                pixelShaderCandidates{};
            std::array<SceneProbeRgb, kSceneProbeSampleCount> composite{};
            std::array<SceneProbeRgb, kSceneProbeSampleCount>
                reflectionFree{};
            std::array<SceneProbeReadResult, kSceneRadianceCandidateCount>
                candidateResults{};
            candidateResults.fill(SceneProbeReadResult::ready);
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                if (slot.pixelShaderCopied[index]) {
                    candidateResults[index] = readSceneProbeSamples(
                        context_.Get(),
                        slot.pixelShaderTextures[index].Get(),
                        slot.format,
                        pixelShaderCandidates[index]);
                }
            }
            const auto compositeResult = readSceneProbeSamples(
                context_.Get(),
                slot.compositeTexture.Get(),
                slot.format,
                composite);
            const auto reflectionFreeResult = slot.reflectionFreeCopied ?
                readSceneProbeSamples(
                    context_.Get(),
                    slot.reflectionFreeTexture.Get(),
                    ReflectionFreeCaptureResources::captureFormat(slot.format),
                    reflectionFree) :
                SceneProbeReadResult::failed;
            if (std::ranges::find(
                    candidateResults,
                    SceneProbeReadResult::pending) !=
                    candidateResults.end() ||
                compositeResult == SceneProbeReadResult::pending ||
                reflectionFreeResult == SceneProbeReadResult::pending) {
                ++slot.pendingPolls;
                if (slot.pendingPolls <
                    kSceneRadianceMaximumReadbackPolls) {
                    continue;
                }
                slot.pending = false;
                slot.completed = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (!slot.failureLogged) {
                    slot.failureLogged = true;
                    logging::warn(
                        "IBL scene-radiance boundary readback timed out after {} nonblocking polls for format {}; the diagnostic remains fail-closed.",
                        slot.pendingPolls,
                        sceneProbeFormatName(slot.format));
                }
                continue;
            }
            if (std::ranges::find(
                    candidateResults,
                    SceneProbeReadResult::failed) !=
                    candidateResults.end() ||
                compositeResult == SceneProbeReadResult::failed ||
                reflectionFreeResult == SceneProbeReadResult::failed) {
                slot.pending = false;
                slot.completed = true;
                sceneRadianceProbeFailures_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (!slot.failureLogged) {
                    slot.failureLogged = true;
                    logging::warn(
                        "IBL scene-radiance boundary readback failed for format {}; the diagnostic remains fail-closed.",
                        sceneProbeFormatName(slot.format));
                }
                continue;
            }

            const auto compositeSummary = summarizeSceneProbe(composite);
            const auto reflectionFreeSummary =
                summarizeSceneProbe(reflectionFree);
            logging::info(
                "IBL scene-radiance pass-end final-draw snapshot DFComposite[{:02}] {:08x}: format={}, packedExtent={}x{}, samples={}; image unchanged.",
                slot.contractPlusOne,
                slot.checksumPrefix,
                sceneProbeFormatName(slot.format),
                slot.sourceWidth,
                slot.sourceHeight,
                kSceneProbeSampleCount);
            const auto logPixelShaderCandidate = [&composite, &reflectionFree](
                                                     UINT shaderSlot,
                                                     bool copied,
                                                     const auto& samples) {
                if (!copied) {
                    logging::info(
                        "IBL scene-radiance PS-t{}: no same-format full-resolution candidate was bound at this boundary.",
                        shaderSlot);
                    return;
                }
                const auto candidateSummary = summarizeSceneProbe(samples);
                logging::info(
                    "IBL scene-radiance PS-t{}: avg=({}, {}, {}), left=({}, {}, {}), right=({}, {}, {}), peak={}, nonBlack={}/{}, meanAbsDeltaToComposite={}, meanAbsDeltaToReflectionFree={}.",
                    shaderSlot,
                    candidateSummary.average.red,
                    candidateSummary.average.green,
                    candidateSummary.average.blue,
                    candidateSummary.leftEyeAverage.red,
                    candidateSummary.leftEyeAverage.green,
                    candidateSummary.leftEyeAverage.blue,
                    candidateSummary.rightEyeAverage.red,
                    candidateSummary.rightEyeAverage.green,
                    candidateSummary.rightEyeAverage.blue,
                    candidateSummary.peak,
                    candidateSummary.nonBlackSamples,
                    candidateSummary.validSamples,
                    meanAbsoluteSceneProbeDifference(samples, composite),
                    meanAbsoluteSceneProbeDifference(
                        samples,
                        reflectionFree));
            };
            for (std::size_t index = 0;
                 index < kSceneRadianceCandidateSlots.size();
                 ++index) {
                logPixelShaderCandidate(
                    kSceneRadianceCandidateSlots[index],
                    slot.pixelShaderCopied[index],
                    pixelShaderCandidates[index]);
            }
            logging::info(
                "IBL scene-radiance reflection-free split: avg=({}, {}, {}), left=({}, {}, {}), right=({}, {}, {}), peak={}, nonBlack={}/{}, meanAbsDeltaToComposite={}.",
                reflectionFreeSummary.average.red,
                reflectionFreeSummary.average.green,
                reflectionFreeSummary.average.blue,
                reflectionFreeSummary.leftEyeAverage.red,
                reflectionFreeSummary.leftEyeAverage.green,
                reflectionFreeSummary.leftEyeAverage.blue,
                reflectionFreeSummary.rightEyeAverage.red,
                reflectionFreeSummary.rightEyeAverage.green,
                reflectionFreeSummary.rightEyeAverage.blue,
                reflectionFreeSummary.peak,
                reflectionFreeSummary.nonBlackSamples,
                reflectionFreeSummary.validSamples,
                meanAbsoluteSceneProbeDifference(
                    reflectionFree,
                    composite));
            logging::info(
                "IBL scene-radiance OM-composite: avg=({}, {}, {}), left=({}, {}, {}), right=({}, {}, {}), peak={}, nonBlack={}/{}.",
                compositeSummary.average.red,
                compositeSummary.average.green,
                compositeSummary.average.blue,
                compositeSummary.leftEyeAverage.red,
                compositeSummary.leftEyeAverage.green,
                compositeSummary.leftEyeAverage.blue,
                compositeSummary.rightEyeAverage.red,
                compositeSummary.rightEyeAverage.green,
                compositeSummary.rightEyeAverage.blue,
                compositeSummary.peak,
                compositeSummary.nonBlackSamples,
                compositeSummary.validSamples);

            slot.pending = false;
            slot.completed = true;
            sceneRadianceProbeReadbacks_.fetch_add(
                1,
                std::memory_order_relaxed);
        }
        if (std::ranges::all_of(
                sceneRadianceReadbackSlots_,
                [](const auto& slot) { return slot.completed; })) {
            captureProbeSessionComplete_ = true;
        }
    }

    void Runtime::onDFLightAmbientBind(ID3D11DeviceContext* context) noexcept
    {
        if (!context || context != context_.Get() ||
            !resourcesReady_.load(std::memory_order_acquire) ||
            (!environmentAcquisitionEnabled() &&
                !pbrEnabled_.load(std::memory_order_acquire))) {
            return;
        }

        if (materialResponseEnabled()) {
            std::array<ID3D11ShaderResourceView*, 3> materialViews{};
            context->PSGetShaderResources(
                0,
                static_cast<UINT>(materialViews.size()),
                materialViews.data());
            ComPtr<ID3D11ShaderResourceView> albedo;
            ComPtr<ID3D11ShaderResourceView> materialProperties;
            albedo.Attach(materialViews[0]);
            if (materialViews[1]) {
                materialViews[1]->Release();
            }
            materialProperties.Attach(materialViews[2]);
            if (albedo) {
                D3D11_SHADER_RESOURCE_VIEW_DESC description{};
                albedo->GetDesc(&description);
                if (description.ViewDimension ==
                    D3D11_SRV_DIMENSION_TEXTURE2D) {
                    materialAlbedo_ = std::move(albedo);
                }
            }
            if (pbrEnabled_.load(std::memory_order_acquire) &&
                materialProperties) {
                D3D11_SHADER_RESOURCE_VIEW_DESC description{};
                materialProperties->GetDesc(&description);
                if (description.ViewDimension ==
                    D3D11_SRV_DIMENSION_TEXTURE2D) {
                    materialProperties_ = std::move(materialProperties);
                }
            }
        }

        const auto now = GetTickCount64();
        updateMaterialEnvironmentTransition(now);
        if (!environmentAcquisitionEnabled()) {
            return;
        }
        if (now >= nextCadenceTickMilliseconds_) {
            nextCadenceTickMilliseconds_ = now + kRuntimePollCadenceMilliseconds;
            cadenceTicks_.fetch_add(1, std::memory_order_relaxed);
            consumeSceneRadianceProbeReadbacks();
        }
        // Poll publication at render cadence, independently of diagnostic readbacks.
        const auto environmentReadback =
            environmentUpdater_.consumeUpdate(context, environmentProvider_);
        if (environmentReadback ==
            EnvironmentUpdateConsumeResult::completed) {
            const auto update = environmentUpdater_.snapshot();
            publishedEnvironmentSessionId_ =
                pendingEnvironmentUpdateSessionId_;
            pendingEnvironmentUpdateSessionId_ = 0;
            beginMaterialEnvironmentTransition(now);
            const auto publicationAction = update.diffuseUpdated ? chooseDiffusePublicationAction(
                update.diffuseSHState,
                publishedUsable_.load(std::memory_order_relaxed),
                publishedDiffuseSessionId_.load(
                    std::memory_order_relaxed),
                publishedEnvironmentSessionId_) : DiffusePublicationAction::retain;
            if (publicationAction == DiffusePublicationAction::publish) {
                publishUsable(
                    update.diffuseSH,
                    update.faceAverageValidity,
                    update.diffuseSHCoverage,
                    update.generation,
                    publishedEnvironmentSessionId_,
                    now);
                diffuseFitsPublished_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                if (!loggedFirstUsableDiffuseFit_) {
                    loggedFirstUsableDiffuseFit_ = true;
                    logging::info(
                        "IBL first validity-aware diffuse SH published with directional vanilla fallback: generation={}, solid-angle coverage={}, face confidence=[{},{},{},{},{},{}], L0 RGB=({}, {}, {}).",
                        update.generation,
                        update.diffuseSHCoverage,
                        update.faceAverageValidity[0],
                        update.faceAverageValidity[1],
                        update.faceAverageValidity[2],
                        update.faceAverageValidity[3],
                        update.faceAverageValidity[4],
                        update.faceAverageValidity[5],
                        update.diffuseSH.rgb[0][0],
                        update.diffuseSH.rgb[1][0],
                        update.diffuseSH.rgb[2][0]);
                }
            } else if (
                publicationAction == DiffusePublicationAction::clear) {
                publishUnavailable(
                    update.faceAverageValidity,
                    update.diffuseSHCoverage,
                    update.generation,
                    publishedEnvironmentSessionId_,
                    now);
                diffuseFitsRejected_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            } else if (
                update.diffuseUpdated && publicationAction == DiffusePublicationAction::retain) {
                diffuseFitsRejected_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (lastLoggedEnvironmentUpdateGeneration_ == 0 ||
                update.generation >=
                    lastLoggedEnvironmentUpdateGeneration_ + 300) {
                lastLoggedEnvironmentUpdateGeneration_ =
                    update.generation;
                logging::info(
                    "Dynamic Cubemaps stereo generation {} atomically published for world session {} as one radiance/validity/position environment: positionAwareHistory={}, probeOriginValid={}, probeOrigin=({}, {}, {}), avg=({}, {}, {}), peak={}, validity={}, covered={}/{}, nonBlack={}/{}, diffuseCoverage={}, diffuseState={}, faceValidity=[{},{},{},{},{},{}], faceLuminance=[{},{},{},{},{},{}]; enabled specular consumers use receiver-position parallax with per-direction vanilla fallback and Diffuse IBL shares the validated generation.",
                    update.generation,
                    publishedEnvironmentSessionId_,
                    update.historyUsed,
                    update.probeOrigin.valid,
                    update.probeOrigin.position.x,
                    update.probeOrigin.position.y,
                    update.probeOrigin.position.z,
                    update.average.x,
                    update.average.y,
                    update.average.z,
                    update.peak,
                    update.averageValidity,
                    update.coveredSamples,
                    update.sampleCount,
                    update.nonBlackSamples,
                    update.sampleCount,
                    update.diffuseSHCoverage,
                    static_cast<unsigned>(update.diffuseSHState),
                    update.faceAverageValidity[0],
                    update.faceAverageValidity[1],
                    update.faceAverageValidity[2],
                    update.faceAverageValidity[3],
                    update.faceAverageValidity[4],
                    update.faceAverageValidity[5],
                    update.faceAverageLuminance[0],
                    update.faceAverageLuminance[1],
                    update.faceAverageLuminance[2],
                    update.faceAverageLuminance[3],
                    update.faceAverageLuminance[4],
                    update.faceAverageLuminance[5]);
            }
        } else if (environmentReadback ==
            EnvironmentUpdateConsumeResult::failed) {
            pendingEnvironmentUpdateSessionId_ = 0;
            if (!loggedEnvironmentUpdateFailure_) {
                loggedEnvironmentUpdateFailure_ = true;
                logging::warn(
                    "IBL stereo environment validation failed; the private generation was aborted and the previous validated pair remains published.");
            }
        }
        if (!loggedFirstDiffuseApplication_) {
            const auto geometry = render::geometryHookSnapshot();
            if (geometry.diffuseAmbientPrepared >
                diffuseApplicationBaseline_) {
                loggedFirstDiffuseApplication_ = true;
                logging::info(
                    "Diffuse IBL first continuous ambient transform applied: generation={}, solid-angle coverage={}, maximum coefficient delta={}.",
                    geometry.latestDiffuseGeneration,
                    geometry.latestDiffuseCoverage,
                    geometry.latestDiffuseMaximumCoefficientDelta);
            }
        }
    }

    bool Runtime::complexMaterialConsumptionReady() const noexcept
    {
        const auto requestedSession =
            requestedCaptureProbeSessionId_.load(
                std::memory_order_acquire);
        const auto dynamicReady =
            enabled_.load(std::memory_order_acquire) &&
            dynamicCubemapsEnabled_.load(std::memory_order_acquire) &&
            publishedEnvironmentSessionId_ != 0 &&
            publishedEnvironmentSessionId_ == requestedSession &&
            environmentProvider_.publishedEnvironment() &&
            environmentProvider_.publishedValidity();
        return resourcesReady_.load(std::memory_order_acquire) &&
            !materialConsumptionFailed_ &&
            (dynamicReady || pbrEnabled_.load(std::memory_order_acquire));
    }

    void Runtime::publishUsable(
        const DiffuseSH& coefficients,
        const std::array<float, kEnvironmentCubeFaceCount>&
            cubeFaceConfidence,
        float coverage,
        std::uint64_t generation,
        std::uint64_t session,
        std::uint64_t tickMilliseconds) noexcept
    {
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        std::size_t index{};
        for (const auto& channel : coefficients.rgb) {
            for (const auto coefficient : channel) {
                publishedCoefficientBits_[index++].store(
                    std::bit_cast<std::uint32_t>(coefficient),
                    std::memory_order_relaxed);
            }
        }
        for (std::size_t face = 0; face < cubeFaceConfidence.size(); ++face) {
            publishedFaceConfidenceBits_[face].store(
                std::bit_cast<std::uint32_t>(cubeFaceConfidence[face]),
                std::memory_order_relaxed);
        }
        diffuseSHCoverageBits_.store(
            std::bit_cast<std::uint32_t>(coverage),
            std::memory_order_relaxed);
        publishedGeneration_.store(generation, std::memory_order_relaxed);
        publishedDiffuseSessionId_.store(
            session,
            std::memory_order_relaxed);
        publishedTickMilliseconds_.store(
            tickMilliseconds,
            std::memory_order_relaxed);
        publishedUsable_.store(true, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::publishUnavailable(
        const std::array<float, kEnvironmentCubeFaceCount>&
            cubeFaceConfidence,
        float coverage,
        std::uint64_t generation,
        std::uint64_t session,
        std::uint64_t tickMilliseconds) noexcept
    {
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        for (std::size_t face = 0; face < cubeFaceConfidence.size(); ++face) {
            publishedFaceConfidenceBits_[face].store(
                std::bit_cast<std::uint32_t>(cubeFaceConfidence[face]),
                std::memory_order_relaxed);
        }
        diffuseSHCoverageBits_.store(
            std::bit_cast<std::uint32_t>(coverage),
            std::memory_order_relaxed);
        publishedGeneration_.store(generation, std::memory_order_relaxed);
        publishedDiffuseSessionId_.store(
            session,
            std::memory_order_relaxed);
        publishedTickMilliseconds_.store(
            tickMilliseconds,
            std::memory_order_relaxed);
        publishedUsable_.store(false, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        RuntimeSnapshot result{
            .enabled = enabled_.load(std::memory_order_acquire),
            .dynamicCubemapsEnabled = dynamicCubemapsEnabled_.load(
                std::memory_order_acquire),
            .diffuseEnabled = diffuseEnabled_.load(
                std::memory_order_acquire),
            .resourcesReady = resourcesReady_.load(std::memory_order_acquire),
            .diffuseLevel = std::bit_cast<float>(
                diffuseLevelBits_.load(std::memory_order_acquire)),
            .diffuseSHCoverage = std::bit_cast<float>(
                diffuseSHCoverageBits_.load(std::memory_order_acquire)),
            .cadenceTicks = cadenceTicks_.load(std::memory_order_acquire),
            .diffuseFitsPublished = diffuseFitsPublished_.load(
                std::memory_order_acquire),
            .diffuseFitsRejected = diffuseFitsRejected_.load(
                std::memory_order_acquire),
            .matchingCaptureShaders = matchingCaptureShaders_.load(
                std::memory_order_acquire),
            .completedCaptureProbes = completedCaptureProbes_.load(
                std::memory_order_acquire),
            .sceneRadianceProbeCaptures =
                sceneRadianceProbeCaptures_.load(std::memory_order_acquire),
            .sceneRadianceProbeReadbacks =
                sceneRadianceProbeReadbacks_.load(std::memory_order_acquire),
            .sceneRadianceProbeFailures =
                sceneRadianceProbeFailures_.load(std::memory_order_acquire),
            .materialReplacementBinds =
                materialReplacementBinds_.load(std::memory_order_acquire),
            .materialBindingFailures =
                materialBindingFailures_.load(std::memory_order_acquire),
        };
        constexpr std::uint32_t kMaximumSnapshotAttempts = 3;
        for (std::uint32_t attempt = 0; attempt < kMaximumSnapshotAttempts;
             ++attempt) {
            const auto before = publishedSequence_.load(
                std::memory_order_acquire);
            if ((before & 1u) != 0) {
                continue;
            }
            DiffuseSH coefficients{};
            std::size_t index{};
            for (auto& channel : coefficients.rgb) {
                for (auto& coefficient : channel) {
                    coefficient = std::bit_cast<float>(
                        publishedCoefficientBits_[index++].load(
                            std::memory_order_relaxed));
                }
            }
            std::array<float, kEnvironmentCubeFaceCount> faceConfidence{};
            for (std::size_t face = 0; face < faceConfidence.size(); ++face) {
                faceConfidence[face] = std::bit_cast<float>(
                    publishedFaceConfidenceBits_[face].load(
                        std::memory_order_relaxed));
            }
            const auto usable = publishedUsable_.load(
                std::memory_order_relaxed);
            const auto generation = publishedGeneration_.load(
                std::memory_order_relaxed);
            const auto session = publishedDiffuseSessionId_.load(
                std::memory_order_relaxed);
            const auto tickMilliseconds = publishedTickMilliseconds_.load(
                std::memory_order_relaxed);
            const auto after = publishedSequence_.load(
                std::memory_order_acquire);
            if (before == after && (after & 1u) == 0) {
                result.diffuseSHUsable = usable && session != 0 &&
                    session == requestedCaptureProbeSessionId_.load(
                                   std::memory_order_acquire);
                result.latestSampleGeneration = generation;
                result.latestSampleTickMilliseconds = tickMilliseconds;
                result.latestDiffuseSH = coefficients;
                result.latestDiffuseFaceConfidence = faceConfidence;
                break;
            }
        }
        return result;
    }

    void Runtime::resetCaptureProbes() noexcept
    {
        try {
            const std::scoped_lock lock(captureShaderMutex_);
            for (auto& slot : captureShaderSlots_) {
                slot.shader.store(nullptr, std::memory_order_release);
                slot.contractPlusOne.store(0, std::memory_order_relaxed);
                slot.owner.Reset();
            }
        } catch (...) {
            logging::warn(
                "IBL capture shader registry reset failed; diagnostics remain fail-closed.");
        }
        for (auto& logged : captureProbeLogged_) {
            logged.store(false, std::memory_order_relaxed);
        }
        matchingCaptureShaders_.store(0, std::memory_order_relaxed);
        completedCaptureProbes_.store(0, std::memory_order_relaxed);
        captureRegistryOverflowLogged_.store(false, std::memory_order_relaxed);
    }

    void Runtime::resetResources() noexcept
    {
        resetCaptureProbes();
        resourcesReady_.store(false, std::memory_order_release);
        for (auto& slot : sceneRadianceReadbackSlots_) {
            slot = {};
        }
        environmentProvider_.reset();
        environmentUpdater_.reset();
        materialStyle_.reset();
        nextDiffuseFitMilliseconds_ = 0;
        reflectionFreeCaptureResources_.reset();
        compactCapture_.reset(); coverageShader_.Reset();
        reflectionFreeCaptureCompactReserved_ = false;
        reflectionFreeDrawTiming_.reset();
        for (auto& replacement : materialReplacementShaders_) {
            replacement.Reset();
        }
        materialDisabledConstants_.Reset();
        materialEnabledConstants_.Reset();
        materialAlbedo_.Reset();
        materialProperties_.Reset();
        context_.Reset();
        device_.Reset();
        nextCadenceTickMilliseconds_ = 0;
        loggedFirstUsableDiffuseFit_ = false;
        loggedFirstDiffuseApplication_ = false;
        diffuseApplicationBaseline_ =
            render::geometryHookSnapshot().diffuseAmbientPrepared;
        cadenceTicks_.store(0, std::memory_order_relaxed);
        diffuseFitsPublished_.store(0, std::memory_order_relaxed);
        diffuseFitsRejected_.store(0, std::memory_order_relaxed);
        diffuseSHCoverageBits_.store(0, std::memory_order_relaxed);
        sceneRadianceProbeCaptures_.store(0, std::memory_order_relaxed);
        sceneRadianceProbeReadbacks_.store(0, std::memory_order_relaxed);
        sceneRadianceProbeFailures_.store(0, std::memory_order_relaxed);
        materialReplacementBinds_.store(0, std::memory_order_relaxed);
        materialBindingFailures_.store(0, std::memory_order_relaxed);
        requestedCaptureProbeSessionId_.store(0, std::memory_order_relaxed);
        requestedCaptureProbeEarliestTickMilliseconds_.store(
            0,
            std::memory_order_relaxed);
        activeCaptureProbeSessionId_ = 0;
        activeCaptureProbeEarliestTickMilliseconds_ = 0;
        pendingEnvironmentUpdateSessionId_ = 0;
        publishedEnvironmentSessionId_ = 0;
        nextEnvironmentCaptureTickMilliseconds_.store(
            0,
            std::memory_order_relaxed);
        lastLoggedEnvironmentUpdateGeneration_ = 0;
        captureProbeSessionComplete_ = true;
        reflectionFreeCaptureDiagnosticReserved_ = false;
        reflectionFreeCaptureProductionReserved_ = false;
        loggedReflectionFreeCaptureFailure_ = false;
        loggedEnvironmentUpdateFailure_ = false;
        loggedFirstMaterialBind_ = false;
        loggedMaterialBindingFailure_ = false;
        materialConsumptionFailed_ = false;
        materialEnvironmentTransitionActive_ = false;
        materialEnvironmentTransitionWeight_ = 1.0f;
        uploadedPbrSettingsRevision_ = 0;
        materialEnvironmentTransitionStartMilliseconds_ = 0;
        nextMaterialEnvironmentTransitionTickMilliseconds_ = 0;
        publishedSequence_.fetch_add(1, std::memory_order_acq_rel);
        publishedUsable_.store(false, std::memory_order_relaxed);
        publishedGeneration_.store(0, std::memory_order_relaxed);
        publishedDiffuseSessionId_.store(0, std::memory_order_relaxed);
        publishedTickMilliseconds_.store(0, std::memory_order_relaxed);
        publishedSequence_.fetch_add(1, std::memory_order_release);
    }
}
