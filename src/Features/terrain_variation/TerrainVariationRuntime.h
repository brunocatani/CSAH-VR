#pragma once
#include "Features/terrain_variation/TerrainVariationSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
namespace csah::terrain_variation
{
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* parameters) noexcept;
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
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] bool requested() const noexcept {return enabled_.load(std::memory_order_acquire);}
        void onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] ScopedBindings scopeDraw(ID3D11DeviceContext* context,bool material) noexcept;
    private:
        Runtime()=default;
        void createParameters() noexcept;
        void publish() noexcept;
        std::atomic_bool enabled_{},lodEnabled_{true};
        std::atomic_uint64_t revision_{1};
        std::uint64_t published_{},resourceAttempts_{};
        std::uint32_t missingResourceDraws_{};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    };
}
