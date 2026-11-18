#pragma once
#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "Features/pbr/PbrSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace csah::screen_space_gi
{
    // Why prepareAmbient left native ambient lighting unchanged on a frame.
    enum class Rejection : std::uint8_t
    {
        retryWait,
        gbufferContract,
        viewport,
        cameraBuffers,
        resources,
        computeState,
        count,
    };
    inline constexpr std::size_t kRejectionCount = static_cast<std::size_t>(Rejection::count);

    struct RuntimeSnapshot final
    {
        bool enabled{};
        std::uint64_t ambientScopes{};
        std::uint64_t preparedFrames{};
        std::uint64_t radianceCaptures{};
        std::uint64_t composedFrames{};
        std::uint64_t composeFailures{};
        std::array<std::uint64_t, kRejectionCount> rejections{};
        // Formats seen by the last G-buffer contract rejection (normal, material, depth).
        std::array<std::uint32_t, 3> lastRejectedFormats{};
    };

    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context,const std::array<ID3D11ShaderResourceView*,8>& views) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,8> previous_;
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
        [[nodiscard]] ScopedBindings scopeAmbient(ID3D11DeviceContext* context,bool ambient,ID3D11ShaderResourceView* authoredPbr=nullptr, const pbr::FrameData& material={}, ID3D11ShaderResourceView* surface=nullptr) noexcept;
        // Called with the native DFComposite bound, before it draws: scales its
        // diffuse accumulators by the root of the occlusion and adds the bounce.
        void composeOcclusion(ID3D11DeviceContext* context,bool tiled) noexcept;
        // True while that pass sets and restores pipeline state, so the D3D11
        // hooks pass its calls straight through instead of re-deriving the
        // active native draw from the pass's own shaders.
        [[nodiscard]] static bool internalRenderActive() noexcept;
        void captureRadiance(ID3D11DeviceContext* context,bool tiled,ID3D11ShaderResourceView* authoredPbr=nullptr) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;
    private:
        bool reject(Rejection reason) noexcept;
        Runtime()=default;
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        struct Texture {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> view;Ptr<ID3D11UnorderedAccessView> output;};
        struct Constants
        {
            UINT width{},height{},frame{},history{};
            float originX{},originY{},viewportWidth{},viewportHeight{};
            float radius{},thickness{},aoPower{},strength{};
            UINT gi{},specular{},temporal{},tiled{};
            pbr::FrameData material{};
        };
        static_assert(sizeof(Constants)==128);
        bool failAllocation() noexcept;
        bool ensureResources(UINT width,UINT height) noexcept;
        bool prepareAmbient(ID3D11DeviceContext* context, ID3D11ShaderResourceView* authored, const pbr::FrameData& material, ID3D11ShaderResourceView* surface) noexcept;
        bool createTexture(Texture& texture,DXGI_FORMAT format,UINT width,UINT height) noexcept;
        bool createComposeResources() noexcept;
        [[nodiscard]] ID3D11RenderTargetView* composeTarget(std::size_t index,ID3D11ShaderResourceView* accumulator) noexcept;
        void dispatch(ID3D11ComputeShader* shader,const std::array<ID3D11ShaderResourceView*,8>& inputs,
            ID3D11UnorderedAccessView* first,ID3D11UnorderedAccessView* second=nullptr) noexcept;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        std::array<Ptr<ID3D11ComputeShader>,4> shaders_;
        Ptr<ID3D11Buffer> constants_,previousCamera_;
        Ptr<ID3D11ShaderResourceView> parameterView_;
        // Composite-time composition: fixed pipeline objects, and the native
        // G-buffer views and camera buffers captured by this frame's ambient
        // preparation (released at the next preparation or device reset).
        struct ComposeTarget {Ptr<ID3D11Resource> resource;Ptr<ID3D11RenderTargetView> view;};
        Ptr<ID3D11VertexShader> composeVertex_;Ptr<ID3D11PixelShader> composePixel_;
        Ptr<ID3D11BlendState> composeBlend_;Ptr<ID3D11RasterizerState> composeRaster_;Ptr<ID3D11DepthStencilState> composeDepth_;
        std::array<Ptr<ID3D11Buffer>,2> composeConstants_;
        std::array<ComposeTarget,2> composeTargets_;
        std::array<Ptr<ID3D11ShaderResourceView>,3> frameNative_;
        Ptr<ID3D11ShaderResourceView> frameAuthored_,frameSurface_;
        Ptr<ID3D11Buffer> frameStereo_,frameCamera_;
        std::array<Texture,2> positions_,normals_,diffuse_,specular_;
        Texture rawDiffuse_,rawSpecular_,radiance_;
        Constants uploaded_{};
        UINT width_{},height_{},current_{};
        std::uint64_t frame_{},preparedFrame_{},radianceFrame_{},composedFrame_{},nextRetry_{};
        bool ready_{},historyValid_{},loggedFailure_{},loggedActive_{},loggedComposeFailure_{};
        std::atomic_bool enabled_{},gi_{true},specularEnabled_{},temporal_{true},reset_{true};
        std::atomic<float> radius_{256},thickness_{32},aoPower_{2},strength_{1};
        std::atomic_uint64_t ambientScopes_{},preparedFrames_{},radianceCaptures_{},composedFrames_{},composeFailures_{};
        std::array<std::atomic_uint64_t,kRejectionCount> rejections_{};
        std::array<std::atomic_uint32_t,3> lastRejectedFormats_{};
    };
}
