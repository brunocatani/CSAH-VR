#include "Features/water_effects/WaterEffectsRuntime.h"
#include "Features/ibl/IblRuntime.h"
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
    struct V {float x{},y{},z{},w{};};
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
    std::vector<char> bytes(const std::filesystem::path& path) {std::ifstream stream(path,std::ios::binary);return {(std::istreambuf_iterator<char>(stream)),{}};}
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        UINT accepted=0;
        for(const auto& entry:std::filesystem::directory_iterator(root/"package/Shaders/Community/WaterLinearLighting")) if(entry.path().extension()==".dxbc") {
            auto code=bytes(entry.path());ComPtr<ID3D11PixelShader> ps;check(device->CreatePixelShader(code.data(),code.size(),nullptr,&ps),"water shader validation");++accepted;
        }
        require(accepted==31,"water shader family incomplete");
        auto original=bytes(root/"package/Shaders/Community/VerifiedWaterLinearLighting/WaterColor_0000001C.dxbc");
        auto candidate=bytes(root/"package/Shaders/Community/WaterLinearLighting/WaterColor_0000001C.dxbc");
        ComPtr<ID3D11PixelShader> native,patched;
        check(device->CreatePixelShader(original.data(),original.size(),nullptr,&native),"native water");check(device->CreatePixelShader(candidate.data(),candidate.size(),nullptr,&patched),"patched water");
        const char* vsSource=R"(
