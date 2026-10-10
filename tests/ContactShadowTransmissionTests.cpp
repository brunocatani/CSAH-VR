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
    ContactInput c;c.Position=float4(id.x+0.5,0.5,0,1);
    float4 visibility=ContactMain(c);
    PixelInput p;p.Position=c.Position;p.Eye=id.x/2;p.NormalCarrier=float4(0,0,1,0);
    p.RimDiffuse=float4(0.2,0.3,0.4,0);p.TransmissionDiffuse=float4(0.3,0.2,0.1,0);
    p.PbrDiffuseScale=1;p.NativeVisibility=0.8;p.ContactVisibility=visibility;
    float3 nativeDiffuse=(float3(0.7,0.8,0.9)+p.RimDiffuse.xyz+p.TransmissionDiffuse.xyz)*0.8*visibility.x;
    Result[id.x]=float4(nativeDiffuse+PSMain(p).diffuseDelta.xyz,0.5*0.8*visibility.x);
})";
    const auto path=root/"package/Shaders/Community/Skylighting/TransmissionTest.hlsl";
    ComPtr<ID3DBlob> code,errors;const auto status=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
    if(FAILED(status)&&errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());check(status,"shader compile");
    ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"shader");context->CSSetShader(shader.Get(),nullptr,0);
    ComPtr<ID3D11Buffer> light,settings,output,staging;D3D11_BUFFER_DESC bd{};bd.ByteWidth=46*16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&light),"light constants");bd.ByteWidth=5*16;check(device->CreateBuffer(&bd,nullptr,&settings),"contact settings");
    std::array<V,46> sun{};sun[1]={0,0,1,0};sun[2]={0,0,-1,0};context->UpdateSubresource(light.Get(),0,nullptr,sun.data(),0,0);auto* cb=light.Get();context->CSSetConstantBuffers(2,1,&cb);cb=settings.Get();context->CSSetConstantBuffers(13,1,&cb);
    bd.ByteWidth=4*16;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;check(device->CreateBuffer(&bd,nullptr,&output),"output");D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=4;ComPtr<ID3D11UnorderedAccessView> uav;check(device->CreateUnorderedAccessView(output.Get(),&uv,&uav),"output view");auto* out=uav.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
    bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateBuffer(&bd,nullptr,&staging),"staging");
    D3D11_TEXTURE2D_DESC td{};td.Width=4;td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ID3D11ShaderResourceView* srv{};
    const std::string resolveSource="#define CSMain main\n#include \"ContactShadowResolveCS.hlsl\"\n";
    const auto resolvePath=root/"package/Shaders/Community/ContactShadows/ResolveTest.hlsl";
    ComPtr<ID3DBlob> resolveCode,resolveErrors;
    const auto resolveStatus=D3DCompile(resolveSource.data(),resolveSource.size(),resolvePath.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&resolveCode,&resolveErrors);
    if(FAILED(resolveStatus)&&resolveErrors) std::cerr<<static_cast<char*>(resolveErrors->GetBufferPointer());check(resolveStatus,"resolve compile");
    ComPtr<ID3D11ComputeShader> resolve;check(device->CreateComputeShader(resolveCode->GetBufferPointer(),resolveCode->GetBufferSize(),nullptr,&resolve),"resolve shader");
    ComPtr<ID3D11Texture2D> raw,depth,normal,cloud,resolved;
    ComPtr<ID3D11ShaderResourceView> rawView,depthView,normalView,cloudView,resolvedView;
    check(device->CreateTexture2D(&td,nullptr,&raw),"raw pair");check(device->CreateShaderResourceView(raw.Get(),nullptr,&rawView),"raw SRV");
    td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;check(device->CreateTexture2D(&td,nullptr,&resolved),"resolved pair");check(device->CreateShaderResourceView(resolved.Get(),nullptr,&resolvedView),"resolved SRV");ComPtr<ID3D11UnorderedAccessView> resolvedOutput;check(device->CreateUnorderedAccessView(resolved.Get(),nullptr,&resolvedOutput),"resolved UAV");
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.Format=DXGI_FORMAT_R32_FLOAT;check(device->CreateTexture2D(&td,nullptr,&depth),"depth");check(device->CreateShaderResourceView(depth.Get(),nullptr,&depthView),"depth SRV");const float depths[4]{0.5F,0.5F,0.5F,0.5F};context->UpdateSubresource(depth.Get(),0,nullptr,depths,16,0);
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;check(device->CreateTexture2D(&td,nullptr,&normal),"normal");check(device->CreateShaderResourceView(normal.Get(),nullptr,&normalView),"normal SRV");const V normals[4]{{1,0.5F,0,0},{0.5F,0.5F,0,0},{1,0.5F,0,0},{0.5F,0.5F,0,0}};context->UpdateSubresource(normal.Get(),0,nullptr,normals,64,0);
    td.Width=td.Height=1;td.ArraySize=6;td.Format=DXGI_FORMAT_R32_FLOAT;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;check(device->CreateTexture2D(&td,nullptr,&cloud),"cloud cube");check(device->CreateShaderResourceView(cloud.Get(),nullptr,&cloudView),"cloud SRV");const float cloudOcclusion=0.5F;for(UINT face=0;face<6;++face) context->UpdateSubresource(cloud.Get(),face,nullptr,&cloudOcclusion,4,0);
    D3D11_SAMPLER_DESC sample{};sample.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sample.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sample,&sampler),"sampler");auto* sampleState=sampler.Get();context->CSSetSamplers(0,1,&sampleState);
    ComPtr<ID3D11Buffer> stereo,camera;bd={};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&stereo),"stereo");bd.ByteWidth=61*16;check(device->CreateBuffer(&bd,nullptr,&camera),"camera");const V stereoData{1,0,0,0};context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);std::array<V,61> cameraData{};cameraData[35].w=cameraData[39].w=1;cameraData[20].x=cameraData[21].y=cameraData[22].z=1;context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);cb=stereo.Get();context->CSSetConstantBuffers(8,1,&cb);cb=camera.Get();context->CSSetConstantBuffers(12,1,&cb);sun[45]={1,1,0,0};context->UpdateSubresource(light.Get(),0,nullptr,sun.data(),0,0);
    ComPtr<ID3D11Buffer> field;ComPtr<ID3D11ShaderResourceView> fieldView;
    bd={};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const std::array<V,3> fieldData{V{0,0,0,1},V{},V{}};D3D11_SUBRESOURCE_DATA initial{fieldData.data(),0,0};
    check(device->CreateBuffer(&bd,&initial,&field),"field");D3D11_SHADER_RESOURCE_VIEW_DESC fd{};fd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;fd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;fd.Buffer.NumElements=3;
    check(device->CreateShaderResourceView(field.Get(),&fd,&fieldView),"field view");
    const std::array<ID3D11ShaderResourceView*,5> inputs{depthView.Get(),rawView.Get(),cloudView.Get(),normalView.Get(),fieldView.Get()};context->CSSetShaderResources(0,5,inputs.data());
    for(unsigned scenario=0;scenario<4;++scenario) {
        const unsigned char frontByte=64,backByte=192;
        std::array<unsigned char,8> pixels{};for(unsigned i=0;i<4;++i) {pixels[i*2]=frontByte;pixels[i*2+1]=backByte;}context->UpdateSubresource(raw.Get(),0,nullptr,pixels.data(),8,0);
        std::array<V,5> params{};params[4]={0,0,4,1};params[0].x=1;params[2]={2048,scenario==3?0.0F:1.0F,scenario==2?0.0F:1.0F,0};params[3]={scenario==1?0.0F:1.0F,1,10000,100000};context->UpdateSubresource(settings.Get(),0,nullptr,params.data(),0,0);
        srv=nullptr;context->CSSetShaderResources(46,1,&srv);out=resolvedOutput.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);context->CSSetShader(resolve.Get(),nullptr,0);context->Dispatch(1,1,1);
        out=uav.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);srv=resolvedView.Get();context->CSSetShaderResources(46,1,&srv);context->CSSetShader(shader.Get(),nullptr,0);
        context->Dispatch(1,1,1);context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"readback");std::array<V,4> values{};std::memcpy(values.data(),m.pData,sizeof(values));context->Unmap(staging.Get(),0);
        const float frontContact=scenario>=2?1:frontByte/255.0F,backContact=scenario>=2?1:backByte/255.0F,cloudVisibility=scenario==1||scenario==3?1:0.5F;
        const auto quantize=[](float v) {return std::round(v*255)/255;};
        for(unsigned i=0;i<4;++i) {const bool front=i==0||i==3;const float direct=quantize((i<2?cloudVisibility:1)*(front?frontContact:1)),transmission=quantize((i<2?cloudVisibility:1)*(front?frontContact:backContact));
            const V expected{0.8F*(0.9F*direct+0.3F*transmission),0.8F*(1.1F*direct+0.2F*transmission),0.8F*(1.3F*direct+0.1F*transmission),0.4F*direct};
            // Typed UNORM UAV conversion may round an exact halfway value
            // differently on WARP and hardware. Bound error by one stored LSB
            // propagated through the known linear lighting coefficients.
            constexpr float tolerance=1.2F/255.0F+1.0e-6F;
            require(std::abs(values[i].x-expected.x)<tolerance&&std::abs(values[i].y-expected.y)<tolerance&&std::abs(values[i].z-expected.z)<tolerance&&std::abs(values[i].w-expected.w)<tolerance,"contact transmission, cloud composition, stereo facing or disabled neutrality changed");}
    }
    // Keep a four-pixel allocation but render one pixel per eye at x=1.
    // The right eye uses the opposite sunlight direction; full-size splitting
    // would incorrectly treat pixel 1 as the second left-eye receiver.
    {
        std::array<V,5> params{};params[0].x=1;params[2]={2048,1,1,0};params[4]={1,0,2,1};
        context->UpdateSubresource(settings.Get(),0,nullptr,params.data(),0,0);
        srv=nullptr;context->CSSetShaderResources(46,1,&srv);
        const float clear[4]{.1f,.1f,.1f,.1f};context->ClearUnorderedAccessViewFloat(resolvedOutput.Get(),clear);
        out=resolvedOutput.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);context->CSSetShader(resolve.Get(),nullptr,0);context->Dispatch(1,1,1);
        out=nullptr;context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
        D3D11_TEXTURE2D_DESC desc{};resolved->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;check(device->CreateTexture2D(&desc,nullptr,&readback),"scaled resolve staging");context->CopyResource(readback.Get(),resolved.Get());
        D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"scaled resolve read");
        const auto* bytes=static_cast<const unsigned char*>(m.pData);
        require(bytes[0]>=25 && bytes[0]<=26 && bytes[6]==bytes[0],"contact resolve wrote outside the active viewport");
        require(bytes[2]==255 && bytes[3]==192 && bytes[4]==255 && bytes[5]==192,"contact resolve used allocation midpoint instead of the active eye boundary");
        context->Unmap(readback.Get(),0);
    }
    const std::string windowSource=R"(
