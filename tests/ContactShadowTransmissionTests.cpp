#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
using Microsoft::WRL::ComPtr;
struct V {float x{},y{},z{},w{};};
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void check(HRESULT result,const char* message) {require(SUCCEEDED(result),message);}
void run(const std::filesystem::path& root,bool hardware) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
    const std::string source=R"(
#define PixelInput ContactInput
#define PSMain ContactMain
#include "../ContactShadows/ContactShadowTransform.hlsl"
#undef PixelInput
#undef PSMain
#include "SkylightingDirectionalTransform.hlsl"
RWBuffer<float4> Result : register(u0);
[numthreads(4,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    ContactInput c;c.Position=float4(id.x+0.5,0.5,0,1);c.Eye=id.x&1;
    c.NormalCarrier=float4(0,0,id.x<2?1:-1,0);
    float4 visibility=ContactMain(c);
    PixelInput p;p.Position=c.Position;p.Eye=c.Eye;p.NormalCarrier=c.NormalCarrier;
    p.RimDiffuse=float4(0.2,0.3,0.4,0);p.TransmissionDiffuse=float4(0.3,0.2,0.1,0);
    p.PbrDiffuseScale=1;p.NativeVisibility=0.8;p.ContactVisibility=visibility;
    float3 nativeDiffuse=(float3(0.7,0.8,0.9)+p.RimDiffuse.xyz+p.TransmissionDiffuse.xyz)*0.8*visibility.x;
    Result[id.x]=float4(nativeDiffuse+PSMain(p).xyz,0.5*0.8*visibility.x);
})";
    const auto path=root/"package/Shaders/Community/Skylighting/TransmissionTest.hlsl";
    ComPtr<ID3DBlob> code,errors;const auto status=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
    if(FAILED(status)&&errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());check(status,"shader compile");
    ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"shader");context->CSSetShader(shader.Get(),nullptr,0);
    ComPtr<ID3D11Buffer> light,settings,output,staging;D3D11_BUFFER_DESC bd{};bd.ByteWidth=46*16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&light),"light constants");bd.ByteWidth=3*16;check(device->CreateBuffer(&bd,nullptr,&settings),"contact settings");
    std::array<V,46> sun{};sun[1]={0,0,1,0};sun[2]={0,0,-1,0};context->UpdateSubresource(light.Get(),0,nullptr,sun.data(),0,0);auto* cb=light.Get();context->CSSetConstantBuffers(2,1,&cb);cb=settings.Get();context->CSSetConstantBuffers(13,1,&cb);
    bd.ByteWidth=4*16;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;check(device->CreateBuffer(&bd,nullptr,&output),"output");D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=4;ComPtr<ID3D11UnorderedAccessView> uav;check(device->CreateUnorderedAccessView(output.Get(),&uv,&uav),"output view");auto* out=uav.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
    bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateBuffer(&bd,nullptr,&staging),"staging");
    D3D11_TEXTURE2D_DESC td{};td.Width=4;td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> mask;check(device->CreateTexture2D(&td,nullptr,&mask),"RG8 mask");ComPtr<ID3D11ShaderResourceView> view;check(device->CreateShaderResourceView(mask.Get(),nullptr,&view),"mask view");auto* srv=view.Get();context->CSSetShaderResources(46,1,&srv);
    for(unsigned scenario=0;scenario<4;++scenario) {
        const unsigned char contactByte=scenario==2?255:64,cloudByte=scenario==1?255:128;
        std::array<unsigned char,8> pixels{};for(unsigned i=0;i<4;++i) {pixels[i*2]=contactByte;pixels[i*2+1]=cloudByte;}context->UpdateSubresource(mask.Get(),0,nullptr,pixels.data(),8,0);
        std::array<V,3> params{};params[2].y=scenario==3?0.0F:1.0F;context->UpdateSubresource(settings.Get(),0,nullptr,params.data(),0,0);
        context->Dispatch(1,1,1);context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"readback");std::array<V,4> values{};std::memcpy(values.data(),m.pData,sizeof(values));context->Unmap(staging.Get(),0);
        const float contact=scenario==3?1:contactByte/255.0F,cloud=scenario==3?1:cloudByte/255.0F;
        for(unsigned i=0;i<4;++i) {const bool front=i==0||i==3;const float direct=cloud*(front?contact:1),transmission=cloud*contact;
            const V expected{0.8F*(0.9F*direct+0.3F*transmission),0.8F*(1.1F*direct+0.2F*transmission),0.8F*(1.3F*direct+0.1F*transmission),0.4F*direct};
            require(std::abs(values[i].x-expected.x)<1e-5F&&std::abs(values[i].y-expected.y)<1e-5F&&std::abs(values[i].z-expected.z)<1e-5F&&std::abs(values[i].w-expected.w)<1e-5F,"contact transmission, cloud composition, stereo facing or disabled neutrality changed");}
    }
    std::cout<<"Contact front/back response, once-only transmission, independent clouds and disabled neutrality passed\n";
}
}
int main(int count,char** args) {try{require(count>=2,"root required");run(std::filesystem::absolute(args[1]),count>2);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
