#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace csah::ibl
{
    // The game-data callback owns loading. An AddRef-owned SRV is transferred
    // atomically to the render thread; all cache and compute work stays there.
    class MaterialStyle final
    {
    public:
        ~MaterialStyle();
        void loadReference() noexcept;
        void reset() noexcept;
        bool initialize(ID3D11Device* device, const void* code, std::size_t size) noexcept;
        ID3D11ShaderResourceView* prepare(ID3D11DeviceContext* context) noexcept;

    private:
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> original;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> reference;
            Microsoft::WRL::ComPtr<ID3D11Buffer> encoding;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
            std::uint64_t refreshed{};
            UINT cubes{};
        };
        std::atomic<ID3D11ShaderResourceView*> pendingReference_{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> reference_;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
        std::array<Entry, 8> entries_{};
        std::size_t nextEntry_{};
        bool failureLogged_{};
    };
}
