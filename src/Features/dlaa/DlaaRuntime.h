#pragma once

#include "Features/dlaa/DlaaSettings.h"
#include "Features/dlaa/Fo4VrFrameBuffer.h"
#include "render/WorldCameraCapture.h"
#include "Features/dlaa/StreamlineBackend.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>

namespace csah::dlaa
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        StreamlineSnapshot streamline{};
        bool deviceReady{};
        bool cameraBufferQualified{};
        bool renderResourcesQualified{};
        bool operational{};
        bool cameraBindingObserved{};
        std::uint64_t mappedCameraFrames{};
        std::uint64_t cameraMapCandidates{};
        std::uint64_t cameraIdentityMatches{};
        std::uint64_t cameraValidationFailures{};
        std::uint64_t preRenderCalls{};
        std::uint64_t postRenderCalls{};
        std::uint64_t stereoEvaluations{};
        std::uint64_t stereoEvaluationFailures{};
        std::uint64_t committedFrames{};
        std::uint64_t qualificationSessions{};
        std::uint64_t hardResets{};
        std::uint32_t inputWidth{};
        std::uint32_t inputHeight{};
        std::uint32_t outputWidth{};
        std::uint32_t outputHeight{};
        std::uint32_t centerLeft{};
        std::uint32_t centerTop{};
        double gpuLeftMilliseconds{};
        double gpuRightMilliseconds{};
        double gpuCommitMilliseconds{};
        double gpuTotalMilliseconds{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void applySettings(const Settings& settings) noexcept;
        void setEnabled(bool enabled) noexcept;
        void requestRefresh() noexcept;
        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept;
        void beginQualificationSession(const char* reason) noexcept;

        void onMapSucceeded(
            ID3D11DeviceContext* context,
            ID3D11Resource* resource,
            UINT subresource,
            D3D11_MAP mapType,
            const D3D11_MAPPED_SUBRESOURCE& mapped) noexcept;
        void onBeforeUnmap(
            ID3D11DeviceContext* context,
            ID3D11Resource* resource,
            UINT subresource) noexcept;
        void onWorldCompositeDraw(ID3D11DeviceContext* context) noexcept;

        void onPreRender(
            void* dynamicResolutionManager,
            float* jitterX,
            float* jitterY) noexcept;
        void onPostImageSpace() noexcept;

        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        static constexpr std::size_t kQualificationProbeCount = 15;

        struct QualificationProbe
        {
            const char* label{};
            DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
            Microsoft::WRL::ComPtr<ID3D11Texture2D> transfer;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
            std::uint32_t polls{};
            bool pending{};
        };

        struct QualifiedResources
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneColor;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> outputColor;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthView;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> motionVectors;
            std::uint32_t packedWidth{};
            std::uint32_t eyeWidth{};
            std::uint32_t height{};

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return sceneColor && outputColor && depth && depthView && motionVectors &&
                    packedWidth != 0 && eyeWidth != 0 && height != 0;
            }
        };

        struct EyeResources
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> colorInput;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> depthInput;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthInputView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> depthOutput;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> motionInput;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> motionInputView;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> motionHistory;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> motionHistoryView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> motionHistoryUav;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> motionOutput;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> motionOutputView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> motionOutputUav;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> biasCurrentColor;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
                biasCurrentColorUav;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> outputColor;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outputView;
            std::uint32_t inputWidth{};
            std::uint32_t inputHeight{};
            std::uint32_t outputWidth{};
            std::uint32_t outputHeight{};
        };

        struct EvaluationGeometry
        {
            std::uint32_t inputWidth{};
            std::uint32_t inputHeight{};
            std::uint32_t outputWidth{};
            std::uint32_t outputHeight{};
            CenterRegion center{};
            float widthScale{ 1.0f };
            float heightScale{ 1.0f };
            bool valid{};
        };

        struct GpuTimingSlot
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> start;
            Microsoft::WRL::ComPtr<ID3D11Query> afterLeft;
            Microsoft::WRL::ComPtr<ID3D11Query> afterRight;
            Microsoft::WRL::ComPtr<ID3D11Query> afterCommit;
            bool pending{};
        };

        struct TaaMaskViewCacheEntry
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        };

        Runtime() = default;
        void captureCameraBinding() noexcept;
        void applyQualificationSession() noexcept;
        void captureQualificationResources() noexcept;
        void queueQualificationProbe(
            const char* label,
            ID3D11Resource* resource,
            ID3D11ShaderResourceView* depthView = nullptr) noexcept;
        void consumeQualificationProbes() noexcept;
        void updateQualificationState() noexcept;
        [[nodiscard]] bool updateEvaluationGeometry() noexcept;
        [[nodiscard]] bool validateActiveViewport() noexcept;
        [[nodiscard]] bool ensureEyeResources(
            std::uint32_t inputWidth,
            std::uint32_t inputHeight,
            std::uint32_t outputWidth,
            std::uint32_t outputHeight) noexcept;
        [[nodiscard]] bool ensureDepthPreparationResources() noexcept;
        [[nodiscard]] bool prepareEyeInputs(
            ID3D11Texture2D* currentTaaMask) noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* resolveTaaMaskView(
            ID3D11Texture2D* currentTaaMask) noexcept;
        [[nodiscard]] bool ensureCompositorResources(
            std::uint32_t eyeWidth,
            std::uint32_t height) noexcept;
        [[nodiscard]] bool composeStereo(
            std::uint32_t destinationLeft,
            std::uint32_t destinationTop,
            float featherPixels,
            float sharpness,
            bool visualize) noexcept;
        void applyPendingSettings() noexcept;
        void hardResetTemporalState(const char* reason) noexcept;
        [[nodiscard]] bool ensureGpuTimingQueries() noexcept;
        void consumeGpuTimingQueries() noexcept;
        [[nodiscard]] GpuTimingSlot* beginGpuTiming() noexcept;
        void recordCpuTiming(
            double captureMicroseconds,
            double evaluateMicroseconds,
            double commitMicroseconds,
            double restoreMicroseconds) noexcept;
        void logPerformanceTimingsIfReady() noexcept;
        [[nodiscard]] bool evaluateStereoFrame(
            std::uint64_t postCall) noexcept;
        void clearRenderResources(bool releaseStreamline) noexcept;
        void restoreDynamicResolutionIfOwned() noexcept;
        [[nodiscard]] bool validateCameraFrame(
            const Fo4VrFrameBuffer& frame) const noexcept;

        Settings settings_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> cameraBuffer_;
        Fo4VrFrameBuffer cameraFrame_{};
        render::WorldCameraCapture worldCameraCapture_;
        bool worldCameraDiagnosticLogged_{};
        bool missingWorldCameraLogged_{};
        std::atomic_bool qualificationResetRequested_{};
        QualifiedResources qualifiedResources_{};
        std::array<EyeResources, 2> eyeResources_{};
        EvaluationGeometry evaluationGeometry_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> compositorShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> compositorConstants_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> motionRepairShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> motionRepairConstants_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> reactiveMaskShader_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> depthPreparationShader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> depthPreparationConstants_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> reactiveMaskConstants_;
        static constexpr std::size_t kTaaMaskViewCacheCount = 4;
        std::array<TaaMaskViewCacheEntry, kTaaMaskViewCacheCount>
            taaMaskViewCache_{};
        std::size_t taaMaskViewCacheReplaceIndex_{};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> compositionSurface_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            compositionSurfaceView_;
        std::uint32_t compositionEyeWidth_{};
        std::uint32_t compositionHeight_{};
        static constexpr std::size_t kGpuTimingSlotCount = 8;
        std::array<GpuTimingSlot, kGpuTimingSlotCount> gpuTimingSlots_{};
        std::array<QualificationProbe, kQualificationProbeCount>
            qualificationProbes_{};
        mutable std::mutex settingsMutex_;
        Settings pendingSettings_{};
        std::atomic_uint64_t pendingSettingsRevision_{ 1 };
        std::uint64_t renderThreadSettingsRevision_{};
        std::atomic_bool requested_{ true };
        std::atomic_bool refreshRequested_{};
        std::atomic_bool deviceReady_{};
        std::atomic_bool cameraBufferQualified_{};
        std::atomic_bool renderResourcesQualified_{};
        std::atomic_bool operational_{};
        std::uint64_t nextResourceAttemptPostCall_{};
        std::atomic_bool qualificationCaptureRequested_{ true };
        std::atomic<ID3D11Buffer*> cameraBufferIdentity_{};
        std::atomic_uint64_t mappedCameraFrames_{};
        std::atomic_uint64_t cameraMapCandidates_{};
        std::atomic_uint64_t cameraIdentityMatches_{};
        std::atomic_uint64_t cameraValidationFailures_{};
        std::atomic_uint32_t qualificationProbeAttempts_{};
        std::atomic_uint64_t preRenderCalls_{};
        std::atomic_uint64_t postRenderCalls_{};
        std::atomic_uint64_t lastValidCameraPostCall_{};
        std::atomic_uint64_t stereoEvaluations_{};
        std::atomic_uint64_t stereoEvaluationFailures_{};
        std::atomic_uint64_t committedFrames_{};
        std::atomic_uint64_t qualificationSessions_{};
        std::atomic_uint64_t hardResets_{};
        std::atomic_uint32_t publishedInputWidth_{};
        std::atomic_uint32_t publishedInputHeight_{};
        std::atomic_uint32_t publishedOutputWidth_{};
        std::atomic_uint32_t publishedOutputHeight_{};
        std::atomic_uint32_t publishedCenterLeft_{};
        std::atomic_uint32_t publishedCenterTop_{};
        std::uint32_t streamlineFrameIndex_{};
        std::size_t gpuTimingWriteIndex_{};
        std::uint64_t gpuTimingSamples_{};
        double gpuLeftMilliseconds_{};
        double gpuRightMilliseconds_{};
        double gpuCommitMilliseconds_{};
        double gpuTotalMilliseconds_{};
        std::atomic<double> publishedGpuLeftMilliseconds_{};
        std::atomic<double> publishedGpuRightMilliseconds_{};
        std::atomic<double> publishedGpuCommitMilliseconds_{};
        std::atomic<double> publishedGpuTotalMilliseconds_{};
        std::uint64_t cpuTimingSamples_{};
        double cpuCaptureMicroseconds_{};
        double cpuEvaluateMicroseconds_{};
        double cpuCommitMicroseconds_{};
        double cpuRestoreMicroseconds_{};
        std::uint32_t jitterPhase_{};
        float jitterProjectionX_{};
        float jitterProjectionY_{};
        bool resourceContractObserved_{};
        bool resetHistory_{ true };
        bool renderThreadRequested_{ true };
        bool dynamicResolutionOwned_{};
        void* dynamicResolutionManager_{};
        float savedDynamicResolutionWidth_{};
        float savedDynamicResolutionHeight_{};
        float ownedDynamicResolutionWidth_{ 1.0f };
        float ownedDynamicResolutionHeight_{ 1.0f };
        std::uint8_t savedDynamicResolutionActive_{};
        std::atomic_bool firstCameraQualifiedLogged_{};
        std::atomic_bool firstCameraValidationFailureLogged_{};
        std::atomic_bool firstResourceContractLogged_{};
        std::atomic_bool firstEvaluationLogged_{};
        std::atomic_bool firstEvaluationFailureLogged_{};
        std::atomic_bool firstColorComparisonQueued_{};
        std::atomic_bool firstActiveColorComparisonQueued_{};
    };
}