#include "ContactShadowMaskCS.hlsl"
RWBuffer<float4> Result : register(u1);
[numthreads(4,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    float sign=id.x<2?1:-1;
    float sample=id.x%2==0?11:13;
    float2 distance=ContactWindowDistance(12,sample,sign);
    Result[id.x]=float4(saturate(distance*4-3),0,1);
})";
    code.Reset();errors.Reset();check(D3DCompile(windowSource.data(),windowSource.size(),resolvePath.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors),"window compile");
    ComPtr<ID3D11ComputeShader> window;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&window),"window shader");
    out=nullptr;context->CSSetUnorderedAccessViews(0,1,&out,nullptr);out=uav.Get();context->CSSetUnorderedAccessViews(1,1,&out,nullptr);context->CSSetShader(window.Get(),nullptr,0);context->Dispatch(1,1,1);context->CopyResource(staging.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE read{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&read),"window readback");std::array<V,4> windows{};std::memcpy(windows.data(),read.pData,sizeof(windows));context->Unmap(staging.Get(),0);
    require(windows[0].x==0&&windows[0].y==1&&windows[1].x==1&&windows[1].y==0&&windows[2].x==1&&windows[2].y==0&&windows[3].x==0&&windows[3].y==1,"paired receiver windows must distinguish opposite surfaces for either depth sign");
    std::cout<<"Paired contact resolve, stereo facing, once-only transmission, cloud composition and disabled neutrality passed\n";
}
}
int main(int count,char** args) {try{require(count>=2,"root required");run(std::filesystem::absolute(args[1]),count>2);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
