#pragma once
#include "Features/lod_blending/LodBlendingSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace csah::lod_blending
{
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context, ID3D11ShaderResourceView* parameters) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previous_;
    };
    class Runtime final
    {
    public:
        static Runtime& get() noexcept;
        void applySettings(const Settings& value) noexcept;
        void onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] bool requested() const noexcept {return requested_.load(std::memory_order_acquire);}
        // Native SetupGeometry/RestoreGeometry bound this classification to
        // one render pass. No engine pointer survives classification.
        void beginGeometry(void* pass) noexcept;
        void endGeometry() noexcept;
        [[nodiscard]] ScopedBindings scopeDraw(ID3D11DeviceContext* context,bool material) noexcept;
    private:
        Runtime()=default;
        void publish() noexcept;
        void createParameters() noexcept;
        std::uint64_t resourceAttempts_{};
        std::uint32_t missingResourceDraws_{};
        std::atomic<float> terrainBrightness_{1},terrainGamma_{1},objectBrightness_{1},objectGamma_{1};
        std::atomic_bool requested_{},objectRequested_{};
        const std::byte* image_{};
        std::atomic_uint64_t revision_{1};
        std::uint64_t published_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,2> buffers_;
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,2> views_;
    };
}
