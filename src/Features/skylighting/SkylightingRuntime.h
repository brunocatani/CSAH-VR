#pragma once

#include "Features/skylighting/SkylightingSettings.h"
#include "render/GpuTimingProfiler.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace csah::skylighting
{
    class Runtime;

    class ScopedVisibilityBindings final
    {
    public:
        ScopedVisibilityBindings() noexcept = default;
        ScopedVisibilityBindings(ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* nearProbe, ID3D11ShaderResourceView* farProbe,
            ID3D11ShaderResourceView* parameters, ID3D11ShaderResourceView* nearSun,
            ID3D11ShaderResourceView* farSun) noexcept;
        ~ScopedVisibilityBindings();
        ScopedVisibilityBindings(const ScopedVisibilityBindings&) = delete;
        ScopedVisibilityBindings& operator=(const ScopedVisibilityBindings&) = delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 5> previous_;
    };

    using NativePrecipitationRender =
        void(__fastcall*)(void* precipitation, void* rainEmitter);
    using NativeProjectionSetup =
        void(__fastcall*)(void* precipitation, void** cameraHolder);

    struct RuntimeSnapshot
    {
        bool requested{};
        bool gpuResourcesReady{};
        bool nativeHookOwned{};
        bool exteriorActive{};
        bool privateDepthReady{};
        bool probeDataValid{};
        Quality requestedQuality{ Quality::high };
        Quality activeQuality{ Quality::high };
        std::uint32_t probeWidth{};
        std::uint32_t probeHeight{};
        std::uint32_t probeDepth{};
        std::uint64_t captureCalls{};
        std::uint64_t privateDepthBinds{};
        std::uint64_t probeDispatches{};
        std::uint64_t rejectedCaptures{};
        std::uint64_t ambientBinds{};
    };

    class ScopedAmbientBindings final
    {
    public:
        ScopedAmbientBindings() = default;
        ScopedAmbientBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* nearProbe,
            ID3D11ShaderResourceView* farProbe,
            ID3D11Buffer* constants,
            ID3D11UnorderedAccessView* diagnostic,
            render::GpuTimingProfiler* drawTiming,
            Runtime* owner) noexcept;
        ~ScopedAmbientBindings();

        ScopedAmbientBindings(const ScopedAmbientBindings&) = delete;
        ScopedAmbientBindings& operator=(
            const ScopedAmbientBindings&) = delete;
        ScopedAmbientBindings(ScopedAmbientBindings&& other) noexcept;
        ScopedAmbientBindings& operator=(
            ScopedAmbientBindings&& other) noexcept;

        [[nodiscard]] bool restore() noexcept;

    private:
        ID3D11DeviceContext* context_{};
        std::array<ID3D11ShaderResourceView*, 2> previousProbes_{};
        ID3D11Buffer* previousConstants_{};
        ID3D11UnorderedAccessView* previousDiagnostic_{};
        render::GpuTimingProfiler::Scope drawTiming_;
        Runtime* owner_{};
        bool diagnosticCaptured_{};
        bool captured_{};
    };

    class Runtime final
    {
    public:
        [[nodiscard]] static Runtime& get() noexcept;

        void applySettings(const Settings& settings) noexcept;
        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* immediateContext) noexcept;
        void setNativeHookOwned(bool owned) noexcept;
        void beginWorldSession() noexcept;

        // Called by the verified native precipitation wrapper detour after
        // the engine's original weather pass has completed. The manager is
        // resolved from FO4VR's persistent renderer state, not active weather.
        void onNativePrecipitationFrame(
            void* precipitation,
            NativePrecipitationRender render,
            NativeProjectionSetup restoreProjection) noexcept;

        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] bool visibilityReady() const noexcept;
        [[nodiscard]] ScopedVisibilityBindings scopeVisibilityDraw(
            ID3D11DeviceContext* context, bool consumerActive) noexcept;
        void captureDirectionalFrame(ID3D11DeviceContext* context, std::uint32_t shadowLayout) noexcept;
        [[nodiscard]] ScopedAmbientBindings scopeAmbientDraw(
            ID3D11DeviceContext* context,
            bool ambientReplacementActive) noexcept;
        void observePrivateCaptureDraw(
            ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        friend class ScopedAmbientBindings;

        Runtime() = default;

        struct UInt4
        {
            std::uint32_t x{};
            std::uint32_t y{};
            std::uint32_t z{};
            std::uint32_t w{};
        };

        struct Int4
        {
            std::int32_t x{};
            std::int32_t y{};
            std::int32_t z{};
            std::int32_t w{};
        };

        struct Float4
        {
            float x{};
            float y{};
            float z{};
            float w{};
        };

        struct Dimensions
        {
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t depth{};
        };

        struct alignas(16) ProbeLevelConstants
        {
            Float4 arraySize{};
            Float4 cellSize{};
            Float4 positionOffset{};
            UInt4 arrayDimensions{};
            UInt4 arrayOrigin{};
            Int4 validMargin{};
        };
        static_assert(sizeof(ProbeLevelConstants) == 96);

        struct alignas(16) Constants
        {
            std::array<float, 16> occlusionViewProjection{};
            Float4 occlusionDirection{};
            ProbeLevelConstants nearLevel{};
            ProbeLevelConstants farLevel{};
            UInt4 updateControl{};
            Float4 response{};
            Float4 probeWorldOrigin{};
            ProbeLevelConstants nearSunLevel{};
            ProbeLevelConstants farSunLevel{};
        };
        static_assert(sizeof(Constants) == 512);

        struct RollingVolumeState
        {
            Dimensions dimensions{};
            std::array<std::int64_t, 3> previousCell{};
            bool previousCellValid{};
        };

        struct ProbeResources : RollingVolumeState
        {
            Microsoft::WRL::ComPtr<ID3D11Texture3D> probeTexture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> probeResource;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> probeOutput;
            Microsoft::WRL::ComPtr<ID3D11Texture3D> accumulationTexture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
                accumulationResource;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
                accumulationOutput;
            Microsoft::WRL::ComPtr<ID3D11Texture3D> sunTexture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sunResource;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> sunOutput;
            RollingVolumeState sunGrid{};
            bool dataValid{};
            std::uint32_t updateSliceCursor{};
            std::uint32_t captureQuadrant{};
        };

        [[nodiscard]] static Dimensions dimensionsFor(Quality quality) noexcept;
        [[nodiscard]] static Dimensions farDimensionsFor() noexcept;
        [[nodiscard]] bool createProbeLevelResources(
            ProbeResources& level,
            Dimensions dimensions) noexcept;
        [[nodiscard]] bool createProbeResources() noexcept;
        [[nodiscard]] bool createDiagnosticResources() noexcept;
        [[nodiscard]] bool ensureGpuResources() noexcept;
        [[nodiscard]] bool ensurePrivateDepth(
            ID3D11DepthStencilView* source) noexcept;
        void clearProbeResources() noexcept;
        void publishConstants(bool featureActive) noexcept;
        void dispatchProbeUpdate(
            ProbeResources& level,
            std::uint32_t levelIndex,
            std::uint32_t sliceStart,
            std::uint32_t sliceCount) noexcept;
        void consumeDiagnosticReadback() noexcept;
        void submitAmbientDiagnostic() noexcept;
        void consumeAmbientDiagnosticReadback() noexcept;
        [[nodiscard]] bool updateRollingVolume(
            RollingVolumeState& level,
            ProbeLevelConstants& levelConstants,
            float captureDistance,
            float captureHeight,
            float x,
            float y,
            float z) noexcept;

        Settings startupSettings_{};
        Quality activeQuality_{ Quality::high };
        ProbeResources nearProbes_{};
        ProbeResources farProbes_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> privateDepthTexture_;
        bool depthReadFailureLogged_{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> privateDepthView_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> privateDepthResource_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> diagnosticBuffer_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            diagnosticOutput_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> diagnosticStaging_;
        Microsoft::WRL::ComPtr<ID3D11Query> diagnosticCompletion_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> ambientDiagnosticBuffer_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            ambientDiagnosticOutput_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> ambientDiagnosticStaging_;
        Microsoft::WRL::ComPtr<ID3D11Query>
            ambientDiagnosticCompletion_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateShader_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateSunShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> sunConstants_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> comparisonSampler_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantsBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> visibilityParameters_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> visibilityParametersView_;
        render::GpuTimingProfiler gpuTiming_;
        render::GpuTimingProfiler ambientDrawGpuTiming_;
        render::GpuTimingProfiler sunGpuTiming_;
        std::uint64_t lastSunCaptureFrame_{ ~std::uint64_t{} };
        std::uint64_t sunCaptures_{};
        bool sunTimingAttempted_{};
        D3D11_TEXTURE2D_DESC privateDepthDescription_{};
        D3D11_DEPTH_STENCIL_VIEW_DESC privateDepthViewDescription_{};
        Constants constants_{};
        alignas(16) std::array<std::byte, 0xF10> nativeOutput_{};
        std::uint64_t publishedSettingsRevision_{};
        std::uint64_t publishedCaptureRevision_{};
        bool publishedFeatureActive_{};
        std::uint64_t captureRevision_{};
        bool diagnosticSubmitted_{};
        bool diagnosticPending_{};
        bool diagnosticLogged_{};
        bool ambientDiagnosticSubmitted_{};
        bool ambientDiagnosticPending_{};
        bool ambientDiagnosticLogged_{};
        std::uint64_t nextResourceAttemptCall_{};

        std::atomic_bool enabled_{ true };
        std::atomic_uint32_t requestedQuality_{
            static_cast<std::uint32_t>(Quality::high) };
        std::atomic_uint32_t minimumDiffuseBits_{};
        std::atomic_uint32_t minimumSpecularBits_{};
        std::atomic_uint32_t maximumZenithBits_{};
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool gpuResourcesReady_{};
        std::atomic_bool nativeHookOwned_{};
        std::atomic_bool exteriorActive_{};
        std::atomic_bool privateDepthReady_{};
        std::atomic_bool probeDataValid_{};
        std::atomic_bool resetRequested_{ true };
        std::atomic_bool privateRenderActive_{};
        std::atomic_bool privateCaptureDrawStateLogged_{};
        std::atomic_uint64_t frameIndex_{};
        std::atomic_uint64_t captureCalls_{};
        std::atomic_uint64_t privateDepthBinds_{};
        std::atomic_uint64_t probeDispatches_{};
        std::atomic_uint64_t rejectedCaptures_{};
        std::atomic_uint64_t ambientBinds_{};
        std::atomic_bool firstPrerequisiteRejectionLogged_{};
        std::atomic_bool firstWorldStateRejectionLogged_{};
        std::atomic_bool firstRenderAttemptLogged_{};
        std::atomic_bool firstPrivateDepthFailureLogged_{};
        std::atomic_bool firstActiveAmbientBindLogged_{};
        std::atomic_bool firstPassProducerSummaryLogged_{};
        std::atomic_bool firstFarProbeUpdateLogged_{};
        std::atomic_bool qualityRestartWarningLogged_{};
    };
}
