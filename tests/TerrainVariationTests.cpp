#include "Features/terrain_variation/TerrainVariationRuntime.h"
#include "D3D11AllocationFailure.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace csah::render {void setDFPrePassTerrainVariationEnabled(bool) noexcept {}}
namespace {
using Microsoft::WRL::ComPtr;
struct V {float x,y,z,w;};
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
ComPtr<ID3DBlob> compile(const std::string& source,const std::filesystem::path& path,const char* entry,const char* target) {
 ComPtr<ID3DBlob> code,error;auto result=D3DCompile(source.data(),source.size(),path.string().c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
 if(FAILED(result)&&error) std::cerr<<static_cast<char*>(error->GetBufferPointer());check(result,"shader compilation");return code;
}
void run(const std::filesystem::path& root,bool hardware) {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 check(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"device");
 const auto path=root/"package/Shaders/Community/TerrainVariation/VariationTest.hlsl";
 const std::string source=R"(
#include "TerrainVariation.hlsli"
Texture2D<float4> Material : register(t0);
Texture2DArray<float4> Layers : register(t1);
Texture2D<float4> Distant : register(t2);
SamplerState Samp : register(s0);
struct Input {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Input VSMain(uint v:SV_VertexID) {Input o;o.uv=float2((v<<1)&2,v&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
struct Output {float4 nearColor:SV_Target0;float4 layerColor:SV_Target1;float4 distantColor:SV_Target2;float4 height:SV_Target3;};
Output PSMain(Input input) {
 float2 uv=float2(frac(input.uv.x*2),input.uv.y);
 float2 dx=ddx(uv),dy=ddy(uv);
 StochasticOffsets offsets=ComputeStochasticOffsets(uv/96+float2(0.13,0.27));
 Output o;o.nearColor=SampleVariedTerrain(Material,Samp,uv,dx,dy,offsets,true,false);
 o.layerColor=SampleVariedTerrain(Layers,Samp,float3(uv,1),dx,dy,offsets,true,false);
 o.distantColor=SampleVariedLod(Distant,Samp,uv);
 o.height=float4(SampleVariedTerrain(Material,Samp,uv,dx,dy,offsets,true,true).a,
  SampleVariedTerrain(Layers,Samp,float3(uv,1),dx,dy,offsets,true,true).a,0,1);
 return o;
})";
 const auto vsCode=compile(source,path,"VSMain","vs_5_0"),psCode=compile(source,path,"PSMain","ps_5_0");
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
 check(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs),"vertex shader");check(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps),"pixel shader");context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 std::array<ComPtr<ID3D11Texture2D>,3> textures;std::array<ComPtr<ID3D11ShaderResourceView>,3> views;
 for(UINT texture=0;texture<3;++texture) {
  const UINT size=texture==2?512:8,slices=texture==1?2:1;
  D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=size;td.ArraySize=slices;td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;check(device->CreateTexture2D(&td,nullptr,&textures[texture]),"input texture");check(device->CreateShaderResourceView(textures[texture].Get(),nullptr,&views[texture]),"input SRV");
  std::vector<V> pixels(size*size);
  for(UINT slice=0;slice<slices;++slice) {for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x) pixels[y*size+x]=texture==2?V{float(x%7)/6,float(y%11)/10,float((x+y)%2),float(x%5)/4}:V{float(x)/7,float(y)/7,slice?0.9F:float((x+y)%2),slice?0.75F:0.25F};context->UpdateSubresource(textures[texture].Get(),slice,nullptr,pixels.data(),size*sizeof(V),0);}
  auto* view=views[texture].Get();context->PSSetShaderResources(texture,1,&view);
 }
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler),"sampler");auto* sample=sampler.Get();context->PSSetSamplers(0,1,&sample);
 D3D11_VIEWPORT viewport{0,0,32,16,0,1};context->RSSetViewports(1,&viewport);D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
 std::array<ComPtr<ID3D11Texture2D>,4> outputs,readbacks;std::array<ComPtr<ID3D11RenderTargetView>,4> targets;std::array<ID3D11RenderTargetView*,4> rtvs{};
 for(UINT i=0;i<4;++i) {D3D11_TEXTURE2D_DESC td{};td.Width=32;td.Height=16;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&td,nullptr,&outputs[i]),"output");check(device->CreateRenderTargetView(outputs[i].Get(),nullptr,&targets[i]),"RTV");rtvs[i]=targets[i].Get();td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&readbacks[i]),"readback");}
 context->OMSetRenderTargets(4,rtvs.data(),nullptr);
 auto& runtime=csah::terrain_variation::Runtime::get();runtime.applySettings({false,true});runtime.onDeviceCreated(device.Get(),context.Get());
 std::array<std::array<std::vector<V>,4>,3> results;
 for(UINT scenario=0;scenario<3;++scenario) {
  runtime.applySettings({scenario!=0,scenario==2});
  {auto bindings=runtime.scopeDraw(context.Get(),true);context->Draw(3,0);}
  ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(56,1,&restored);require(!restored,"terrain parameters leaked beyond the material draw");
  for(UINT i=0;i<4;++i) {context->CopyResource(readbacks[i].Get(),outputs[i].Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readbacks[i].Get(),0,D3D11_MAP_READ,0,&m),"map");auto& values=results[scenario][i];values.resize(32*16);for(UINT y=0;y<16;++y) std::memcpy(values.data()+y*32,static_cast<const char*>(m.pData)+y*m.RowPitch,32*sizeof(V));context->Unmap(readbacks[i].Get(),0);}
 }
 bool varied=false,lodVaried=false;
 for(UINT scenario=0;scenario<3;++scenario)for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x) {
  const UINT at=y*32+x;
  for(UINT target=0;target<4;++target) {const auto a=results[scenario][target][at],b=results[scenario][target][at+16];require(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&std::isfinite(a.w)&&std::abs(a.x-b.x)<1e-5F&&std::abs(a.y-b.y)<1e-5F&&std::abs(a.z-b.z)<1e-5F&&std::abs(a.w-b.w)<1e-5F,"eye-dependent or nonfinite stochastic sample");}
  const auto nearValue=results[scenario][0][at],layer=results[scenario][1][at],lod=results[scenario][2][at],height=results[scenario][3][at];
  require(std::abs(nearValue.w-0.25F)<1e-5F&&std::abs(layer.w-0.75F)<1e-5F&&std::abs(layer.z-0.9F)<1e-5F&&std::abs(height.x-0.25F)<1e-5F&&std::abs(height.y-0.75F)<1e-5F,"array slice or parallax height alignment changed");
  require(lod.w==results[0][2][at].w,"LOD variation changed alpha-test coverage");
  if(scenario==0) require(std::abs(nearValue.x-float(x/2)/7)<1e-5F&&std::abs(nearValue.y-float(y/2)/7)<1e-5F,"disabled sampler diverged from native point sampling");
  else {varied|=std::abs(nearValue.x-results[0][0][at].x)>0.01F;lodVaried|=scenario==2&&std::abs(lod.x-results[0][2][at].x)>0.01F;}
  if(scenario==1) require(lod.x==results[0][2][at].x&&lod.y==results[0][2][at].y,"LOD opt-out ignored");
 }
 require(varied&&lodVaried,"enabled variation never changed terrain samples");runtime.onDeviceCreated(nullptr,nullptr);context->ClearState();std::cout<<"Terrain variation: stereo stability, arrays, parallax height, LOD alpha, disabled parity and live controls passed\n";
}
}
int main(int argc,char** argv) {try{require(argc>=2,"root required");run(std::filesystem::absolute(argv[1]),argc>2);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
