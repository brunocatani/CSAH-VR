#include "Features/water_effects/WaterEffectsRuntime.h"
#include "ContactShadowsDFLight.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace
{
    struct V {float x{},y{},z{},w{};};
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
        for(const auto& contract:csah_contact_shadow_dflight_contracts) {
            ComPtr<ID3D11PixelShader> candidate;
            check(device->CreatePixelShader(contract.replacementBytecode,contract.replacementBytecodeLength,nullptr,&candidate),"directional caustics composition rejected");
        }
        const std::string source=R"(
#include "WaterCaustics.hlsli"
cbuffer Fixture:register(b0){float4 Test;};
float4 VS(uint id:SV_VertexID):SV_POSITION{float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}
float4 PS(float4 pixel:SV_POSITION):SV_Target{
float localX=pixel.x<32?pixel.x:pixel.x-32;
float3 world=float3(Test.x+localX*32,Test.y+pixel.y*32,Test.z);
return float4(WaterCausticMultiplier(world),.375);})";
        const auto path=root/"package/Shaders/Community/WaterEffects/CausticFixture.hlsl";
        const auto compile=[&](const char* entry,const char* target) {ComPtr<ID3DBlob> code,error;auto status=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);if(FAILED(status)&&error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());check(status,"fixture compile");return code;};
        const auto v=compile("VS","vs_5_0"),p=compile("PS","ps_5_0");ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"VS");check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps),"PS");
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_TEXTURE2D_DESC td{};td.Width=64;td.Height=32;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateTexture2D(&td,nullptr,&target),"target");check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging),"staging");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());D3D11_VIEWPORT viewport{0,0,64,32,0,1};context->RSSetViewports(1,&viewport);
        std::array<V,28> data{};data[1]={1,1,0,1};data[2]={-1,0,0,0};data[15]={-1,0,500,1};V fixture{-1024,128,372,0};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(data);bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Buffer> parameters,constants;check(device->CreateBuffer(&bd,nullptr,&parameters),"parameters");D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=28;ComPtr<ID3D11ShaderResourceView> view;check(device->CreateShaderResourceView(parameters.Get(),&sv,&view),"parameter view");bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&constants),"fixture constants");auto* cb=constants.Get();context->PSSetConstantBuffers(0,1,&cb);
        auto& runtime=csah::water_effects::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());csah::water_effects::Settings settings;settings.enabled=true;runtime.applySettings(settings);
        const auto render=[&](bool overrideParameters) {
            context->UpdateSubresource(parameters.Get(),0,nullptr,data.data(),0,0);context->UpdateSubresource(constants.Get(),0,nullptr,&fixture,0,0);
            auto* output=rtv.Get();context->OMSetRenderTargets(1,&output,nullptr);
            {auto scope=runtime.scopeCaustics(context.Get());if(overrideParameters) {auto* srv=view.Get();context->PSSetShaderResources(66,1,&srv);}context->Draw(3,0);}
            ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(65,1,&restored);require(!restored,"caustic texture leaked");ComPtr<ID3D11SamplerState> restoredSampler;context->PSGetSamplers(15,1,&restoredSampler);require(!restoredSampler,"caustic sampler leaked");
            context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"readback");std::vector<V> pixels(64*32);for(UINT y=0;y<32;y++)std::memcpy(pixels.data()+y*64,static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch,64*sizeof(V));context->Unmap(staging.Get(),0);return pixels;
        };
        const auto neutral=[](const std::vector<V>& pixels) {for(auto value:pixels) require(value.x==1&&value.y==1&&value.z==1&&value.w==.375f,"non-submerged or unavailable water changed lighting");};
        neutral(render(false)); // No native world frame has been published.
        auto active=render(true);float minimum=4,maximum=0;
        for(UINT y=0;y<32;y++)for(UINT x=0;x<32;x++) {
            const auto a=active[y*64+x],b=active[y*64+x+32];require(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&a.x>=0&&a.x<=4&&a.y>=0&&a.y<=4&&a.z>=0&&a.z<=4&&a.w==.375f,"caustic bounds or alpha");
            require(std::abs(a.x-b.x)<1e-6f&&std::abs(a.y-b.y)<1e-6f&&std::abs(a.z-b.z)<1e-6f,"stereo caustic mismatch");minimum=(std::min)(minimum,a.x);maximum=(std::max)(maximum,a.x);
        }
        require(maximum-minimum>.01f,"caustic pattern missing");
        data[1].z=5;auto animated=render(true);bool changed=false;for(UINT i=0;i<active.size();i++)changed|=std::abs(active[i].x-animated[i].x)>.001f;require(changed,"caustics did not animate");
        fixture.z=600;neutral(render(true));fixture.z=500;neutral(render(true));fixture.z=372;
        data[1].y=0;neutral(render(true));data[1].y=1;data[15].w=0;fixture.z=-1000;neutral(render(true));data[15].w=1;
        fixture.x=0;neutral(render(true));fixture.x=-1024;data[1].w=0;neutral(render(true));
        settings.enabled=false;runtime.applySettings(settings);neutral(render(false));runtime.resetWorld();runtime.onDeviceCreated(nullptr,nullptr);context->ClearState();
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": BC4 mip chain, world-cell matching, submersion, colour bounds, stereo, animation, disable and binding restoration passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"source root required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
