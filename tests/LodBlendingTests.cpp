#include "Features/lod_blending/LodBlendingRuntime.h"
#include "Features/lod_blending/LodClassification.h"
#include "Features/lod_blending/LodBlendingSettingsStore.h"
#include "D3D11AllocationFailure.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace csah::render { void setDFPrePassLodBlendingEnabled(bool) noexcept {} }
namespace {
using Microsoft::WRL::ComPtr;
struct V {float x,y,z,w;};
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
HRESULT STDMETHODCALLTYPE failBuffer(ID3D11Device*,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer** output) {if(output) *output=nullptr;return E_OUTOFMEMORY;}
void run(const std::filesystem::path& root,bool hardware) {
    using namespace csah::lod_blending;
    struct Node {Node* parent;};
    Node rootNode{nullptr},branch{&rootNode},leaf{&branch},ordinary{nullptr};
    const auto parent=[](const void* node) noexcept {return static_cast<const Node*>(node)->parent;};
    require(beneathRoot(&leaf,&rootNode,parent),"descendant classification");
    require(!beneathRoot(&ordinary,&rootNode,parent)&&!beneathRoot(&leaf,nullptr,parent),"unrelated or missing LOD root");
    branch.parent=&leaf;require(!beneathRoot(&leaf,&rootNode,parent),"cycle must fail closed");
    const auto bad=sanitize({true,-1,std::numeric_limits<float>::quiet_NaN(),100,0});
    require(bad.terrainBrightness==0.01F&&bad.terrainGamma==1&&bad.objectBrightness==5&&bad.objectGamma==0.1F,"settings bounds");
    require(!requested(Settings{}),"neutral controls must not select replacement shaders");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
    auto& runtime=Runtime::get();runtime.applySettings({});runtime.onDeviceCreated(device.Get(),context.Get());
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Buffer> parameters;check(device->CreateBuffer(&bd,nullptr,&parameters),"parameters");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=2;
    ComPtr<ID3D11ShaderResourceView> view;check(device->CreateShaderResourceView(parameters.Get(),&sd,&view),"parameter view");auto* srv=view.Get();context->PSSetShaderResources(55,1,&srv);
    {auto scope=runtime.scopeDraw(context.Get(),true);ComPtr<ID3D11ShaderResourceView> current;context->PSGetShaderResources(55,1,&current);require(!current,"neutral draw exposed adjustment parameters");}
    ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(55,1,&restored);require(restored.Get()==view.Get(),"parameter slot restoration");
    runtime.applySettings({true,2,1,1,1});runtime.beginGeometry(nullptr);
    {auto scope=runtime.scopeDraw(context.Get(),true);ComPtr<ID3D11ShaderResourceView> current;context->PSGetShaderResources(55,1,&current);require(current&&current.Get()!=view.Get(),"active draw has no owned parameters");}runtime.endGeometry();
    {DeviceFailureScope failure(device.Get(),3,reinterpret_cast<void*>(&failBuffer));runtime.onDeviceCreated(device.Get(),context.Get());}
    {auto scope=runtime.scopeDraw(context.Get(),true);ComPtr<ID3D11ShaderResourceView> current;context->PSGetShaderResources(55,1,&current);require(current.Get()!=nullptr,"allocation failure did not recover on a later draw");}
    const std::string source=R"(
#include "LODBlending.hlsli"
RWBuffer<float4> Result : register(u0);
[numthreads(2,1,1)] void main(uint3 id:SV_DispatchThreadID) {Result[id.x]=AdjustLodAlbedo(float4(0.25,0.5,0.75,0.37),id.x==0);}
)";
    ComPtr<ID3DBlob> code,errors;const auto path=root/"package/Shaders/Community/LODBlending/LodTest.hlsl";
    const auto hr=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
    if(FAILED(hr)&&errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());check(hr,"LOD shader compilation");
    ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"LOD shader");context->CSSetShader(shader.Get(),nullptr,0);context->CSSetShaderResources(55,1,&srv);
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;ComPtr<ID3D11Buffer> output,staging;check(device->CreateBuffer(&bd,nullptr,&output),"output");D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=2;ComPtr<ID3D11UnorderedAccessView> uav;check(device->CreateUnorderedAccessView(output.Get(),&ud,&uav),"output view");auto* dest=uav.Get();context->CSSetUnorderedAccessViews(0,1,&dest,nullptr);
    bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateBuffer(&bd,nullptr,&staging),"readback");
    for(unsigned scenario=0;scenario<4;++scenario) {
        std::array<V,2> data{V{2,2,0.5F,0.5F},V{scenario==0?0.0F:1.0F,scenario>=2?1.0F:0.0F,0,0}};
        if(scenario==3) data[0]={1,1,1,1};context->UpdateSubresource(parameters.Get(),0,nullptr,data.data(),0,0);context->Dispatch(1,1,1);context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"map");std::array<V,2> result{};std::memcpy(result.data(),map.pData,sizeof(result));context->Unmap(staging.Get(),0);
        for(unsigned i=0;i<2;++i) {float brightness=1,gamma=1;if(scenario>0&&scenario<3&&(i==0||scenario==2)) {brightness=i==0?2:0.5F;gamma=i==0?2:0.5F;}require(std::abs(result[i].x-std::pow(0.25F,gamma)*brightness)<1e-5F&&std::abs(result[i].y-std::pow(0.5F,gamma)*brightness)<1e-5F&&std::abs(result[i].z-std::pow(0.75F,gamma)*brightness)<1e-5F&&result[i].w==0.37F,"LOD response, ordinary-object exclusion, alpha preservation or neutrality");}
    }
    runtime.onDeviceCreated(nullptr,nullptr);context->ClearState();std::cout<<"LOD response, classification gate, neutral defaults, slot restoration and allocation recovery passed\n";
}
}
int main(int argc,char** argv) {try{require(argc>=2,"root required");run(std::filesystem::absolute(argv[1]),argc>2);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
