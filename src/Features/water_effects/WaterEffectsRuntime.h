#pragma once
#include "Features/water_effects/WaterEffectsSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
namespace csah::water_effects
{
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context,const std::array<ID3D11ShaderResourceView*,3>& views) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,3> previous_;
    };
    class Runtime final
    {
    public:
        static Runtime& get() noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] bool requested() const noexcept {return enabled_.load(std::memory_order_acquire);}
        void onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] ScopedBindings scopeDraw(ID3D11DeviceContext* context,bool water) noexcept;
    private:
        Runtime()=default;
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        bool ensureBuffer(Ptr<ID3D11Buffer>& buffer,Ptr<ID3D11ShaderResourceView>& view,UINT bytes) noexcept;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        std::array<Ptr<ID3D11Buffer>,3> buffers_;
        std::array<Ptr<ID3D11ShaderResourceView>,3> views_;
        std::atomic_bool enabled_{};
        std::atomic<float> scale_{1};
        UINT missingDraws_{};
        float publishedScale_{-1};
        bool failed_{},loggedFailure_{};
    };
}
