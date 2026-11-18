#pragma once
#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "Features/pbr/PbrSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
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
    // Pixel-shader views bound at t57..t65 for the ambient and composite consumers.
    inline constexpr UINT kConsumerFirstSlot = 57;
    inline constexpr std::size_t kConsumerViewCount = 9;
    using ConsumerViews = std::array<ID3D11ShaderResourceView*, kConsumerViewCount>;

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
        ScopedBindings(ID3D11DeviceContext* context,const ConsumerViews& views) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,kConsumerViewCount> previous_;
    };
    // Thread model: applySettings runs on the settings owner's thread and only
    // writes pending_ under settingsMutex_; every other entry point runs on the
    // render thread that owns the immediate context, which copies pending_
    // once per frame in beginFrame.
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
        // Called with the native DFComposite bound, before it draws. Capture
        // first: it records this frame's direct and ambient light for the next
        // frame's bounce, before composeOcclusion scales the accumulators by
        // the root of the occlusion and adds the bounce.
        void captureRadiance(ID3D11DeviceContext* context,bool tiled) noexcept;
        void composeOcclusion(ID3D11DeviceContext* context,bool tiled) noexcept;
        // True while that pass sets and restores pipeline state, so the D3D11
        // hooks pass its calls straight through instead of re-deriving the
        // active native draw from the pass's own shaders.
        [[nodiscard]] static bool internalRenderActive() noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;
    private:
        static constexpr UINT kMipCount=5;
        enum class Pass : std::uint8_t {prepare,prefilterPosition,reproject,prefilterRadiance,trace,blur,capture,count};
        bool reject(Rejection reason) noexcept;
        Runtime()=default;
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        struct Texture
        {
            Ptr<ID3D11Texture2D> texture;
            Ptr<ID3D11ShaderResourceView> view;
            Ptr<ID3D11UnorderedAccessView> output;
            // Mipped textures: mip 0 alone as the prefilter input, and one UAV
            // per lower mip (index 0 is mip 1).
            Ptr<ID3D11ShaderResourceView> topView;
            std::array<Ptr<ID3D11UnorderedAccessView>,kMipCount-1> mipOutputs;
        };
        // Mirrors cbuffer Settings in ScreenSpaceGICommon.hlsli; also uploaded
        // as the consumers' parameter buffer, one float4 per row.
        struct Constants
        {
            UINT width{},height{},frame{},history{};
            float originX{},originY{},viewportWidth{},viewportHeight{};
            float aoRadius{},thickness{},aoPower{},strength{};
            UINT gi{},specular{},temporal{},tiled{};
            float effectRadius{},giRadius{},minScreenRadius{},saturation{};
            float distanceCompensation{},compensationMaxDistance{},fadeStart{},fadeScale{};
            float depthDisocclusion{},blurRadius{},distanceNormalisation{};UINT maxAccumFrames{};
            UINT slices{},steps{},blur{},resolutionMode{};
            pbr::FrameData material{};
        };
        static_assert(sizeof(Constants)==192);
        bool failAllocation() noexcept;
        bool ensureResources(UINT width,UINT height) noexcept;
        bool createNoise() noexcept;
        bool prepareAmbient(ID3D11DeviceContext* context, ID3D11ShaderResourceView* authored, const pbr::FrameData& material, ID3D11ShaderResourceView* surface) noexcept;
        bool createTexture(Texture& texture,DXGI_FORMAT format,UINT width,UINT height,UINT mips=1) noexcept;
        bool createComposeResources() noexcept;
        [[nodiscard]] ID3D11RenderTargetView* composeTarget(std::size_t index,ID3D11ShaderResourceView* accumulator) noexcept;
        [[nodiscard]] ConsumerViews consumerViews(ID3D11ShaderResourceView* albedo,ID3D11ShaderResourceView* authored,ID3D11ShaderResourceView* surface) const noexcept;
        void dispatch(Pass pass,std::initializer_list<ID3D11ShaderResourceView*> inputs,
            std::initializer_list<ID3D11UnorderedAccessView*> outputs,UINT width,UINT height) noexcept;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        std::array<Ptr<ID3D11ComputeShader>,static_cast<std::size_t>(Pass::count)> shaders_;
        Ptr<ID3D11Buffer> constants_,previousCamera_;
        Ptr<ID3D11ShaderResourceView> parameterView_,noise_;
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
        // The native emissive target seen at the last composite; the G-buffer
        // pass rewrites it before the next ambient pass, whose trace reads its
        // alpha as vertex AO.
        Ptr<ID3D11ShaderResourceView> emissive_;
        // History ping-pongs on current_; remapped and traced are per-frame.
        std::array<Texture,2> positions_,normals_,luma_,chroma_,specular_;
        Texture remappedLuma_,remappedChroma_,remappedSpecular_,tracedLuma_,tracedChroma_;
        Texture radiance_,radianceMips_;
        Constants uploaded_{};
        Settings settings_{};
        UINT width_{},height_{},current_{};
        std::uint64_t frame_{},preparedFrame_{},radianceFrame_{},composedFrame_{},nextRetry_{};
        bool ready_{},historyValid_{},loggedFailure_{},loggedActive_{},loggedComposeFailure_{};
        std::mutex settingsMutex_;
        Settings pending_{};
        std::atomic_bool enabled_{},reset_{true};
        std::atomic_uint64_t ambientScopes_{},preparedFrames_{},radianceCaptures_{},composedFrames_{},composeFailures_{};
        std::array<std::atomic_uint64_t,kRejectionCount> rejections_{};
        std::array<std::atomic_uint32_t,3> lastRejectedFormats_{};
    };
}
