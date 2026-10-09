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
#include <bit>

namespace
{
    bool optimalAvailable = true;
    unsigned evaluations{};
    bool verifyTemporal{};
    bool temporalConsistent{true};
    std::array<float, 2> expectedJitter{};
    float maximumJitterError{};
    unsigned verifiedInputModes{};
    bool stereoInputsCorrect{true};
    bool verifyCapturedCamera{};
    bool capturedCameraCorrect{true};
    bool jitterPatternCorrect{true};
    unsigned jitterSample{};
    std::uint32_t nativeDepthAt(unsigned x, unsigned y)
    { return std::uint32_t((0.2f + 0.004f*x + 0.001f*y)*16777215.0f); }
    std::uint32_t nativeColorAt(unsigned x, unsigned y)
    { return 0x3c0u | (x<<11) | ((y*4u)<<22); }
    std::uint32_t nativeMotionAt(unsigned x, unsigned y)
    { return (0x2000u+x*2u) | ((0xa000u+y)<<16); }
    bool readInput(ID3D11DeviceContext* context, ID3D11Texture2D* texture,
        unsigned width, unsigned height, std::vector<std::uint32_t>& pixels)
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        if (desc.Width!=width || desc.Height!=height) return false;
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
        context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
        pixels.resize(width*height);
        for (unsigned row=0;row<height;++row)
            std::memcpy(pixels.data()+row*width,static_cast<const std::byte*>(mapped.pData)+row*mapped.RowPitch,width*4);
        context->Unmap(staging.Get(),0);
        return true;
    }
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
    StreamlineOptimalSettings queryStreamlineOptimalSettings(Mode mode, std::uint32_t w, std::uint32_t h) noexcept
    {
        const float scale = mode == Mode::dlssQuality ? 2.0f/3.0f :
            mode == Mode::dlssBalanced ? 0.58f : mode == Mode::dlssPerformance ? 0.5f : 1.0f/3.0f;
        return {optimalAvailable, UINT(w * scale), UINT(h * scale)};
    }
    bool evaluateStreamlineDlaa(const StreamlineDlaaFrame& frame) noexcept
    {
        ++evaluations;
        for (unsigned eye=0; eye<2; ++eye) {
            if (!frame.resources[eye].colorInput || !frame.resources[eye].colorOutput ||
                !frame.resources[eye].depth || !frame.resources[eye].motionVectors) return false;
            if (verifyTemporal) {
                const auto error = (std::max)(std::abs(frame.constants[eye].jitterPixelsX-expectedJitter[0]),
                    std::abs(frame.constants[eye].jitterPixelsY-expectedJitter[1]));
                maximumJitterError = (std::max)(maximumJitterError,error);
                temporalConsistent = temporalConsistent && error < 0.0001f;
                temporalConsistent = temporalConsistent &&
                    std::abs(frame.constants[eye].cameraViewToClip.values[8]-(eye==0 ? 0.08f : -0.07f))<0.000001f &&
                    std::abs(frame.constants[eye].cameraViewToClip.values[9]-0.04f)<0.000001f;
                if ((verifiedInputModes & (1u<<unsigned(frame.mode)))==0) {
                    std::vector<std::uint32_t> color,depth,motion;
                    const auto w=frame.inputWidth,h=frame.inputHeight;
                    const auto readable=readInput(frame.context,frame.resources[eye].colorInput,w,h,color) &&
                        readInput(frame.context,frame.resources[eye].depth,w,h,depth) &&
                        readInput(frame.context,frame.resources[eye].motionVectors,w,h,motion);
                    stereoInputsCorrect=stereoInputsCorrect && readable;
                    if (readable) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
                        const auto sx=eye*w+x;
                        const auto expectedDepth=float(nativeDepthAt(sx,y))/16777215.0f*1.01f-0.01f;
                        const auto at=y*w+x;
                        stereoInputsCorrect=stereoInputsCorrect && color[at]==nativeColorAt(sx,y) &&
                            motion[at]==nativeMotionAt(sx,y) && std::abs(std::bit_cast<float>(depth[at])-expectedDepth)<0.000002f;
                    }
                }
            }
            if (verifyCapturedCamera) {
                // Independently measured from the recorded world upload at
                // 2688x2880 per eye, then expressed at this test's 64x64 size.
                capturedCameraCorrect=capturedCameraCorrect &&
                    std::abs(frame.constants[eye].jitterPixelsX+0.25f*64.0f/2688.0f)<0.000003f &&
                    std::abs(frame.constants[eye].jitterPixelsY+0.3888889f*64.0f/2880.0f)<0.000003f;
            }
            if (frame.timingAfterEye[eye]) frame.context->End(frame.timingAfterEye[eye]);
        }
        if (verifyTemporal) verifiedInputModes |= 1u<<unsigned(frame.mode);
        return true;
    }
}

namespace csah::diagnostics { void processRenderDocCapture() noexcept {} }

