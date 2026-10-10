#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
// Only the native producer hook is outside this real GPU runtime fixture.
namespace csah::render {void setDFPrePassSurfaceClassificationEnabled(bool) noexcept {}}
namespace {
    struct F4 {float x,y,z,w;};
    void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    void check(HRESULT hr,const char* message){require(SUCCEEDED(hr),message);}
    void run(D3D_DRIVER_TYPE driver) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        constexpr UINT width=1024,height=2;
        std::vector<F4> pixels(width*height,{0,0,0,.37f});
        pixels[100]={4,2,1,.37f};pixels[width+100]=pixels[100];
        pixels[600]={1,2,4,.37f};pixels[width+600]=pixels[600];
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        auto texture=[&](const std::vector<F4>& data,ComPtr<ID3D11Texture2D>& resource){D3D11_SUBRESOURCE_DATA initial{data.data(),width*sizeof(F4),0};ComPtr<ID3D11ShaderResourceView> view;check(device->CreateTexture2D(&desc,&initial,&resource),"texture");check(device->CreateShaderResourceView(resource.Get(),nullptr,&view),"view");return view;};
        std::array<ComPtr<ID3D11Texture2D>,5> resources;
        auto directional=texture(pixels,resources[0]),tiled=texture(pixels,resources[1]);
        auto& surface=csah::surface_classification::Runtime::get();surface.onDeviceCreated(device.Get(),context.Get());
        surface.setConsumerEnabled(csah::surface_classification::Consumer::subsurfaceScattering,true);
        const std::array<DXGI_FORMAT,6> formats{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R16G16_FLOAT};
        std::array<ComPtr<ID3D11RenderTargetView>,6> gbuffer;std::array<ID3D11RenderTargetView*,6> rawGbuffer{};
        for(unsigned i=0;i<6;i++) {auto d=desc;d.Format=formats[i];d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> attachment;check(device->CreateTexture2D(&d,nullptr,&attachment),"G-buffer");check(device->CreateRenderTargetView(attachment.Get(),nullptr,&gbuffer[i]),"G-buffer target");rawGbuffer[i]=gbuffer[i].Get();}
        surface.markWorldFrameConsumed();auto binding=surface.prepareGBufferBinding(context.Get(),6,rawGbuffer.data());require(bool(binding),"classified G-buffer");
        ComPtr<ID3D11Resource> classResource;surface.shaderResourceView()->GetResource(&classResource);
        std::vector<unsigned char> codes(width*height,3);codes[100]=0;context->UpdateSubresource(classResource.Get(),0,nullptr,codes.data(),width,0);
        const auto clears=surface.snapshot().targetClears;
        binding=surface.prepareGBufferBinding(context.Get(),6,rawGbuffer.data());require(surface.snapshot().targetClears==clears,"same-frame bind cleared classification");
        // A complete new frame without a directional draw must clear old skin IDs.
        surface.markWorldFrameConsumed();binding=surface.prepareGBufferBinding(context.Get(),6,rawGbuffer.data());require(surface.snapshot().targetClears==clears+1,"sunless next frame kept old classification");
        D3D11_TEXTURE2D_DESC maskDesc=desc;maskDesc.Format=DXGI_FORMAT_R8_UNORM;maskDesc.BindFlags=0;maskDesc.Usage=D3D11_USAGE_STAGING;maskDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> maskRead;check(device->CreateTexture2D(&maskDesc,nullptr,&maskRead),"mask readback");context->CopyResource(maskRead.Get(),classResource.Get());D3D11_MAPPED_SUBRESOURCE mask{};check(context->Map(maskRead.Get(),0,D3D11_MAP_READ,0,&mask),"mask map");require(static_cast<const unsigned char*>(mask.pData)[600]==0,"stale skin mask survived clear");context->Unmap(maskRead.Get(),0);
        context->UpdateSubresource(classResource.Get(),0,nullptr,codes.data(),width,0);
        auto albedo=texture(std::vector<F4>(width*height,{1,1,1,1}),resources[3]);
        auto depth=texture(std::vector<F4>(width*height,{.5f,0,0,0}),resources[4]);
        auto* raw=albedo.Get();context->PSSetShaderResources(0,1,&raw);raw=depth.Get();context->PSSetShaderResources(3,1,&raw);
        raw=directional.Get();context->PSSetShaderResources(5,1,&raw);raw=tiled.Get();context->PSSetShaderResources(11,1,&raw);
        // Compute state from another feature must survive the scattering dispatches.
        raw=albedo.Get();context->CSSetShaderResources(0,1,&raw);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"staging");
        auto read=[&](unsigned source){context->CopyResource(staging.Get(),resources[source].Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback");std::vector<F4> result(width*height);for(UINT y=0;y<height;y++)std::memcpy(result.data()+y*width,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,width*sizeof(F4));context->Unmap(staging.Get(),0);return result;};
        auto& runtime=csah::subsurface_scattering::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());runtime.applySettings({true,1,5,.003f});runtime.beginFrame();
        const auto before=runtime.snapshot().executions;runtime.executeBeforeWorldComposite(context.Get(),true);
        require(runtime.snapshot().executions==before+2,"both completed light buffers were not scattered");
        const auto a=read(0),b=read(1);
        for(unsigned i=0;i<width*height;i++) {require(std::isfinite(a[i].x) && a[i].w==pixels[i].w,"invalid scattering or alpha changed");require(std::abs(a[i].x-b[i].x)<1e-6f,"tiled and directional scattering diverged");}
        require(a[599].x>0 && a[600].x<pixels[600].x,"interior lamp light was not scattered");
        require(a[100].x==pixels[100].x,"non-skin receiver changed");
        runtime.executeBeforeWorldComposite(context.Get(),true);require(runtime.snapshot().executions==before+2,"duplicate composite scattered twice");
        for(UINT slot:{0u,3u,5u,11u}) {ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(slot,1,&restored);require(restored.Get()==(slot==0?albedo.Get():slot==3?depth.Get():slot==5?directional.Get():tiled.Get()),"composite input leaked");}
        ComPtr<ID3D11ShaderResourceView> restored;context->CSGetShaderResources(0,1,&restored);require(restored==albedo,"compute state leaked");
        runtime.beginFrame();runtime.executeBeforeWorldComposite(context.Get(),false);require(runtime.snapshot().executions==before+3,"next frame did not scatter non-tiled diffuse");
        runtime.applySettings({false});runtime.beginFrame();runtime.executeBeforeWorldComposite(context.Get(),true);require(runtime.snapshot().executions==before+3,"disabled scattering ran");
        context->ClearState();runtime.onDeviceCreated(nullptr,nullptr);surface.onDeviceCreated(nullptr,nullptr);
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": completed local/directional scattering, once-per-frame ownership, class/alpha protection and state restoration passed\n";
    }
}
int main(){try{run(D3D_DRIVER_TYPE_WARP);run(D3D_DRIVER_TYPE_HARDWARE);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
