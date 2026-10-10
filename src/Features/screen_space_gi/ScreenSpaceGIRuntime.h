#pragma once
#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace csah::screen_space_gi
{
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context,const std::array<ID3D11ShaderResourceView*,7>& views) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,7> previous_;
    };
    class Runtime final
    {
    public:
        static Runtime& get() noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] bool requested() const noexcept {return enabled_.load(std::memory_order_acquire);}
        void onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
        // Native renderer pre-render callback, on the immediate-context owner.
        void beginFrame() noexcept;
        void resetHistory() noexcept {reset_.store(true,std::memory_order_release);}
        [[nodiscard]] ScopedBindings scopeAmbient(ID3D11DeviceContext* context,bool ambient,ID3D11ShaderResourceView* authoredPbr=nullptr) noexcept;
        void captureRadiance(ID3D11DeviceContext* context,bool tiled,ID3D11ShaderResourceView* authoredPbr=nullptr) noexcept;
    private:
        Runtime()=default;
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        struct Texture {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> view;Ptr<ID3D11UnorderedAccessView> output;};
        struct Constants
        {
            UINT width{},height{},frame{},history{};
            float originX{},originY{},viewportWidth{},viewportHeight{};
            float radius{},thickness{},aoPower{},strength{};
            UINT gi{},specular{},temporal{},tiled{};
        };
        static_assert(sizeof(Constants)==64);
        bool failAllocation() noexcept;
        bool ensureResources(UINT width,UINT height) noexcept;
        bool prepareAmbient(ID3D11DeviceContext* context) noexcept;
        bool createTexture(Texture& texture,DXGI_FORMAT format,UINT width,UINT height) noexcept;
        void dispatch(ID3D11ComputeShader* shader,const std::array<ID3D11ShaderResourceView*,8>& inputs,
            ID3D11UnorderedAccessView* first,ID3D11UnorderedAccessView* second=nullptr) noexcept;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        std::array<Ptr<ID3D11ComputeShader>,4> shaders_;
        Ptr<ID3D11Buffer> constants_,previousCamera_;
        Ptr<ID3D11ShaderResourceView> parameterView_;
        std::array<Texture,2> positions_,normals_,diffuse_,specular_;
        Texture rawDiffuse_,rawSpecular_,radiance_;
        Constants uploaded_{};
        UINT width_{},height_{},current_{};
        std::uint64_t frame_{},preparedFrame_{},radianceFrame_{},nextRetry_{};
        bool ready_{},historyValid_{},loggedFailure_{},loggedActive_{};
        std::atomic_bool enabled_{},gi_{true},specularEnabled_{},temporal_{true},reset_{true};
        std::atomic<float> radius_{256},thickness_{32},aoPower_{2},strength_{1};
    };
}