int main(int argc, char** argv)
{
    try {
        using Microsoft::WRL::ComPtr;
        using namespace csah::dlaa;
        require(argc == 2 || argc == 3, "expected repository path and optional --hardware");
        const auto hardware=argc==3 && std::string_view(argv[2])=="--hardware";
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
        const auto recordedCamera=camera;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,
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
        std::vector<std::uint32_t> depthPixels(width*height),colorPixels(width*height),motionPixels(width*height);
        for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
            depthPixels[y*width+x]=nativeDepthAt(x,y);
            colorPixels[y*width+x]=nativeColorAt(x,y);
            motionPixels[y*width+x]=nativeMotionAt(x,y);
        }
        auto texture=[&](DXGI_FORMAT format,UINT flags,const void* initial=nullptr) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=format; desc.BindFlags=flags; desc.Usage=D3D11_USAGE_DEFAULT;
            ComPtr<ID3D11Texture2D> result;
            const D3D11_SUBRESOURCE_DATA data{initial,width*4,width*height*4};
            require(SUCCEEDED(device->CreateTexture2D(&desc,initial ? &data : nullptr,&result)), "texture creation failed");
            return result;
        };
        constexpr auto colorFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        auto scene=texture(DXGI_FORMAT_R11G11B10_FLOAT,colorFlags);
        auto mask=texture(DXGI_FORMAT_R8G8B8A8_UNORM,colorFlags);
        auto output=texture(DXGI_FORMAT_R8G8B8A8_UNORM,colorFlags); // No UAV flag is needed for copy destination.
        auto motion=texture(DXGI_FORMAT_R16G16_FLOAT,colorFlags);
        auto depth=texture(DXGI_FORMAT_R24G8_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE,depthPixels.data());
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
            if (verifyTemporal) {
                float scaleX{},scaleY{};
                std::memcpy(&scaleX,manager.data()+0x1664,4);
                std::memcpy(&scaleY,manager.data()+0x1668,4);
                const auto packedWidth=std::floor(float(width)*scaleX);
                const auto renderHeight=std::floor(float(height)*scaleY);
                // Independent replay of 141DAA370: native camera generation
                // multiplies the shared offsets by display/render dimensions.
                const auto clipX=jitterX*float(width)/packedWidth;
                const auto clipY=jitterY*float(height)/renderHeight;
                expectedJitter={-clipX*packedWidth*0.25f,clipY*renderHeight*0.5f};
                if (runtime.snapshot().inputWidth!=0) {
                    constexpr std::array<float,8> xs{0,-0.25f,0.25f,-0.375f,0.125f,-0.125f,0.375f,-0.4375f};
                    constexpr std::array<float,8> ys{-1.0f/6,1.0f/6,-7.0f/18,-1.0f/18,5.0f/18,-5.0f/18,1.0f/18,7.0f/18};
                    require(jitterSample<xs.size(),"unexpected native jitter sample count");
                    jitterPatternCorrect=jitterPatternCorrect &&
                        std::abs(expectedJitter[0]-xs[jitterSample])<0.000001f &&
                        std::abs(expectedJitter[1]-ys[jitterSample])<0.000001f;
                    ++jitterSample;
                }
                const Matrix4 identity{Float4{1,0,0,0},Float4{0,1,0,0},Float4{0,0,1,0},Float4{0,0,0,1}};
                camera.view=camera.inverseView=identity;
                for (UINT eye=0;eye<2;++eye) {
                    // Distinct asymmetric eye frusta; shift only the jittered
                    // projection. The native world upload contains both.
                    const Matrix4 p{Float4{1.3f,0,eye==0 ? 0.08f : -0.07f,0},
                        Float4{0,0.9f,0.04f,0},Float4{0,0,1.0001f,-5.0005f},Float4{0,0,1,0}};
                    camera.viewProjectionUnjittered[eye]=camera.previousViewProjectionUnjittered[eye]=p;
                    camera.projection[eye]=p;
                    camera.projection[eye][0].z+=clipX;
                    camera.projection[eye][1].z+=clipY;
                    camera.viewProjection[eye]=camera.projection[eye];
                }
            }
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
        context->UpdateSubresource(scene.Get(),0,nullptr,colorPixels.data(),width*4,0);
        context->UpdateSubresource(motion.Get(),0,nullptr,motionPixels.data(),width*4,0);
        verifyTemporal=true;
        for (const auto mode : {Mode::dlssQuality,Mode::dlssBalanced,Mode::dlssPerformance,Mode::dlssUltraPerformance}) {
            jitterSample=0;
            settings.mode=mode; runtime.applySettings(settings);
            for (unsigned i=0;i<9;++i) frame();
        }
        std::cout<<"Maximum per-eye jitter declaration error: "<<maximumJitterError<<" pixels\n";
        require(temporalConsistent,"DLSS declared jitter differs from the native stereo rasterization");
        require(verifiedInputModes==0x1e && stereoInputsCorrect,"scaled stereo color/depth/motion inputs disagree");
        require(jitterPatternCorrect,"native camera did not rasterize the requested per-eye sample pattern");
        verifyTemporal=false; verifyCapturedCamera=true; camera=recordedCamera;
        for (const auto mode : {Mode::dlaa,Mode::centerDlaa}) {
            settings.mode=mode; runtime.applySettings(settings);
            for (unsigned i=0;i<4;++i) frame();
        }
        require(capturedCameraCorrect,"native/center DLAA jitter disagrees with recorded world-camera evidence");
        for (UINT64 i=0;i<debug->GetNumStoredMessages();++i) {
            SIZE_T bytes{}; debug->GetMessage(i,nullptr,&bytes); std::vector<std::byte> data(bytes);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data()); debug->GetMessage(i,message,&bytes);
            if (message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(message->pDescription);
        }
        runtime.onDeviceCreated(nullptr,nullptr); context->ClearState();
        std::cout<<"Real upscaler recovery, four scaled stereo inputs, and recorded-camera jitter passed on "
            <<(hardware ? "hardware" : "WARP")<<"; vendor evaluation substituted.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
