#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;
struct V { float x{},y{},z{},w{}; };
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void check(HRESULT value,const char* message) { require(SUCCEEDED(value),message); }
void run(const std::filesystem::path& root,bool hardware) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
    const std::string source="#define CSMain main\n#include \"UpdateSunProbesCS.hlsl\"\n";
    ComPtr<ID3DBlob> code,errors;
    const auto path=root/"package/Shaders/Community/Skylighting/SunTest.hlsl";
    const auto result=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
    if(FAILED(result)&&errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());check(result,"sun shader compilation");
    ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"sun shader");context->CSSetShader(shader.Get(),nullptr,0);
    std::array<ComPtr<ID3D11Buffer>,3> buffers;
    const std::array<UINT,3> sizes{46*16,61*16,32*16}, slots{0,12,13};
    for(UINT i=0;i<3;++i) {D3D11_BUFFER_DESC d{};d.ByteWidth=sizes[i];d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&d,nullptr,&buffers[i]),"constants");auto* b=buffers[i].Get();context->CSSetConstantBuffers(slots[i],1,&b);}
    std::array<V,46> sun{};sun[1]={0,0,1,0};sun[11]={0.25F,0,0,0};sun[12]={0.75F,1,0,0};
    for(UINT i=13;i<=18;++i) sun[i]={0,0,0,0.5F};sun[38]=sun[39]={0,0,0,100};sun[42].x=1e20F;
    std::array<V,61> camera{};camera[0]=camera[20]={1,0,0,0};camera[1]=camera[21]={0,1,0,0};camera[2]=camera[22]={0,0,1,0};camera[6]={0,0,0,0.5F};camera[7]={0,0,0,1};
    std::array<V,32> settings{};const auto four=std::bit_cast<float>(4u);
    for(UINT base:{20u,26u}) {settings[base]={4,4,4,0};settings[base+1]={1,1,1,0};settings[base+3]={four,four,four,0};}
    settings[18].z=1;
    D3D11_TEXTURE3D_DESC td{};td.Width=td.Height=td.Depth=4;td.MipLevels=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture3D> history,readback;check(device->CreateTexture3D(&td,nullptr,&history),"history");ComPtr<ID3D11UnorderedAccessView> output;check(device->CreateUnorderedAccessView(history.Get(),nullptr,&output),"history UAV");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture3D(&td,nullptr,&readback),"readback");auto* uav=output.Get();context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    D3D11_TEXTURE2D_DESC sd{};sd.Width=sd.Height=4;sd.ArraySize=2;sd.MipLevels=sd.SampleDesc.Count=1;sd.Format=DXGI_FORMAT_R32_FLOAT;sd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> depth;check(device->CreateTexture2D(&sd,nullptr,&depth),"shadow depth");ComPtr<ID3D11ShaderResourceView> depthView;check(device->CreateShaderResourceView(depth.Get(),nullptr,&depthView),"shadow view");auto* srv=depthView.Get();context->CSSetShaderResources(0,1,&srv);
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;sampler.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> state;check(device->CreateSamplerState(&sampler,&state),"comparison sampler");auto* comparison=state.Get();context->CSSetSamplers(0,1,&comparison);
    auto setDepth=[&](float a,float b) {std::array<float,16> values{};values.fill(a);context->UpdateSubresource(depth.Get(),0,nullptr,values.data(),16,0);values.fill(b);context->UpdateSubresource(depth.Get(),1,nullptr,values.data(),16,0);};
    auto clear=[&] {const float unobserved[4]{0,0,0,-1};context->ClearUnorderedAccessViewFloat(output.Get(),unobserved);};
    auto dispatch=[&](float expected) {context->UpdateSubresource(buffers[0].Get(),0,nullptr,sun.data(),0,0);context->UpdateSubresource(buffers[1].Get(),0,nullptr,camera.data(),0,0);context->UpdateSubresource(buffers[2].Get(),0,nullptr,settings.data(),0,0);context->Dispatch(1,1,1);context->CopyResource(readback.Get(),history.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"history map");bool valid=true;for(UINT z=0;z<4;++z)for(UINT y=0;y<4;++y) {const auto* row=reinterpret_cast<const V*>(static_cast<const char*>(m.pData)+z*m.DepthPitch+y*m.RowPitch);for(UINT x=0;x<4;++x) valid=valid&&std::isfinite(row[x].w)&&std::abs(row[x].w-expected)<1e-5F;}context->Unmap(readback.Get(),0);require(valid,"sun history visibility mismatch");};
    // A current sample initializes each cell; a stable cell retains exactly 31/32 history.
    settings[17].w=std::bit_cast<float>(1u);setDepth(1,0);clear();dispatch(1);setDepth(0,1);dispatch(31.0F/32.0F);
    sun[1]={1,0,0,0};dispatch(0); // direction change must discard stale sunlight
    settings[25]={std::bit_cast<float>(4),std::bit_cast<float>(4),std::bit_cast<float>(4),0};setDepth(1,0);dispatch(1); // replaced cells
    settings[25]={};sun[11].y=1;clear();dispatch(0); // single layout must honor native slice
    settings[17].w=std::bit_cast<float>(2u);sun[11].y=0.75F;setDepth(1,0);clear();dispatch(0.5F); // split cascade blend
    sun[13].w=2;clear();dispatch(-1); // out of projected bounds is unavailable, never false shadow
    sun[13].w=0.5F;settings[18].z=0;dispatch(-1);
    context->ClearState();std::cout<<"Sun history: single/split projections, slice selection, temporal retention, direction/cell reset and unavailable observations passed\n";
}
}
int main(int count,char** args) {try {require(count>=2,"root required");run(std::filesystem::absolute(args[1]),count>2);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
