#include "Features/inverse_square/InverseSquareRuntime.h"
#include "InverseSquareShaders.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace
{
    struct F4 {float x,y,z,w;};
    void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
    void check(HRESULT hr,const char* message) {require(SUCCEEDED(hr),message);}
    std::vector<char> bytes(const std::filesystem::path& p) {std::ifstream f(p,std::ios::binary);require(bool(f),"fixture missing");return {std::istreambuf_iterator<char>(f),{}};}
    void run(const std::filesystem::path& generated,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        for(const auto& pair:kInverseSquareShaders) {
            if(pair.compute) {ComPtr<ID3D11ComputeShader> a,b;check(device->CreateComputeShader(pair.original,pair.originalSize,nullptr,&a),"native CS creation");check(device->CreateComputeShader(pair.replacement,pair.replacementSize,nullptr,&b),"replacement CS creation");}
            else {ComPtr<ID3D11PixelShader> a,b;check(device->CreatePixelShader(pair.original,pair.originalSize,nullptr,&a),"native PS creation");check(device->CreatePixelShader(pair.replacement,pair.replacementSize,nullptr,&b),"replacement PS creation");}
        }
        const char* source="struct O{float4 p:SV_Position;nointerpolation uint eye:EYEINDEX;};O main(uint id:SV_VertexID,uint eye:SV_InstanceID){float2 p[6]={float2(0,0),float2(1,0),float2(0,1),float2(1,0),float2(1,1),float2(0,1)};O o;o.p=float4(p[id].x+eye-1,1-2*p[id].y,0,1);o.eye=eye;return o;}";
        ComPtr<ID3DBlob> code,error;check(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error),"fixture VS");ComPtr<ID3D11VertexShader> vertex;check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"VS creation");
        auto buffer=[&](UINT size,UINT flags){D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=flags;ComPtr<ID3D11Buffer> b;check(device->CreateBuffer(&d,nullptr,&b),"buffer");return b;};
        auto camera=buffer(50*16,D3D11_BIND_CONSTANT_BUFFER),light=buffer(30*16,D3D11_BIND_CONSTANT_BUFFER),stereo=buffer(16,D3D11_BIND_CONSTANT_BUFFER),tiled=buffer(9*16,D3D11_BIND_CONSTANT_BUFFER);
        std::array<F4,50> cameraData{};for(unsigned eye=0;eye<2;eye++){auto row=32+eye*4;cameraData[row+2].w=100;cameraData[row+3].w=1;}
        std::array<F4,30> lightData{};lightData[0]={.125f,.125f,0,0};lightData[1].w=lightData[2].w=1024;lightData[3]={1,1,1,0};lightData[4]={0,1,2,0};lightData[29]={1,1,0,0};
        const F4 stereoData{1,0,0,0};context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);context->UpdateSubresource(light.Get(),0,nullptr,lightData.data(),0,0);
        auto* cb=camera.Get();context->PSSetConstantBuffers(12,1,&cb);context->CSSetConstantBuffers(12,1,&cb);cb=light.Get();context->PSSetConstantBuffers(2,1,&cb);cb=stereo.Get();context->PSSetConstantBuffers(8,1,&cb);
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        auto texture=[&](F4 value){std::array<F4,64> pixels;pixels.fill(value);D3D11_SUBRESOURCE_DATA init{pixels.data(),8*sizeof(F4),0};ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> v;check(device->CreateTexture2D(&td,&init,&t),"fixture texture");check(device->CreateShaderResourceView(t.Get(),nullptr,&v),"fixture SRV");return v;};
        auto albedo=texture({1,1,1,1}),normal=texture({.5f,.5f,0,0}),material=texture({.5f,0,0,0}),depth=texture({.5f,.5f,.5f,.5f});
        ID3D11ShaderResourceView* psInputs[]{albedo.Get(),normal.Get(),material.Get(),depth.Get()};context->PSSetShaderResources(0,4,psInputs);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler),"sampler");ID3D11SamplerState* samplers[]{sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,4,samplers);
        td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;std::array<ComPtr<ID3D11Texture2D>,2> targets;std::array<ComPtr<ID3D11RenderTargetView>,2> rtvs;std::array<ComPtr<ID3D11UnorderedAccessView>,2> uavs;
        for(unsigned i=0;i<2;i++){check(device->CreateTexture2D(&td,nullptr,&targets[i]),"output");check(device->CreateRenderTargetView(targets[i].Get(),nullptr,&rtvs[i]),"RTV");check(device->CreateUnorderedAccessView(targets[i].Get(),nullptr,&uavs[i]),"UAV");}
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&td,nullptr,&staging),"readback");
        context->VSSetShader(vertex.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT viewport{0,0,8,8,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
        auto& runtime=csah::inverse_square::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());runtime.setDispatchReady(true);csah::inverse_square::Settings settings;
        auto readback=[&](){std::array<F4,128> out;for(unsigned i=0;i<2;i++){context->CopyResource(staging.Get(),targets[i].Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"readback map");for(unsigned y=0;y<8;y++)std::memcpy(out.data()+i*64+y*8,static_cast<const char*>(m.pData)+m.RowPitch*y,8*sizeof(F4));context->Unmap(staging.Get(),0);}return out;};
        auto a=bytes(generated/"PS_00000004_original.dxbc"),b=bytes(generated/"PS_00000004_replacement.dxbc");ComPtr<ID3D11PixelShader> nativePS,replacementPS;check(device->CreatePixelShader(a.data(),a.size(),nullptr,&nativePS),"native PS");check(device->CreatePixelShader(b.data(),b.size(),nullptr,&replacementPS),"replacement PS");
        require(!csah::inverse_square::taggedShader(nativePS.Get()) && csah::inverse_square::tagShader(replacementPS.Get()) && csah::inverse_square::taggedShader(replacementPS.Get()),"shader tags");
        auto pixelDraw=[&](bool replacement){context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);ID3D11RenderTargetView* r[]{rtvs[0].Get(),rtvs[1].Get()};context->OMSetRenderTargets(2,r,nullptr);context->PSSetShader(replacement?replacementPS.Get():nativePS.Get(),nullptr,0);{auto scope=runtime.scope(context.Get(),false,replacement);context->DrawInstanced(6,2,0,0);}context->OMSetRenderTargets(0,nullptr,nullptr);ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(78,1,&restored);require(!restored,"pixel t78 leaked");return readback();};
        auto compare=[&](const auto& original,const auto& changed,float expectedRatio){require(original[9].x>1e-8f && original[13].x>1e-8f,"unlit fixture");for(unsigned i=0;i<128;i++){require(std::isfinite(changed[i].x),"nonfinite light");require(std::abs(changed[i].x-original[i].x*expectedRatio)<std::max(1e-7f,std::abs(original[i].x*expectedRatio)*.002f),"native attenuation ratio mismatch");require(changed[i].w==original[i].w,"native alpha changed");}};
        runtime.applySettings(settings);const auto native=pixelDraw(false);compare(native,pixelDraw(true),1);
        settings.enabled=true;runtime.applySettings(settings);const auto corrected=pixelDraw(true);
        const float nativeFalloff=std::pow(1-100.0f*100/(1024.0f*1024),2.2f);const float ratio=(3920/(100.0f*100+3920))/nativeFalloff;compare(native,corrected,ratio);
        runtime.setDispatchReady(false);compare(native,pixelDraw(true),1);runtime.setDispatchReady(true);
        settings.enabled=false;runtime.applySettings(settings);compare(native,pixelDraw(true),1);
        settings.enabled=true;settings.size=2;settings.intensity=2;runtime.applySettings(settings);
        compare(native,pixelDraw(true),(3920/(10000.0f+1960*4)*2)/nativeFalloff);
        settings.size=1.41421356237f;settings.intensity=1;runtime.applySettings(settings);
        cameraData[34].w=cameraData[38].w=1000;
        const auto edgeNative=pixelDraw(false);const float t=24.0f/252;
        const float edgeRatio=(3920/(1000000.0f+3920)*t*t*(3-2*t))/std::pow(1-1000000.0f/(1024*1024),2.2f);
        compare(edgeNative,pixelDraw(true),edgeRatio);
        cameraData[34].w=cameraData[38].w=1200;const auto outside=pixelDraw(true);
        for(const auto& value:outside) require(value.x==0,"light escaped native radius");
        cameraData[34].w=cameraData[38].w=100;context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);
        // Native tiled records are 64 bytes in VR (the flat donor uses a
        // different layout). This fixture uses the unchanged native radius.
        std::array<float,16> record{};record[4]=record[9]=1024;record[10]=record[11]=record[12]=1;record[14]=1;record[15]=2;
        std::array<UINT,128> list{};list[0]=1;
        auto structured=[&](const void* data,UINT stride){D3D11_BUFFER_DESC d{};d.ByteWidth=d.StructureByteStride=stride;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> resource;ComPtr<ID3D11ShaderResourceView> view;check(device->CreateBuffer(&d,&initial,&resource),"structured buffer");check(device->CreateShaderResourceView(resource.Get(),nullptr,&view),"structured SRV");return view;};
        auto lights=structured(record.data(),64),tiles=structured(list.data(),512);std::array<F4,9> tiledData{};tiledData[0]={0,0,.125f,.125f};tiledData[3]={0,1,1,0};context->UpdateSubresource(tiled.Get(),0,nullptr,tiledData.data(),0,0);cb=tiled.Get();context->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* csInputs[]{depth.Get(),albedo.Get(),normal.Get(),nullptr,material.Get(),nullptr,lights.Get(),tiles.Get()};context->CSSetShaderResources(0,8,csInputs);
        for(const auto* key:{"00000001","00000002"}) {
            a=bytes(generated/(std::string("CS_")+key+"_original.dxbc"));b=bytes(generated/(std::string("CS_")+key+"_replacement.dxbc"));ComPtr<ID3D11ComputeShader> oldCS,newCS;check(device->CreateComputeShader(a.data(),a.size(),nullptr,&oldCS),"native CS");check(device->CreateComputeShader(b.data(),b.size(),nullptr,&newCS),"replacement CS");
            auto compute=[&](bool replacement){context->CSSetShader(replacement?newCS.Get():oldCS.Get(),nullptr,0);ID3D11UnorderedAccessView* outputs[]{uavs[0].Get(),uavs[1].Get()};context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);{auto scope=runtime.scope(context.Get(),true,replacement);context->Dispatch(1,1,1);}ID3D11UnorderedAccessView* nulls[2]{};context->CSSetUnorderedAccessViews(0,2,nulls,nullptr);ComPtr<ID3D11ShaderResourceView> restored;context->CSGetShaderResources(78,1,&restored);require(!restored,"compute t78 leaked");return readback();};
            settings.enabled=false;runtime.applySettings(settings);const auto baseline=compute(false);compare(baseline,compute(true),1);
            settings.enabled=true;runtime.applySettings(settings);compare(baseline,compute(true),ratio);
        }
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": all 73 native/replacement shaders, real stereo PS and both tiled CS falloff, disable parity, alpha and binding restoration passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"generated directory required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
