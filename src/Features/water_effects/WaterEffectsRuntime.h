#pragma once
#include "Features/water_effects/WaterEffectsSettings.h"
#include "Features/water_effects/WaterWorldData.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
namespace csah::water_effects
{
    class ScopedCausticBindings final
    {
    public:
        ScopedCausticBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* texture,
            ID3D11ShaderResourceView* parameters,ID3D11SamplerState* sampler) noexcept;
        ~ScopedCausticBindings();
        ScopedCausticBindings(const ScopedCausticBindings&)=delete;
        ScopedCausticBindings& operator=(const ScopedCausticBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,2> previous_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    };
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
        [[nodiscard]] bool causticsRequested() const noexcept
        {return requested() && caustics_.load(std::memory_order_relaxed);}
        void onSkyUpdated(bool worldReady,float seconds) noexcept {world_.capture(causticsRequested(),worldReady,seconds);}
        void resetWorld() noexcept {world_.invalidate();}
        void onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
        [[nodiscard]] ScopedBindings scopeDraw(ID3D11DeviceContext* context,bool water) noexcept;
        [[nodiscard]] ScopedCausticBindings scopeCaustics(ID3D11DeviceContext* context) noexcept;
    private:
        Runtime()=default;
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        bool ensureBuffer(Ptr<ID3D11Buffer>& buffer,Ptr<ID3D11ShaderResourceView>& view,UINT bytes) noexcept;
        bool publishParameters() noexcept;
        bool ensureCaustics() noexcept;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        std::array<Ptr<ID3D11Buffer>,3> buffers_;
        std::array<Ptr<ID3D11ShaderResourceView>,3> views_;
        Ptr<ID3D11Texture2D> causticTexture_;
        Ptr<ID3D11ShaderResourceView> causticView_;
        Ptr<ID3D11SamplerState> causticSampler_;
        WorldData world_;
        std::atomic_bool enabled_{};
        std::atomic_bool parallax_{true},caustics_{true};
        std::atomic<float> scale_{1};
        std::atomic<float> causticStrength_{1};
        std::atomic_uint64_t settingsRevision_{1};
        UINT missingDraws_{};
        UINT missingCausticDraws_{};
        std::uint64_t publishedSettings_{},publishedWorld_{};
        bool failed_{},loggedFailure_{};
        bool causticFailed_{},loggedCausticFailure_{},loggedCausticPlanes_{};
    };
}
