#pragma once

#include "Features/subsurface_scattering/SubsurfaceScatteringSettings.h"
#include "render/GpuTimingProfiler.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace csah::subsurface_scattering
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool gpuReady{};
        std::uint64_t executions{};
        std::uint64_t dispatches{};
        std::uint64_t resourceRebuilds{};
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
        void beginFrame() noexcept { processed_ = {}; }
        void executeBeforeWorldComposite(ID3D11DeviceContext* context, bool tiled) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
        [[nodiscard]] bool ensureScratch(
            ID3D11ShaderResourceView* target,
            ID3D11Texture2D** source,
            UINT* sourceSubresource) noexcept;
        bool executeSource(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source) noexcept;
        std::array<Microsoft::WRL::ComPtr<ID3D11Resource>,2> processed_{};
        [[nodiscard]] bool updateConstants(
            ID3D11DeviceContext* context,
            float directionX,
            float directionY) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> scratchA_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> scratchB_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scratchAView_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scratchBView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> scratchAOutput_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> scratchBOutput_;
        render::GpuTimingProfiler gpuTiming_;
        UINT width_{};
        UINT height_{};
        DXGI_FORMAT format_{ DXGI_FORMAT_UNKNOWN };
        std::atomic_bool enabled_{ true };
        std::atomic<float> strength_{ 0.55f };
        std::atomic<float> radiusPixels_{ 2.0f };
        std::atomic<float> depthRejection_{ 0.003f };
        std::atomic_bool resourcesReady_{};
        std::atomic_uint64_t executions_{};
        std::atomic_uint64_t dispatches_{};
        std::atomic_uint64_t resourceRebuilds_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstExecutionLogged_{};
    };
}
