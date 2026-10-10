#pragma once
#include "Features/inverse_square/InverseSquareSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstddef>
namespace csah::inverse_square
{
    struct ShaderSelection {const void* data{};std::size_t size{};bool replaced{};};
    [[nodiscard]] ShaderSelection selectShader(const void*,std::size_t,bool compute) noexcept;
    [[nodiscard]] bool tagShader(ID3D11DeviceChild*) noexcept;
    [[nodiscard]] bool taggedShader(ID3D11DeviceChild*) noexcept;
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext*,ID3D11ShaderResourceView*,bool compute) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};bool compute_{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previous_;
    };
    class Runtime final
    {
    public:
        static Runtime& get() noexcept;
        void applySettings(const Settings&) noexcept;
        void onDeviceCreated(ID3D11Device*,ID3D11DeviceContext*) noexcept;
        void setDispatchReady(bool ready) noexcept {dispatchReady_.store(ready,std::memory_order_release);}
        [[nodiscard]] ScopedBindings scope(ID3D11DeviceContext*,bool compute,bool active,bool localMaterials=false) noexcept;
    private:
        Runtime()=default;
        std::atomic_bool enabled_{},dispatchReady_{};
        std::atomic<float> size_{1.41421356237f},intensity_{1};
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
        std::array<float,4> uploaded_{};bool uploadedValid_{},loggedFailure_{};unsigned retry_{};
    };
}
