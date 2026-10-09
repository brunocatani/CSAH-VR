#include "Features/dlaa/DlaaRuntime.h"
#include "D3D11AllocationFailure.h"
#include <d3d11sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <vector>

namespace
{
    bool optimalAvailable = true;
    unsigned evaluations{};
    void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
    HRESULT STDMETHODCALLTYPE failTexture2D(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
        const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D** output)
    { if (output) *output = nullptr; return E_OUTOFMEMORY; }
}

// Exercise the real rendering/recovery code on D3D11. Only the vendor neural
// evaluator is substituted; this is not a visual-quality or NGX runtime test.
namespace csah::dlaa
{
    StreamlineSnapshot streamlineSnapshot() noexcept { return {true,true,true,true,true,true}; }
    void releaseStreamlineDlaaResources() noexcept {}
    StreamlineOptimalSettings queryStreamlineOptimalSettings(Mode, std::uint32_t w, std::uint32_t h) noexcept
    { return {optimalAvailable, w / 2, h / 2}; }
    bool evaluateStreamlineDlaa(const StreamlineDlaaFrame& frame) noexcept
    {
        ++evaluations;
        for (unsigned eye=0; eye<2; ++eye) {
            if (!frame.resources[eye].colorInput || !frame.resources[eye].colorOutput ||
                !frame.resources[eye].depth || !frame.resources[eye].motionVectors) return false;
            if (frame.timingAfterEye[eye]) frame.context->End(frame.timingAfterEye[eye]);
        }
        return true;
    }
}

namespace csah::diagnostics { void processRenderDocCapture() noexcept {} }

