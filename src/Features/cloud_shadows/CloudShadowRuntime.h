#pragma once

#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace csah::cloud_shadows
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool gpuReady{};
        bool cubeReady{};
        std::uint32_t populatedFaceMask{};
        std::uint64_t captureCandidates{};
        std::uint64_t capturedDraws{};
        std::uint64_t captureRejects{};
        std::uint64_t cubeRebuilds{};
        std::uint64_t lightingBinds{};
        std::uint64_t lightingRejects{};
        std::uint64_t restores{};
        std::uint64_t failures{};
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
        static bool internalRenderActive() noexcept;
        void beginFrame() noexcept;
        void resetWorld() noexcept;
        void observeMainView(ID3D11DeviceContext* context) noexcept;
        void capture(ID3D11DeviceContext* context,
            linear_lighting::ReplacementShaderBinding binding,
            void (*submit)(void*), void* user) noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* fieldView() const noexcept { return fieldView_.Get(); }
        [[nodiscard]] bool prepareLighting(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView*& cube,
            ID3D11SamplerState*& sampler,
            float& opacity, ID3D11Buffer* camera = nullptr) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
        bool ensureCaptureResources() noexcept;
        bool updateField(ID3D11Buffer* camera) noexcept;
        void synchronizeReset() noexcept;
        template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
        struct Cube {
            Ptr<ID3D11Texture2D> texture;
            Ptr<ID3D11ShaderResourceView> view;
            std::array<Ptr<ID3D11RenderTargetView>, 6> targets;
        };
        Ptr<ID3D11Device> device_;
        Ptr<ID3D11DeviceContext> context_;
        std::array<Cube,2> cubes_;
        Ptr<ID3D11Resource> mainTarget_;
        Ptr<ID3D11SamplerState> sampler_;
        Ptr<ID3D11Buffer> field_, fieldParameters_;
        Ptr<ID3D11ShaderResourceView> fieldView_;
        Ptr<ID3D11UnorderedAccessView> fieldOutput_;
        Ptr<ID3D11ComputeShader> fieldUpdate_;
        unsigned readIndex_{}, writeIndex_{1};
        bool captureStarted_{}, captureRejected_{}, fieldReset_{true};
        std::uint64_t frame_{}, fieldFrame_{~0ull}, startTick_{};
        unsigned retryFrames_{};
        std::atomic_uint64_t epoch_{1};
        std::uint64_t observedEpoch_{};
        std::atomic_uint32_t populatedFaces_{};
        std::atomic_bool enabled_{ true };
        std::atomic<float> opacity_{ 0.55f };
        std::atomic_bool resourcesReady_{};
        std::atomic_uint64_t captureCandidates_{};
        std::atomic_uint64_t capturedDraws_{};
        std::atomic_uint64_t captureRejects_{};
        std::atomic_uint64_t cubeRebuilds_{};
        std::atomic_uint64_t lightingBinds_{};
        std::atomic_uint64_t lightingRejects_{};
        std::atomic_uint64_t restores_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstCaptureLogged_{};
        std::atomic_bool firstCaptureRejectLogged_{};
        std::atomic_bool firstLightingBindLogged_{};
    };
}
