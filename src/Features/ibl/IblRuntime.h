#pragma once

#include "Features/ibl/IblCaptureProbeModel.h"
#include "Features/ibl/IblCompactCapture.h"
#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblEnvironmentUpdater.h"
#include "Features/ibl/IblMaterialBindingScope.h"
#include "Features/ibl/IblMaterialStyle.h"
#include "Features/ibl/IblProjectionModel.h"
#include "Features/ibl/IblReflectionFreeCapture.h"
#include "Features/ibl/IblSceneRadianceProbeModel.h"
#include "Features/ibl/IblSettings.h"
#include "Features/pbr/PbrSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <mutex>

namespace csah::ibl
{
    struct DiffuseAmbientSample
    {
        DiffuseSH coefficients{};
        std::array<float, kEnvironmentCubeFaceCount> cubeFaceConfidence{};
        float coverage{};
        float level{ 1.0f };
        std::uint64_t generation{};
    };

    struct SslrEnvironmentView
    {
        ID3D11ShaderResourceView* publishedEnvironment{};
        ID3D11ShaderResourceView* publishedValidity{};
        ID3D11ShaderResourceView* publishedPosition{};
        ID3D11ShaderResourceView* previousEnvironment{};
        ID3D11ShaderResourceView* previousValidity{};
        ID3D11ShaderResourceView* previousPosition{};
        EnvironmentProbeOrigin publishedProbeOrigin{};
        EnvironmentProbeOrigin previousProbeOrigin{};
        float transitionWeight{ 1.0f };
        bool publishedAvailable{};
        bool previousAvailable{};
    };

    struct RuntimeSnapshot
    {
        bool enabled{};
        bool dynamicCubemapsEnabled{};
        bool diffuseEnabled{};
        bool resourcesReady{};
        bool diffuseSHUsable{};
        float diffuseLevel{ 1.0f };
        float diffuseSHCoverage{};
        std::uint64_t cadenceTicks{};
        std::uint64_t diffuseFitsPublished{};
        std::uint64_t diffuseFitsRejected{};
        std::uint64_t matchingCaptureShaders{};
        std::uint64_t completedCaptureProbes{};
        std::uint64_t sceneRadianceProbeCaptures{};
        std::uint64_t sceneRadianceProbeReadbacks{};
        std::uint64_t sceneRadianceProbeFailures{};
        std::uint64_t materialReplacementBinds{};
        std::uint64_t materialBindingFailures{};
        std::uint64_t latestSampleGeneration{};
        std::uint64_t latestSampleTickMilliseconds{};
        DiffuseSH latestDiffuseSH{};
        std::array<float, kEnvironmentCubeFaceCount>
            latestDiffuseFaceConfidence{};
    };

    class Runtime
    {
    public:
        using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**);