struct Out{float4 p:SV_POSITION;float4 eye:TEXCOORD0;float4 object:TEXCOORD4;float4 uv01:TEXCOORD1;float4 uv2:TEXCOORD2;float3 view:TEXCOORD5;nointerpolation uint eyeIndex:EYEPOS;};
Out main(uint vertex:SV_VertexID,uint eye:SV_InstanceID){Out o;float2 uv=float2((vertex<<1)&2,vertex&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.p.x=o.p.x*.5+(eye==0?-.5:.5);o.eye=float4(30,20,-100,1000);o.object=float4(uv,0,1);o.uv01=uv.xyxy;o.uv2=float4(uv,1,0);o.view=float3(30,20,100);o.eyeIndex=eye;return o;})";
        ComPtr<ID3DBlob> code,error;check(D3DCompile(vsSource,std::strlen(vsSource),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error),"fixture VS compile");
        ComPtr<ID3D11VertexShader> vs;check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs),"fixture VS");context->VSSetShader(vs.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto buffer=[&](const void* data,UINT size) {D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;check(device->CreateBuffer(&d,&initial,&result),"constants");return result;};
        std::array<V,77> frame{};frame[0]={1,0,0,0};frame[1]={0,1,0,0};frame[2]={0,0,1,0};frame[3]={0,0,0,1};frame[20]=frame[0];frame[21]=frame[1];frame[22]=frame[2];
        for(UINT row:std::array<UINT,4>{32,36,40,44}) {frame[row]={1,0,0,0};frame[row+1]={0,1,0,0};frame[row+2]={0,0,0,1};frame[row+3]={0,0,.01f,0};}
        frame[71]={.0001f,0,0,0};frame[72]={.05f,.06f,.07f,1};frame[73]={.05f,.06f,.07f,1};frame[74]=frame[72];frame[75]=frame[73];
        std::array<V,3> geometry{};geometry[0]={1.0f/32,1.0f/16,1,1};geometry[1]={.2f,.1f,1,1};geometry[2]={1,1,1,1};
        std::array<V,14> material{V{.1f,.15f,.2f,.7f},V{.01f,.03f,.05f,1},V{.2f,.3f,.4f,.5f},V{.1f,.2f,.3f,0},V{.3f,.4f,.5f,0},V{.6f,.7f,.8f,0},V{1,1,1,.2f},V{0,0,0,0},V{1,1,1,1},V{.6f,.2f,.2f,0},V{20000,0,1,1000},V{0,.7f,1,0},V{1,0,0,1},V{0,0,.02f,0}};
        std::array<V,21> lights{};lights[8]=lights[9]={0,0,1,-100};
        std::array<V,5> linear{};std::array<ComPtr<ID3D11Buffer>,5> psBuffers{buffer(geometry.data(),sizeof(geometry)),buffer(material.data(),sizeof(material)),buffer(lights.data(),sizeof(lights)),buffer(linear.data(),sizeof(linear)),buffer(frame.data(),sizeof(frame))};
        const std::array<UINT,5> psSlots{0,1,2,5,12};for(UINT i=0;i<5;i++) {auto* cb=psBuffers[i].Get();context->PSSetConstantBuffers(psSlots[i],1,&cb);}
        std::array<V,3> vertexMaterial{};vertexMaterial[2]={256,256,256,0};std::array<V,15> vertexGeometry{};
        auto vsMaterial=buffer(vertexMaterial.data(),sizeof(vertexMaterial)),vsGeometry=buffer(vertexGeometry.data(),sizeof(vertexGeometry));
        auto* cb=vsMaterial.Get();context->VSSetConstantBuffers(1,1,&cb);cb=vsGeometry.Get();context->VSSetConstantBuffers(2,1,&cb);
        std::array<ComPtr<ID3D11Texture2D>,6> textures;std::array<ComPtr<ID3D11ShaderResourceView>,6> views;
        const std::array<UINT,6> slots{1,4,5,6,7,11};std::array<std::vector<V>,3> normals;
        for(UINT i=0;i<6;i++) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=32;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            check(device->CreateTexture2D(&d,nullptr,&textures[i]),"input");check(device->CreateShaderResourceView(textures[i].Get(),nullptr,&views[i]),"input SRV");
            std::vector<V> pixels(32*32,i==0?V{.2f,.3f,.4f,1}:i==4?V{.5f,.5f,.5f,1}:V{});
            if(i>=1 && i<=3) {for(UINT y=0;y<32;y++) for(UINT x=0;x<32;x++) pixels[y*32+x]={.5f+.3f*std::sin(x*.4f),.5f+.3f*std::cos(y*.4f),0,.5f};normals[i-1]=pixels;}
            context->UpdateSubresource(textures[i].Get(),0,nullptr,pixels.data(),32*sizeof(V),0);auto* view=views[i].Get();context->PSSetShaderResources(slots[i],1,&view);
        }
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler),"sampler");for(UINT slot:slots) {auto* s=sampler.Get();context->PSSetSamplers(slot,1,&s);}
        D3D11_TEXTURE2D_DESC td{};td.Width=32;td.Height=16;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateTexture2D(&td,nullptr,&target),"target");check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging),"staging");
        D3D11_VIEWPORT viewport{0,0,32,16,0,1};context->RSSetViewports(1,&viewport);D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
        auto& runtime=csah::water_effects::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());csah::water_effects::Settings settings;runtime.applySettings(settings);
        csah::ibl::SslrEnvironmentView environment;
        const auto render=[&](ID3D11PixelShader* shader) {
            context->PSSetShader(shader,nullptr,0);auto* output=rtv.Get();context->OMSetRenderTargets(1,&output,nullptr);
            {auto bindings=runtime.scopeDraw(context.Get(),true);auto reflections=runtime.scopeReflections(context.Get(),true,environment);context->DrawInstanced(3,2,0,0);}
            std::array<ComPtr<ID3D11ShaderResourceView>,3> restored;for(UINT i=0;i<3;i++) {context->PSGetShaderResources(66+i,1,&restored[i]);require(!restored[i],"water bindings escaped draw scope");}
            ComPtr<ID3D11ShaderResourceView> reflected;context->PSGetShaderResources(75,1,&reflected);require(!reflected,"water reflection parameters escaped draw scope");
            context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"readback");std::vector<V> result(32*16);for(UINT y=0;y<16;y++) std::memcpy(result.data()+y*32,static_cast<const std::byte*>(map.pData)+y*map.RowPitch,32*sizeof(V));context->Unmap(staging.Get(),0);return result;
        };
        const auto compare=[](const std::vector<V>& a,const std::vector<V>& b,const char* message) {
            for(UINT i=0;i<a.size();i++) require(std::isfinite(a[i].x)&&std::isfinite(a[i].y)&&std::isfinite(a[i].z)&&std::isfinite(a[i].w)&&std::abs(a[i].x-b[i].x)<3e-5f&&std::abs(a[i].y-b[i].y)<3e-5f&&std::abs(a[i].z-b[i].z)<3e-5f&&std::abs(a[i].w-b[i].w)<3e-5f,message);
        };
        const auto baseline=render(native.Get());require(std::any_of(baseline.begin(),baseline.end(),[](V v){return v.x>.01f;}),"native fixture did not shade water");compare(render(patched.Get()),baseline,"disabled water changed native output");
        settings.enabled=true;runtime.applySettings(settings);

        compare(render(patched.Get()),baseline,"constant-height water changed its mean surface");
        for(UINT i=0;i<3;i++) {for(UINT y=0;y<32;y++) for(UINT x=0;x<32;x++) normals[i][y*32+x].w=float((x+2*y)%8)/7;context->UpdateSubresource(textures[i+1].Get(),0,nullptr,normals[i].data(),32*sizeof(V),0);}

        const auto heightBaseline=render(native.Get()),displaced=render(patched.Get());UINT changed=0;
        for(UINT i=0;i<displaced.size();i++) {const auto v=displaced[i];require(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w),"non-finite parallax");if(std::abs(v.x-heightBaseline[i].x)+std::abs(v.y-heightBaseline[i].y)+std::abs(v.z-heightBaseline[i].z)>1e-5f) ++changed;}
        require(changed>0,"height did not affect water normals");
        for(UINT y=0;y<16;y++) for(UINT x=0;x<16;x++) {const auto a=displaced[y*32+x],b=displaced[y*32+x+16];require(std::abs(a.x-b.x)<3e-5f && std::abs(a.y-b.y)<3e-5f && std::abs(a.z-b.z)<3e-5f,"stereo water mismatch");}
        vertexGeometry[14].x=1;context->UpdateSubresource(vsGeometry.Get(),0,nullptr,vertexGeometry.data(),0,0);compare(render(patched.Get()),heightBaseline,"incompatible vertex UVs did not retain native sampling");
        cb=nullptr;context->VSSetConstantBuffers(1,1,&cb);compare(render(patched.Get()),heightBaseline,"missing native scales did not retain native water");
        cb=vsMaterial.Get();context->VSSetConstantBuffers(1,1,&cb);vertexGeometry[14].x=0;context->UpdateSubresource(vsGeometry.Get(),0,nullptr,vertexGeometry.data(),0,0);
        settings.parallax=false;settings.reflectionDistance=4096;runtime.applySettings(settings);
        std::array<ComPtr<ID3D11Texture2D>,4> cubes;std::array<ComPtr<ID3D11ShaderResourceView>,4> cubeViews;
        const auto fillCube=[&](UINT cube,bool valid) {
            std::array<V,16> pixels{};
            for(UINT face=0;face<6;face++) {
                const auto colour=cube==0?(face==4?V{0,0,4,1}:V{4,0,0,1}):cube==1?V{valid?1.0f:0.0f,0,0,0}:cube==2?V{}:V{0,4,0,1};
                pixels.fill(colour);context->UpdateSubresource(cubes[cube].Get(),face,nullptr,pixels.data(),4*sizeof(V),0);
            }
        };
        for(UINT i=0;i<4;i++) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.ArraySize=6;d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
            check(device->CreateTexture2D(&d,nullptr,&cubes[i]),"environment cube");check(device->CreateShaderResourceView(cubes[i].Get(),nullptr,&cubeViews[i]),"environment view");fillCube(i,true);
        }
        environment.publishedAvailable=true;environment.publishedEnvironment=cubeViews[0].Get();environment.publishedValidity=cubeViews[1].Get();environment.publishedPosition=cubeViews[2].Get();
        const auto reflected=render(patched.Get());float reflectedChange=0;
        for(UINT i=0;i<reflected.size();i++) {
            require(std::isfinite(reflected[i].x)&&std::isfinite(reflected[i].z)&&reflected[i].w==heightBaseline[i].w,"reflection changed alpha or produced non-finite colour");
            reflectedChange+=reflected[i].z-heightBaseline[i].z;
        }
        require(reflectedChange>.1f,"outgoing water reflection sampled the wrong hemisphere or never contributed");
        fillCube(1,false);compare(render(patched.Get()),heightBaseline,"missing cubemap coverage did not preserve native water");fillCube(1,true);
        environment.previousAvailable=true;environment.previousEnvironment=cubeViews[3].Get();environment.previousValidity=cubeViews[1].Get();environment.previousPosition=cubeViews[2].Get();
        environment.transitionWeight=0;const auto oldReflection=render(patched.Get());environment.transitionWeight=.5f;const auto transition=render(patched.Get());
        for(UINT i=0;i<transition.size();i++) require(std::abs(transition[i].y-(oldReflection[i].y+reflected[i].y)*.5f)<1e-4f && std::abs(transition[i].z-(oldReflection[i].z+reflected[i].z)*.5f)<1e-4f,"water environment transition is not coherent");
        settings.reflectionDistance=64;runtime.applySettings(settings);compare(render(patched.Get()),heightBaseline,"distant reflection did not preserve native water");
        settings.reflectionDistance=4096;settings.reflections=false;runtime.applySettings(settings);compare(render(patched.Get()),heightBaseline,"disabled reflection did not preserve native water");
        settings.reflections=true;settings.screenReflections=true;environment.previousAvailable=false;environment.transitionWeight=1;runtime.applySettings(settings);
        const auto fxCubeFallback=render(patched.Get());
        float fxDifference=0;for(UINT i=0;i<fxCubeFallback.size();i++) {require(std::isfinite(fxCubeFallback[i].x) && fxCubeFallback[i].w==heightBaseline[i].w,"FX fallback changed native alpha");fxDifference+=std::abs(fxCubeFallback[i].x-heightBaseline[i].x);}
        require(fxDifference>.1f,"FX miss lost the world cubemap fallback");
        settings.reflections=false;runtime.applySettings(settings);compare(render(patched.Get()),heightBaseline,"FX miss without cube did not preserve native base water");
        settings.reflections=true;settings.reflectionDistance=64;runtime.applySettings(settings);compare(render(patched.Get()),fxCubeFallback,"FX distant water lost its world fallback");settings.reflectionDistance=4096;runtime.applySettings(settings);
        auto legacyColour=views[0].Get();auto* legacySampler=sampler.Get();
        context->PSSetShaderResources(9,1,&legacyColour);context->PSSetShaderResources(10,1,&legacyColour);
        context->PSSetSamplers(9,1,&legacySampler);context->PSSetSamplers(10,1,&legacySampler);
        for(const auto& names:std::array<std::array<const char*,2>,3>{{{"WaterColor_0000001C.dxbc","WaterColor_0000021C.dxbc"},{"WaterColor_0000009F.dxbc","WaterColor_0000029F.dxbc"},{"WaterColor_000000BF.dxbc","WaterColor_000002BF.dxbc"}}}) {
            auto plainCode=bytes(root/"package/Shaders/Community/WaterLinearLighting"/names[0]);auto legacyCode=bytes(root/"package/Shaders/Community/WaterLinearLighting"/names[1]);
            ComPtr<ID3D11PixelShader> plain,legacy;check(device->CreatePixelShader(plainCode.data(),plainCode.size(),nullptr,&plain),"FX plain variant");check(device->CreatePixelShader(legacyCode.data(),legacyCode.size(),nullptr,&legacy),"FX legacy variant");
            compare(render(plain.Get()),render(legacy.Get()),"opaque-receiver SSR leaked into FX water reflection");
        }
        runtime.onDeviceCreated(nullptr,nullptr);context->ClearState();
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": native water variants, parallax, stereo, outgoing reflections, missing coverage, history transition, distance fade and disable passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"source root required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