int main(int argc, char** argv)
{
    try {
        using Microsoft::WRL::ComPtr;
        using namespace csah::dlaa;
        require(argc == 2, "expected repository path");
        std::ifstream fixture(std::filesystem::path(argv[1]) / "tests/fixtures/fo4vr_world_camera_without_first_person.txt");
        Fo4VrFrameBuffer camera{};
        std::string word;
        std::size_t count{};
        while (fixture >> word) {
            if (word.starts_with('#')) { std::getline(fixture, word); continue; }
            require(count < sizeof(camera)/sizeof(float), "fixture overflow");
            reinterpret_cast<float*>(&camera)[count++] = std::stof(word);
        }
        require(count == sizeof(camera)/sizeof(float), "camera fixture missing/truncated");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)), "WARP debug device failed");
        ComPtr<ID3D11InfoQueue> debug;
        require(SUCCEEDED(device.As(&debug)), "debug queue missing");
        auto& runtime=Runtime::get();
        Settings settings{};
        settings.motionVectorRepair=false;
        runtime.applySettings(settings);
        runtime.onDeviceCreated(device.Get(),context.Get());
        // More than the old five-attempt budget may elapse in menus/loading.
        for (unsigned i=0;i<361;++i) runtime.onPostImageSpace();
        require(!runtime.snapshot().renderResourcesQualified, "empty context qualified");
        constexpr UINT width=128, height=64;
        auto texture=[&](DXGI_FORMAT format,UINT flags) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=format; desc.BindFlags=flags; desc.Usage=D3D11_USAGE_DEFAULT;
            ComPtr<ID3D11Texture2D> result;
            require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&result)), "texture creation failed");
            return result;
        };
        constexpr auto colorFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        auto scene=texture(DXGI_FORMAT_R11G11B10_FLOAT,colorFlags);
        auto mask=texture(DXGI_FORMAT_R8G8B8A8_UNORM,colorFlags);
        auto output=texture(DXGI_FORMAT_R8G8B8A8_UNORM,colorFlags); // No UAV flag is needed for copy destination.
        auto motion=texture(DXGI_FORMAT_R16G16_FLOAT,colorFlags);
        auto depth=texture(DXGI_FORMAT_R24G8_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> sceneTarget,maskTarget,outputTarget,motionTarget;
        require(SUCCEEDED(device->CreateRenderTargetView(scene.Get(),nullptr,&sceneTarget)) &&
            SUCCEEDED(device->CreateRenderTargetView(mask.Get(),nullptr,&maskTarget)) &&
            SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&outputTarget)) &&
            SUCCEEDED(device->CreateRenderTargetView(motion.Get(),nullptr,&motionTarget)), "RTVs failed");
        const float black[4]{};
        for (auto* view : {sceneTarget.Get(),maskTarget.Get(),outputTarget.Get(),motionTarget.Get()})
            context->ClearRenderTargetView(view,black);
        ComPtr<ID3D11ShaderResourceView> sceneView,motionView,depthView;
        require(SUCCEEDED(device->CreateShaderResourceView(scene.Get(),nullptr,&sceneView)) &&
            SUCCEEDED(device->CreateShaderResourceView(motion.Get(),nullptr,&motionView)), "color SRVs failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels=1;
        require(SUCCEEDED(device->CreateShaderResourceView(depth.Get(),&srv,&depthView)), "depth SRV failed");
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11DepthStencilView> depthTarget;
        require(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),&dsv,&depthTarget)), "DSV failed");
        context->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
        ComPtr<ID3D11Buffer> cameraBuffer;
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(camera); cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        cb.Usage=D3D11_USAGE_DYNAMIC; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        require(SUCCEEDED(device->CreateBuffer(&cb,nullptr,&cameraBuffer)), "camera allocation failed");
        auto* cameraPtr=cameraBuffer.Get();
        context->PSSetConstantBuffers(12,1,&cameraPtr);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
        auto bindPost=[&](bool valid) {
            auto vp=viewport; if (!valid) vp.Width=16;
            context->RSSetViewports(1,&vp);
            ID3D11RenderTargetView* targets[]{maskTarget.Get(),outputTarget.Get()};
            // Upscaling consumes the depth SRV; a bound depth-stencil target
            // is unnecessary at this post-processing boundary.
            context->OMSetRenderTargets(2,targets,nullptr);
            ID3D11ShaderResourceView* views[]{sceneView.Get(),nullptr,motionView.Get(),depthView.Get()};
            context->PSSetShaderResources(0,4,views);
        };
        bindPost(true);
        for (unsigned i=0;i<61;++i) runtime.onPostImageSpace();
        alignas(16) std::array<std::byte,0x1690> manager{};
        const float one=1;
        std::memcpy(manager.data()+0x1664,&one,4); std::memcpy(manager.data()+0x1668,&one,4);
        auto frame=[&](bool validViewport=true) {
            float jitterX{},jitterY{};
            runtime.onPreRender(manager.data(),&jitterX,&jitterY);
            ID3D11ShaderResourceView* nulls[4]{}; context->PSSetShaderResources(0,4,nulls);
            auto* target=sceneTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(cameraPtr,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)), "camera map failed");
            std::memcpy(mapped.pData,&camera,sizeof(camera));
            runtime.onMapSucceeded(context.Get(),cameraPtr,0,D3D11_MAP_WRITE_DISCARD,mapped);
            runtime.onBeforeUnmap(context.Get(),cameraPtr,0); context->Unmap(cameraPtr,0);
            runtime.onWorldCompositeDraw(context.Get());
            bindPost(validViewport); runtime.onPostImageSpace();
        };
        for (unsigned i=0;i<4;++i) frame();
        require(runtime.snapshot().operational && runtime.snapshot().committedFrames>0,
            "black scene/no shader registration/late resources did not qualify");
        const auto before=evaluations;
        frame(false);
        require(!runtime.snapshot().operational && evaluations==before,"wrong viewport was evaluated");
        frame(); frame();
        require(runtime.snapshot().operational && evaluations>before,"viewport interruption latched a permanent failure");
        settings.mode=Mode::dlssQuality; optimalAvailable=false; runtime.applySettings(settings);
        frame(); const auto beforeQueryRecovery=evaluations;
        optimalAvailable=true;
        for (unsigned i=0;i<64;++i) frame();
        require(runtime.snapshot().operational && evaluations>beforeQueryRecovery,"temporary optimal-size query failure did not recover");
        settings.mode=Mode::dlssBalanced; runtime.applySettings(settings);
        frame(); // Establish the new size before the pre-render scale owner runs.
        {
            DeviceFailureScope failure(device.Get(), 5, reinterpret_cast<void*>(&failTexture2D));
            frame();
            require(!runtime.snapshot().operational,"failed eye allocation remained operational");
        }
        float restoredWidth{}; std::memcpy(&restoredWidth,manager.data()+0x1664,4);
        require(restoredWidth==1,"allocation retry left vanilla rendering at DLSS input resolution");
        for (unsigned i=0;i<64;++i) frame();
        require(runtime.snapshot().operational,"temporary eye allocation failure did not recover");
        for (UINT64 i=0;i<debug->GetNumStoredMessages();++i) {
            SIZE_T bytes{}; debug->GetMessage(i,nullptr,&bytes); std::vector<std::byte> data(bytes);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data()); debug->GetMessage(i,message,&bytes);
            if (message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(message->pDescription);
        }
        runtime.onDeviceCreated(nullptr,nullptr); context->ClearState();
        std::cout<<"Real upscaler resource discovery and recovery passed on WARP; vendor evaluation substituted.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