        struct MaterialShaderBinding
        {
            ID3D11PixelShader* original{};
            ID3D11PixelShader* replacement{};
            std::uint16_t contractPlusOne{};

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return original && replacement && contractPlusOne != 0;
            }
        };

        struct MaterialPixelShaderSelection
        {
            ID3D11PixelShader* shader{};
            MaterialShaderBinding binding{};
        };

        static Runtime& get() noexcept;

        // Thread-safe feature control. Disabling immediately restores the
        // exact vanilla DFComposite selection path. Re-enabling requests a
        // fresh world capture before material consumption can resume.
        void setEnabled(bool enabled) noexcept;
        void setDynamicCubemapsEnabled(bool enabled) noexcept;
        void setDiffuseEnabled(bool enabled) noexcept;
        void setDiffuseLevel(float level) noexcept;
        // Vanilla Fixes SSLR is a read-only consumer of the shared
        // environment. It may keep acquisition active without enabling IBL
        // material substitution or diffuse ambient lighting.
        void setSslrConsumerEnabled(bool enabled) noexcept;
        void applySettings(const Settings& settings) noexcept;
        void applyPbrSettings(const pbr::Settings& settings) noexcept;

        // Render-thread-only immutable view of the currently published
        // provider generation. Raw pointers remain owned by this runtime.
        [[nodiscard]] bool tryGetSslrEnvironment(
            SslrEnvironmentView& view) noexcept;

        // Allocation-free DFLight hot-path read. Publication is seqlocked and
        // valid only for the current world capture session. Environment
        // refreshes never disable diffuse IBL in the visible scene.
        [[nodiscard]] bool tryGetDiffuseAmbient(
            DiffuseAmbientSample& sample) const noexcept;

        // Render-thread only. The device/context are retained for the process
        // lifetime; no engine pointer is retained by this subsystem.
        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* immediateContext,
            CreatePixelShaderFunction createPixelShader) noexcept;

        // May be called from the game-message thread. The render thread
        // consumes the request and waits for the world to settle before any
        // diagnostic capture. This lifetime is deliberately independent of
        // the Linear Lighting qualification window.
        void beginWorldCaptureProbeSession() noexcept;
        void loadMaterialReference() noexcept { materialStyle_.loadReference(); }

        // Creation-time identity registration is bounded and retains only
        // exact local-FXP matches. Draw-time lookup is allocation-free.
        void onPixelShaderCreated(
            const void* bytecode,
            std::size_t bytecodeSize,
            ID3D11PixelShader* shader) noexcept;

        [[nodiscard]] CaptureProbeShaderBinding captureProbeBindingForShader(
            ID3D11PixelShader* shader) const noexcept;

        // Exact-identity material replacement becomes selectable only after
        // the provider atomically publishes both radiance and validity.
        // The returned raw pointers remain owned by this runtime and by the
        // creation-time capture registry for the device lifetime.
        [[nodiscard]] MaterialPixelShaderSelection selectMaterialPixelShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* original) noexcept;

        // Single atomic gate for hook-level fast paths. Disabled IBL must not
        // probe shader registries or enter capture bookkeeping.
        [[nodiscard]] bool featureEnabled() const noexcept;

        // Allocation-free draw-boundary validity check. A previously bound
        // replacement must be retired immediately when its feature or
        // published provider becomes unavailable; waiting for the engine to
        // bind another pixel shader leaves the expensive replacement path
        // active across later draws.
        [[nodiscard]] bool materialBindingActive(
            MaterialShaderBinding binding) const noexcept;

        // enabled=true binds the published t30/t31 pair with weight one.
        // enabled=false binds null provider views and the immutable zero
        // weight constants used by the reflection-free duplicate draw.
        [[nodiscard]] ScopedMaterialBindings scopeMaterialBindings(
            ID3D11DeviceContext* context,
            MaterialShaderBinding binding,
            bool enabled) noexcept;
        void onMaterialBindingsComplete(
            MaterialShaderBinding binding,
            bool enabled,
            bool restored) noexcept;

        // Called for an exact qualified DFComposite draw. The runtime's
        // delayed world-session gate decides whether to inspect it. It reads
        // current D3D11 bindings once per exact contract and changes no state.
        // This staging readback is removed when its selected source is
        // replaced by the production capture/update provider.
        void onCaptureProbeDraw(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne) noexcept;

        // Begins one image-neutral duplicate draw into an owned target with
        // the exact shader's environment inputs neutralized. The caller must
        // issue the same original draw only while the returned scope is
        // active, restore it, and then report completion below before issuing
        // the normal visible draw.
        [[nodiscard]] ScopedReflectionFreeCapture beginReflectionFreeCapture(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne) noexcept;
        [[nodiscard]] ID3D11PixelShader* reflectionFreeCoverageShader() const noexcept
        { return reflectionFreeCaptureCompactReserved_ ? coverageShader_.Get() : nullptr; }

        void onReflectionFreeCaptureDrawComplete(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne,
            bool stateRestored) noexcept;

        // Called immediately after a qualified DFComposite draw while its
        // render target and inputs remain bound. It preserves one bounded
        // stereo sample grid per supported format; CPU readback waits for the
        // family boundary and never stalls the draw hook.
        void onCaptureProbeDrawComplete(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne) noexcept;

        // Called before PSSetShader leaves the complete qualified DFComposite
        // family. It stages the last post-draw GPU snapshot for nonblocking
        // readback, avoiding targets that the engine cleared after drawing.
        void onCaptureProbePassComplete(
            ID3D11DeviceContext* context,
            std::uint16_t contractPlusOne) noexcept;

        // Called only at an exact DFLight ambient shader bind. The
        // image-neutral provider stage performs bounded nonblocking work,
        // never waits for the GPU, and does not alter material lighting.
        void onDFLightAmbientBind(ID3D11DeviceContext* context) noexcept;

        // Render-thread-only readiness gate for DFPrepass complex-material
        // encoding. Producer writes are allowed only when the exact
        // DFComposite consumer has a retained DFLight albedo source and a
        // published environment pair, so no tagged draw can lose energy.
        [[nodiscard]] bool complexMaterialConsumptionReady() const noexcept;

        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        // The local FXP contains 183 DFComposite PS records. Keep bounded
        // headroom even if the engine creates a distinct object per alias.
        static constexpr std::size_t kCaptureShaderSlotCount = 256;

        struct CaptureShaderSlot
        {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> owner;
            std::atomic<ID3D11PixelShader*> shader{};
            std::atomic<std::uint16_t> contractPlusOne{};
            std::atomic_bool tiledDiffuse{};
            std::atomic_bool deferredDiffuse{};
        };

        static constexpr std::size_t kSceneRadianceCandidateCount = 3;

        struct SceneRadianceReadbackSlot
        {
            std::array<
                Microsoft::WRL::ComPtr<ID3D11Texture2D>,
                kSceneRadianceCandidateCount>
                rollingPixelShaderTextures;
            Microsoft::WRL::ComPtr<ID3D11Texture2D>
                rollingCompositeTexture;
            Microsoft::WRL::ComPtr<ID3D11Texture2D>
                rollingReflectionFreeTexture;
            std::array<
                Microsoft::WRL::ComPtr<ID3D11Texture2D>,
                kSceneRadianceCandidateCount>
                pixelShaderTextures;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> compositeTexture;
            Microsoft::WRL::ComPtr<ID3D11Texture2D>
                reflectionFreeTexture;
            DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
            UINT sourceWidth{};
            UINT sourceHeight{};
            std::uint16_t contractPlusOne{};
            std::uint32_t checksumPrefix{};
            std::uint32_t pendingPolls{};
            bool rollingReady{};
            bool reflectionFreeCaptureAttempted{};
            bool rollingReflectionFreeCopied{};
            std::array<bool, kSceneRadianceCandidateCount>
                rollingPixelShaderCopied{};
            bool pending{};
            bool completed{};
            bool reflectionFreeCopied{};
            std::array<bool, kSceneRadianceCandidateCount>
                pixelShaderCopied{};
            bool failureLogged{};
        };

        [[nodiscard]] bool createResources(
            CreatePixelShaderFunction createPixelShader) noexcept;
        [[nodiscard]] bool createMaterialResources(
            CreatePixelShaderFunction createPixelShader) noexcept;
        [[nodiscard]] bool createSceneRadianceProbeResources() noexcept;
        void consumeSceneRadianceProbeReadbacks() noexcept;
        void publishUsable(
            const DiffuseSH& coefficients,
            const std::array<float, kEnvironmentCubeFaceCount>&
                cubeFaceConfidence,
            float coverage,
            std::uint64_t generation,
            std::uint64_t session,
            std::uint64_t tickMilliseconds) noexcept;
        void publishUnavailable(
            const std::array<float, kEnvironmentCubeFaceCount>&
                cubeFaceConfidence,
            float coverage,
            std::uint64_t generation,
            std::uint64_t session,
            std::uint64_t tickMilliseconds) noexcept;
        [[nodiscard]] bool environmentAcquisitionEnabled() const noexcept;
        [[nodiscard]] bool materialResponseEnabled() const noexcept;
        void refreshComplexMaterialProducerGate() noexcept;
        void beginMaterialEnvironmentTransition(
            std::uint64_t tickMilliseconds) noexcept;
        void updateMaterialEnvironmentTransition(
            std::uint64_t tickMilliseconds) noexcept;
        [[nodiscard]] bool synchronizeWorldCaptureSession() noexcept;
        [[nodiscard]] bool activateWorldCaptureProbeSession() noexcept;
        void resetCaptureProbes() noexcept;
        void resetResources() noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        EnvironmentProvider environmentProvider_;
        EnvironmentUpdater environmentUpdater_;
        MaterialStyle materialStyle_;
        std::uint64_t nextDiffuseFitMilliseconds_{};
        ReflectionFreeCaptureResources reflectionFreeCaptureResources_;
        CompactCapture compactCapture_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> coverageShader_;
        bool reflectionFreeCaptureCompactReserved_{};
        render::GpuTimingProfiler reflectionFreeDrawTiming_;
        std::array<
            Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kCaptureProbeContracts.size()>
            materialReplacementShaders_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> materialDisabledConstants_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> materialEnabledConstants_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialAlbedo_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            materialProperties_;
        std::array<SceneRadianceReadbackSlot, 2>
            sceneRadianceReadbackSlots_{};
        mutable std::mutex captureShaderMutex_;
        std::array<CaptureShaderSlot, kCaptureShaderSlotCount>
            captureShaderSlots_{};
        std::array<std::atomic_bool, kCaptureProbeContracts.size()>
            captureProbeLogged_{};
        std::atomic_uint64_t requestedCaptureProbeSessionId_{};
        std::atomic_uint64_t requestedCaptureProbeEarliestTickMilliseconds_{};
        std::uint64_t activeCaptureProbeSessionId_{};
        std::uint64_t activeCaptureProbeEarliestTickMilliseconds_{};
        std::uint64_t pendingEnvironmentUpdateSessionId_{};
        std::uint64_t publishedEnvironmentSessionId_{};
        std::atomic_uint64_t nextEnvironmentCaptureTickMilliseconds_{};
        std::uint64_t lastLoggedEnvironmentUpdateGeneration_{};
        bool captureProbeSessionComplete_{ true };
        bool reflectionFreeCaptureDiagnosticReserved_{};
        bool reflectionFreeCaptureProductionReserved_{};
        std::uint64_t nextCadenceTickMilliseconds_{};
        bool loggedFirstUsableDiffuseFit_{};
        bool loggedFirstDiffuseApplication_{};
        std::uint64_t diffuseApplicationBaseline_{};
        bool loggedReflectionFreeCaptureFailure_{};
        bool loggedEnvironmentUpdateFailure_{};
        bool loggedFirstMaterialBind_{};
        bool loggedMaterialBindingFailure_{};
        bool materialConsumptionFailed_{};
        bool materialEnvironmentTransitionActive_{};
        float materialEnvironmentTransitionWeight_{ 1.0f };
        std::uint64_t materialEnvironmentTransitionStartMilliseconds_{};
        std::uint64_t nextMaterialEnvironmentTransitionTickMilliseconds_{};

        std::atomic_bool resourcesReady_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool dynamicCubemapsEnabled_{ true };
        std::atomic_bool diffuseEnabled_{ true };
        std::atomic_bool pbrEnabled_{ true };
        std::atomic_bool pbrLegacyMaterials_{ true };
        std::atomic_bool pbrDirectGgx_{ true };
        std::atomic_bool pbrGrassGgx_{};
        std::atomic_bool pbrEnvironmentFresnel_{ true };
        std::atomic_bool pbrEnergyConservation_{ true };
        std::atomic_bool pbrMultiscatterCompensation_{ true };
        std::atomic_bool pbrSpecularOcclusion_{ true };
        std::atomic<float> pbrRoughnessMultiplier_{ 1.0f };
        std::atomic<float> pbrSpecularRoughnessBlend_{ 1.0f };
        std::atomic<float> pbrBaseF0Multiplier_{ 0.32f };
        std::atomic<float> pbrMinimumF0_{ 0.02f };
        std::atomic<float> pbrCubemapToF0Multiplier_{ 1.0f };
        std::atomic<float> pbrComplexMaterialF0Multiplier_{ 1.0f };
        std::atomic<float> pbrDirectLightingScale_{ 1.0f };
        std::atomic_uint64_t pbrSettingsRevision_{ 1 };
        std::uint64_t uploadedPbrSettingsRevision_{};
        std::atomic_bool sslrConsumerEnabled_{};
        std::atomic_uint32_t diffuseLevelBits_{
            std::bit_cast<std::uint32_t>(1.0f) };
        std::atomic_uint32_t diffuseSHCoverageBits_{};
        std::atomic_uint64_t cadenceTicks_{};
        std::atomic_uint64_t diffuseFitsPublished_{};
        std::atomic_uint64_t diffuseFitsRejected_{};
        std::atomic_uint64_t matchingCaptureShaders_{};
        std::atomic_uint64_t completedCaptureProbes_{};
        std::atomic_uint64_t sceneRadianceProbeCaptures_{};
        std::atomic_uint64_t sceneRadianceProbeReadbacks_{};
        std::atomic_uint64_t sceneRadianceProbeFailures_{};
        std::atomic_uint64_t materialReplacementBinds_{};
        std::atomic_uint64_t materialBindingFailures_{};
        std::atomic_bool captureRegistryOverflowLogged_{};
        std::atomic_uint64_t publishedSequence_{};
        std::atomic_bool publishedUsable_{};
        std::atomic_uint64_t publishedGeneration_{};
        std::atomic_uint64_t publishedDiffuseSessionId_{};
        std::atomic_uint64_t publishedTickMilliseconds_{};
        std::array<std::atomic_uint32_t, 12> publishedCoefficientBits_{};
        std::array<std::atomic_uint32_t, kEnvironmentCubeFaceCount>
            publishedFaceConfidenceBits_{};
    };
}
