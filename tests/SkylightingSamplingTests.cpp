#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    struct V { float x{}, y{}, z{}, w{}; };
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    void check(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
    ComPtr<ID3DBlob> compile(const std::string& source, const std::filesystem::path& path)
    {
        ComPtr<ID3DBlob> code, errors;
        const auto result = D3DCompile(source.data(), source.size(), path.string().c_str(), nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
        if (FAILED(result) && errors) std::cerr << static_cast<char*>(errors->GetBufferPointer());
        check(result, "sampling shader compilation");
        return code;
    }
    void run(const std::filesystem::path& root, bool hardware)
    {
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
            nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "D3D11 device");
        const auto shaderDirectory = root / "package/Shaders/Community/Skylighting";
        const std::string ambient = R"(
#include "SkylightingAmbientTransform.hlsl"
RWBuffer<float4> Result : register(u0);
[numthreads(2,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    PixelInput p; p.Position=float4(id.x+0.5,0.5,0,1); p.Eye=id.x;
    PixelOutput value=PSMain(p);
    Result[id.x]=float4(value.DiffuseVisibility.x,value.SpecularVisibility.x,0,1);
})";
        const std::string reflection = R"(
#define SKYLIGHTING_PARAMETERS_SRV
#include "SkylightingSampling.hlsli"
cbuffer TestWorld : register(b0) { float4 WorldPosition; float4 WorldNormal; float4 Eye[2]; };
RWBuffer<float4> Result : register(u0);
[numthreads(2,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    float value=SkylightingReflectionVisibility(WorldPosition.xyz,WorldNormal.xyz,
        normalize(Eye[id.x].xyz-WorldPosition.xyz),0.5);
    Result[id.x]=float4(value,value,0,1);
})";
        const std::string sunlight = R"(
#include "SkylightingDirectionalTransform.hlsl"
cbuffer TestWorld : register(b0) { float4 WorldPosition; float4 WorldNormal; float4 Eye[2]; };
RWBuffer<float4> Result : register(u0);
[numthreads(2,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    PixelInput p; p.Position=float4(id.x+0.5,0.5,0,1); p.Eye=id.x;
    p.NormalCarrier=float4(dot(float3(Camera[20].x,Camera[21].x,Camera[22].x),WorldNormal.xyz),
        dot(float3(Camera[20].y,Camera[21].y,Camera[22].y),WorldNormal.xyz),
        dot(float3(Camera[20].z,Camera[21].z,Camera[22].z),WorldNormal.xyz),0);
    p.RimDiffuse=float4(0.2,0.3,0.4,0); p.TransmissionDiffuse=float4(0.3,0.2,0.1,0);
    p.PbrDiffuseScale=float4(0.5,0.6,0.7,1);p.NativeVisibility=0.8;p.ContactVisibility=0.6;
    Result[id.x]=PSMain(p);
})";
        std::array<ComPtr<ID3D11ComputeShader>,3> shaders;
        for (UINT index=0; index<3; ++index) {
            const auto code=compile(index==2 ? sunlight : (index ? reflection : ambient), shaderDirectory/"SamplingTest.hlsl");
            check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shaders[index]),"sampling shader");
        }
        std::array<ComPtr<ID3D11Buffer>,4> constants;
        const std::array<UINT,4> sizes{4*16,16,61*16,32*16};
        const std::array<UINT,4> slots{0,8,12,13};
        for (UINT index=0; index<4; ++index) {
            D3D11_BUFFER_DESC description{};description.ByteWidth=sizes[index];description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&description,nullptr,&constants[index]),"test constants");
            auto* buffer=constants[index].Get();context->CSSetConstantBuffers(slots[index],1,&buffer);
        }
        V stereo{1,0,0,0};context->UpdateSubresource(constants[1].Get(),0,nullptr,&stereo,0,0);
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=32*16;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Buffer> parameterBuffer;check(device->CreateBuffer(&bd,nullptr,&parameterBuffer),"parameter buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=32;
        ComPtr<ID3D11ShaderResourceView> parameterView;check(device->CreateShaderResourceView(parameterBuffer.Get(),&sv,&parameterView),"parameter SRV");
        auto* parameter=parameterView.Get();context->CSSetShaderResources(52,1,&parameter);
        bd.ByteWidth=2*16;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Buffer> output,staging;check(device->CreateBuffer(&bd,nullptr,&output),"output");
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=2;
        ComPtr<ID3D11UnorderedAccessView> outputView;check(device->CreateUnorderedAccessView(output.Get(),&uv,&outputView),"output UAV");
        bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateBuffer(&bd,nullptr,&staging),"readback");
        auto* destination=outputView.Get();context->CSSetUnorderedAccessViews(0,1,&destination,nullptr);
        D3D11_TEXTURE3D_DESC pd{};pd.Width=pd.Height=pd.Depth=4;pd.MipLevels=1;
        pd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;pd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<V,64> probes{};
        for (UINT z=0;z<4;++z)for(UINT y=0;y<4;++y)for(UINT x=0;x<4;++x)
            probes[(z*4+y)*4+x]={3.5449077018F*(0.15F+0.2F*x),0,0,0};
        D3D11_SUBRESOURCE_DATA initial{probes.data(),4*16,16*16};
        ComPtr<ID3D11Texture3D> volume;ComPtr<ID3D11ShaderResourceView> probeView;
        check(device->CreateTexture3D(&pd,&initial,&volume),"probe volume");check(device->CreateShaderResourceView(volume.Get(),nullptr,&probeView),"probe SRV");
        const std::array<ID3D11ShaderResourceView*,2> probeViews{probeView.Get(),probeView.Get()};context->CSSetShaderResources(50,2,probeViews.data());
        std::array<V,64> sunCells{};sunCells.fill({0,0,1,0.25F});
        D3D11_SUBRESOURCE_DATA sunInitial{sunCells.data(),4*16,16*16};
        ComPtr<ID3D11Texture3D> sunVolume;ComPtr<ID3D11ShaderResourceView> sunView;
        check(device->CreateTexture3D(&pd,&sunInitial,&sunVolume),"sun volume");
        check(device->CreateShaderResourceView(sunVolume.Get(),nullptr,&sunView),"sun SRV");
        const std::array<ID3D11ShaderResourceView*,2> sunViews{sunView.Get(),sunView.Get()};
        context->CSSetShaderResources(53,2,sunViews.data());
        std::array<ComPtr<ID3D11Texture2D>,3> inputs;
        for (UINT index=0;index<3;++index) {
            D3D11_TEXTURE2D_DESC td{};td.Width=2;td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;
            td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&td,nullptr,&inputs[index]),"native input");
            ComPtr<ID3D11ShaderResourceView> view;check(device->CreateShaderResourceView(inputs[index].Get(),nullptr,&view),"native input SRV");
            auto* input=view.Get();context->CSSetShaderResources(index+1,1,&input);
            const std::array<V,2> pixels{V{0.5F,0.5F,0.5F,0.5F},V{0.5F,0.5F,0.5F,0.5F}};
            context->UpdateSubresource(inputs[index].Get(),0,nullptr,pixels.data(),32,0);
        }
        std::array<V,32> settings{};
        for(UINT base:{5u,11u,20u,26u}) {
            settings[base]={2000,2000,2000,0};settings[base+1]={500,500,500,0};
            const auto four=std::bit_cast<float>(4u);settings[base+3]={four,four,four,0};
        }
        settings[18]={0.1F,0.1F,1,0};settings[19]={1000,2000,3000,0};
        const V target{1100,2030,3050,1},normalWorld{0.6F,0,-0.8F,0};
        std::array<V,2> baseline{};
        for (UINT scenario=0;scenario<5;++scenario) {
            const bool rotated=scenario==1;
            if(scenario==4) {sunCells.fill({0,0,0,-1});context->UpdateSubresource(sunVolume.Get(),0,nullptr,sunCells.data(),4*16,16*16); }
            const V origin{1000+float(scenario==2?100:0),2000,3000,0};
            settings[19]=origin;settings[7]={1000-origin.x,0,0,0};settings[13]=settings[7];settings[22]=settings[7];settings[28]=settings[7];
            settings[18].z=scenario==3?0.0F:1.0F;
            context->UpdateSubresource(constants[3].Get(),0,nullptr,settings.data(),0,0);
            context->UpdateSubresource(parameterBuffer.Get(),0,nullptr,settings.data(),0,0);
            std::array<V,61> camera{};
            camera[20]=rotated?V{0,-1,0,0}:V{1,0,0,0};
            camera[21]=rotated?V{1,0,0,0}:V{0,1,0,0};camera[22]={0,0,1,0};
            std::array<V,4> world{target,normalWorld,V{997,2000,3000,0},V{1003,2000,3000,0}};
            for(UINT eye=0;eye<2;++eye) {
                camera[59+eye]=world[2+eye];const V delta{target.x-world[2+eye].x,target.y-world[2+eye].y,target.z-world[2+eye].z,0};
                const V view=rotated?V{delta.y,-delta.x,delta.z,0}:delta;
                const UINT base=32+4*eye;camera[base]={0,0,0,view.x};camera[base+1]={0,0,0,view.y};camera[base+2]={0,0,0,view.z};camera[base+3]={0,0,0,1};
            }
            context->UpdateSubresource(constants[0].Get(),0,nullptr,world.data(),0,0);
            context->UpdateSubresource(constants[2].Get(),0,nullptr,camera.data(),0,0);
            const V n=rotated?V{0,-0.6F,-0.8F,0}:normalWorld;
            const auto divisor=std::sqrt(8*(1-n.z));
            const V packedNormal{0.5F+n.x/divisor,0.5F+n.y/divisor,0,0};
            const std::array<V,2> normals{packedNormal,packedNormal};context->UpdateSubresource(inputs[0].Get(),0,nullptr,normals.data(),32,0);
            std::array<V,2> ambientResult{};
            for(UINT shader=0;shader<3;++shader) {
                context->CSSetShader(shaders[shader].Get(),nullptr,0);context->Dispatch(1,1,1);context->CopyResource(staging.Get(),output.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"sampling readback");
                std::array<V,2> result{};std::memcpy(result.data(),mapped.pData,sizeof(result));context->Unmap(staging.Get(),0);
                for(UINT eye=0;eye<2;++eye) {
                    if(shader==2) {
                        const float delta=(scenario==3||scenario==4)?0.0F:(0.25F-0.8F)*0.6F*0.5F;
                        require(std::abs(result[eye].x-delta*0.5F)<1e-5F &&
                            std::abs(result[eye].y-delta*0.6F)<1e-5F &&
                            std::abs(result[eye].z-delta*0.7F)<1e-5F && result[eye].w==0,
                            "sun lobe delta, PBR scale, stereo reconstruction or missing-history neutrality changed");
                        continue;
                    }
                    require(std::isfinite(result[eye].x)&&result[eye].x>0&&result[eye].x<=1,"invalid visibility");
                    if (scenario==3) require(result[eye].x==1&&result[eye].y==1,"disabled visibility changed shading");
                    else if(shader==0) {
                        require(result[eye].x<0.9F,"test did not sample sheltered probes");
                        if(scenario==0) baseline[eye]=result[eye];
                        else require(std::abs(result[eye].x-baseline[eye].x)<1e-5F&&std::abs(result[eye].y-baseline[eye].y)<1e-5F,"head rotation or origin movement changed world visibility");
                    }
                    if(shader==1) require(std::abs(result[eye].y-ambientResult[eye].y)<1e-5F,"reflection and ambient probe consumers disagree");
                }
                if(shader==0) ambientResult=result;
            }
        }
        std::cout<<"Skylighting stereo world positions, rotated normals, moved origins, shared reflection visibility, isolated sun lobes, missing history and disabled neutrality passed\n";
    }
}
int main(int count,char** arguments)
{
    try { require(count>=2,"repository root required");run(std::filesystem::absolute(arguments[1]),count>2);return 0; }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
