#pragma once

#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblProjectionModel.h"
#include "Features/ibl/IblProviderModel.h"
#include "render/GpuTimingProfiler.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace csah::ibl
{
    enum class EnvironmentUpdateConsumeResult : std::uint8_t
    {
        idle,
        pending,
        completed,
        failed,
    };

    struct EnvironmentUpdateSummary
    {
        bool initialized{};
        bool pending{};
        bool historyUsed{};
        bool diffuseUpdated{};
        std::uint64_t dispatches{};
        std::uint64_t publishedUpdates{};
        std::uint64_t completedReadbacks{};
        std::uint64_t failedUpdates{};
        std::uint64_t generation{};
        EnvironmentProbeOrigin probeOrigin{};
        Float3 average{};
        std::array<float, kEnvironmentCubeFaceCount> faceAverageLuminance{};
        std::array<float, kEnvironmentCubeFaceCount> faceAverageValidity{};
        float peak{};
        float averageValidity{};
        std::uint32_t nonBlackSamples{};
        std::uint32_t coveredSamples{};
        std::uint32_t sampleCount{};
        float diffuseSHCoverage{};
        DiffuseSHState diffuseSHState{ DiffuseSHState::invalid };
        DiffuseSH diffuseSH{};
        double validationCpuMilliseconds{};
    };

    // Render-thread-only transactional updater. It captures one shared
    // stereo cube plus directional validity, builds a bounded scene-linear
    // GGX mip chain in the provider's private back pair, and publishes only
    // after a nonblocking readback validates the completed generation.
    class EnvironmentUpdater final
    {
    public:
        [[nodiscard]] bool initialize(
            ID3D11Device* device,
            const void* captureShaderBytecode,
            std::size_t captureShaderBytecodeSize,
            const void* filterShaderBytecode,
            std::size_t filterShaderBytecodeSize,
            const void* reductionShaderBytecode,
            std::size_t reductionShaderBytecodeSize,
            std::uint32_t extent) noexcept;
        void reset() noexcept;

        [[nodiscard]] bool dispatchUpdate(
            ID3D11DeviceContext* context,
            EnvironmentProvider& provider,
            ID3D11ShaderResourceView* reflectionFreeRadiance,
            ID3D11ShaderResourceView* sceneDepth,
            ID3D11Buffer* sceneConstants,
            bool usePublishedHistory,
            bool fitDiffuse = true) noexcept;

        [[nodiscard]] EnvironmentUpdateConsumeResult consumeUpdate(
            ID3D11DeviceContext* context,
            EnvironmentProvider& provider) noexcept;

        [[nodiscard]] EnvironmentUpdateSummary snapshot() const noexcept
        {
            return summary_;
        }

    private:
        struct HistoryCube
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
        };

        struct Resources
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> captureShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> filterShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> reductionShader;
            Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
            Microsoft::WRL::ComPtr<ID3D11Buffer> captureConstants;
            Microsoft::WRL::ComPtr<ID3D11Buffer> filterConstants;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedRadiance;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
                capturedRadianceView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
                capturedRadianceOutput;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedValidity;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
                capturedValidityView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
                capturedValidityOutput;
            Microsoft::WRL::ComPtr<ID3D11Buffer> reductionConstants;
            Microsoft::WRL::ComPtr<ID3D11Buffer> reductionPartials;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> reductionPartialsView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> reductionPartialsOutput;
            Microsoft::WRL::ComPtr<ID3D11Buffer> reductionResult;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> reductionResultOutput;
            Microsoft::WRL::ComPtr<ID3D11Buffer> stagingReduction;
            Microsoft::WRL::ComPtr<ID3D11Buffer> stagingSceneConstants;
            Microsoft::WRL::ComPtr<ID3D11Query> completionEvent;
            std::array<HistoryCube, 2> sky;
            HistoryCube nearestDepth;
            HistoryCube nearestOwner;
            std::uint32_t extent{};
            UINT sceneConstantsByteWidth{};
        };

        struct UpdateConstants
        {
            std::uint32_t sourceWidth{};
            std::uint32_t sourceHeight{};
            std::uint32_t targetExtent{};
            std::uint32_t historyAvailable{};
            std::uint32_t updatePass{};
            float historyBlend{};
            std::array<std::uint32_t, 2> reserved{};
            Float3 previousProbeOrigin{};
            std::uint32_t previousProbeOriginValid{};
        };

        static_assert(sizeof(UpdateConstants) == 48);

        struct FilterConstants
        {
            std::uint32_t targetExtent{};
            std::uint32_t mipLevel{};
            std::uint32_t mipCount{};
            float roughness{};
        };

        static_assert(sizeof(FilterConstants) == 16);

        [[nodiscard]] bool validateInputs(
            ID3D11DeviceContext* context,
            const EnvironmentProvider& provider,
            ID3D11ShaderResourceView* reflectionFreeRadiance,
            ID3D11ShaderResourceView* sceneDepth,
            ID3D11Buffer* sceneConstants,
            bool usePublishedHistory,
            D3D11_TEXTURE2D_DESC& radianceDescription) const noexcept;
        [[nodiscard]] bool prepareSceneConstantsReadback(
            ID3D11Buffer* sceneConstants) noexcept;
        void recordFailure() noexcept;

        std::uint32_t skyFront_{};
        bool pendingDiffuseFit_{};
        Resources resources_{};
        EnvironmentUpdateSummary summary_{};
        render::GpuTimingProfiler gpuTiming_;
        bool gpuTimingInitializationAttempted_{};
        double validationCpuMilliseconds_{};
        std::uint64_t validationCpuSamples_{};
        bool firstValidationTimingLogged_{};
    };
}
