#pragma once

#include "Features/bloom_glare/BloomGlareSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace csah::bloom_glare
{
    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool gpuReady{};
        bool sizeResourcesReady{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t fftResolution{};
        std::uint64_t bloomDispatches{};
        std::uint64_t glareDispatches{};
        std::uint64_t psfGenerations{};
        std::uint64_t failures{};
    };

    class Runtime;

    class ScopedBindings final
    {
    public:
        ScopedBindings() noexcept = default;
        ~ScopedBindings() noexcept;
        ScopedBindings(const ScopedBindings&) = delete;
        ScopedBindings(ScopedBindings&&) = delete;
        ScopedBindings& operator=(const ScopedBindings&) = delete;
        ScopedBindings& operator=(ScopedBindings&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedBindings(
            Runtime* owner,
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* bloom,
            ID3D11ShaderResourceView* glare,
            ID3D11SamplerState* sampler,
            ID3D11Buffer* constants) noexcept;

        Runtime* owner_{};
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>
            previousResources_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> previousSampler_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] ScopedBindings scopeDraw(
            ID3D11DeviceContext* context,
            bool outputCompositeActive) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        friend class ScopedBindings;
        Runtime() = default;

        struct alignas(16) GpuConstants final
        {
            std::array<float, 4> composite{};
            std::array<float, 4> bloom{};
            std::array<float, 4> glareCore{};
            std::array<float, 4> glareScreen{};
            std::array<float, 4> glareOptics{};
            std::array<float, 4> glarePsf{};
            std::array<float, 4> glareTail{};
        };
        static_assert(sizeof(GpuConstants) == 112);

        struct TextureViews final
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource{};
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output{};
        };

        static constexpr std::size_t kBloomMipCount = 9;
        static constexpr std::size_t kColorChannels = 3;
        static constexpr std::size_t kFftPingPongCount = 2;

        [[nodiscard]] Settings currentSettings() const noexcept;
        [[nodiscard]] GpuConstants constantsFor(
            const Settings& settings,
            std::uint32_t eyeIndex = 0) const noexcept;
        void uploadConstants(
            ID3D11DeviceContext* context,
            const GpuConstants& constants) noexcept;
        [[nodiscard]] bool ensureSizeResources(
            ID3D11Texture2D* scene,
            const Settings& settings) noexcept;
        void releaseSizeResources() noexcept;
        [[nodiscard]] bool dispatchBloom(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* scene,
            const Settings& settings) noexcept;
        [[nodiscard]] bool dispatchGlare(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* scene,
            const Settings& settings) noexcept;
        [[nodiscard]] bool generatePsf(
            ID3D11DeviceContext* context,
            const Settings& settings) noexcept;
        void dispatchFft(
            ID3D11DeviceContext* context,
            ID3D11ComputeShader* shader,
            const TextureViews& input,
            const TextureViews& output,
            const TextureViews* psf = nullptr) noexcept;
        void clearComputeViews(ID3D11DeviceContext* context) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_{};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> wrapSampler_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> bloomThreshold_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> bloomDownsample_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> bloomUpsample_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareThreshold_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareAperture_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>,
            kColorChannels> glarePsf_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareFftRowForward_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareFftColumnForward_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareConvolveRowInverse_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareFftColumnInverse_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> glareComposite_{};

        Microsoft::WRL::ComPtr<ID3D11Texture2D> bloomTexture_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,
            kBloomMipCount> bloomResources_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>,
            kBloomMipCount> bloomOutputs_{};
        std::array<std::array<TextureViews, kFftPingPongCount>,
            kColorChannels> glareFft_{};
        std::array<TextureViews, kColorChannels> glarePsfFft_{};
        TextureViews glareOutput_{};
        std::uint32_t width_{};
        std::uint32_t height_{};
        std::uint32_t bloomMipCount_{};
        std::uint32_t fftResolution_{};
        std::atomic_bool psfDirty_{ true };

        std::atomic_bool bloomEnabled_{ true };
        std::atomic<float> bloomThresholdEV_{ 3.0f };
        std::atomic<float> bloomIntensity_{ 0.035f };
        std::atomic<float> bloomRadius_{ 2.0f };
        std::atomic_bool glareEnabled_{ true };
        std::atomic<float> glareThresholdEV_{ 6.0f };
        std::atomic<float> glareIntensity_{ 0.20f };
        std::atomic_uint32_t glareFftResolution_{ 256 };
        std::atomic<float> glarePaddingRatio_{ 0.10f };
        std::atomic_uint32_t glareApertureMode_{};
        std::atomic_uint32_t glareApertureBlades_{ 6 };
        std::atomic<float> glareApertureRotationDegrees_{};
        std::atomic<float> glareFStop_{ 2.8f };
        std::atomic<float> glareFresnelExponent_{ 30.0f };
        std::atomic<float> glareSphericalAberration_{};
        std::atomic<float> glareChromaticSpread_{ 1.0f };
        std::atomic<float> glareKernelScale_{ 1.0f };
        std::atomic<float> glarePsfSharpness_{ 0.45f };
        std::atomic<float> glarePsfNoiseFloor_{ 0.001f };
        std::atomic_bool gpuReady_{};
        std::atomic_bool firstDispatchLogged_{};
        std::atomic_uint64_t bloomDispatches_{};
        std::atomic_uint64_t glareDispatches_{};
        std::atomic_uint64_t psfGenerations_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t failures_{};
    };
}
