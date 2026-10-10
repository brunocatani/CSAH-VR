#include "Features/inverse_square/InverseSquareRuntime.h"
#include "InverseSquareShaders.h"
#include <d3dcompiler.h>
#include <array>
#include <bit>
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
        auto texture=[&,td](F4 value){std::array<F4,64> pixels;pixels.fill(value);D3D11_SUBRESOURCE_DATA init{pixels.data(),8*sizeof(F4),0};ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> v;check(device->CreateTexture2D(&td,&init,&t),"fixture texture");check(device->CreateShaderResourceView(t.Get(),nullptr,&v),"fixture SRV");return v;};
        auto albedo=texture({1,1,1,1}),normal=texture({.5f,.5f,0,0}),material=texture({.5f,.5f,0,1}),depth=texture({.5f,.5f,.5f,.5f});
        ID3D11ShaderResourceView* psInputs[]{albedo.Get(),normal.Get(),material.Get(),depth.Get()};context->PSSetShaderResources(0,4,psInputs);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(device->CreateSamplerState(&sd,&sampler),"sampler");ID3D11SamplerState* samplers[]{sampler.Get(),sampler.Get(),sampler.Get(),sampler.Get()};context->PSSetSamplers(0,4,samplers);
        td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;std::array<ComPtr<ID3D11Texture2D>,2> targets;std::array<ComPtr<ID3D11RenderTargetView>,2> rtvs;std::array<ComPtr<ID3D11UnorderedAccessView>,2> uavs;
        for(unsigned i=0;i<2;i++){check(device->CreateTexture2D(&td,nullptr,&targets[i]),"output");check(device->CreateRenderTargetView(targets[i].Get(),nullptr,&rtvs[i]),"RTV");check(device->CreateUnorderedAccessView(targets[i].Get(),nullptr,&uavs[i]),"UAV");}
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&td,nullptr,&staging),"readback");
        context->VSSetShader(vertex.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT viewport{0,0,8,8,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
        auto& runtime=csah::inverse_square::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());runtime.setDispatchReady(true);csah::inverse_square::Settings settings;
        bool localEnabled=false;
        auto pbr=buffer(64,D3D11_BIND_CONSTANT_BUFFER),wet=buffer(32,D3D11_BIND_CONSTANT_BUFFER),hair=buffer(16,D3D11_BIND_CONSTANT_BUFFER);
        std::array<F4,4> pbrData{{{0,1,1,0},{0,1,0,0},{1,1,.32f,.02f},{1,1,1,0}}};
        std::array<F4,2> wetData{};F4 hairData{};
        auto classMask=texture({0,0,0,0});
        auto bindMaterials=[&](){
            context->UpdateSubresource(pbr.Get(),0,nullptr,pbrData.data(),0,0);context->UpdateSubresource(wet.Get(),0,nullptr,wetData.data(),0,0);context->UpdateSubresource(hair.Get(),0,nullptr,&hairData,0,0);
            for(auto slot:{7u,9u,10u}) {auto* value=slot==7?pbr.Get():slot==9?wet.Get():hair.Get();context->PSSetConstantBuffers(slot,1,&value);context->CSSetConstantBuffers(slot,1,&value);}
            for(auto slot:{47u,81u,82u}) {auto* value=slot==47?classMask.Get():slot==81?albedo.Get():material.Get();context->PSSetShaderResources(slot,1,&value);context->CSSetShaderResources(slot,1,&value);}
        };bindMaterials();
        auto readback=[&](){std::array<F4,128> out;for(unsigned i=0;i<2;i++){context->CopyResource(staging.Get(),targets[i].Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"readback map");for(unsigned y=0;y<8;y++)std::memcpy(out.data()+i*64+y*8,static_cast<const char*>(m.pData)+m.RowPitch*y,8*sizeof(F4));context->Unmap(staging.Get(),0);}return out;};
        auto a=bytes(generated/"PS_00000004_original.dxbc"),b=bytes(generated/"PS_00000004_replacement.dxbc");ComPtr<ID3D11PixelShader> nativePS,replacementPS;check(device->CreatePixelShader(a.data(),a.size(),nullptr,&nativePS),"native PS");check(device->CreatePixelShader(b.data(),b.size(),nullptr,&replacementPS),"replacement PS");
        require(!csah::inverse_square::taggedShader(nativePS.Get()) && csah::inverse_square::tagShader(replacementPS.Get()) && csah::inverse_square::taggedShader(replacementPS.Get()),"shader tags");
        auto pixelDraw=[&](bool replacement){context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);ID3D11RenderTargetView* r[]{rtvs[0].Get(),rtvs[1].Get()};context->OMSetRenderTargets(2,r,nullptr);context->PSSetShader(replacement?replacementPS.Get():nativePS.Get(),nullptr,0);{auto scope=runtime.scope(context.Get(),false,replacement,localEnabled);context->DrawInstanced(6,2,0,0);}context->OMSetRenderTargets(0,nullptr,nullptr);ComPtr<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(78,1,&restored);require(!restored,"pixel t78 leaked");return readback();};
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
        settings.enabled=false;runtime.applySettings(settings);localEnabled=true;
        compare(pixelDraw(false),pixelDraw(true),1);
        wetData={{{1,1,.2f,1},{.35f,0,0,0}}};bindMaterials();
        auto dry=pixelDraw(false),wetResult=pixelDraw(true);
        for(unsigned i=0;i<64;i++) require(std::abs(wetResult[i].x-dry[i].x*.8f)<1e-6f,"local PS wet diffuse missing");
        wetData={};pbrData[0].x=1;bindMaterials();
        auto energy=pixelDraw(true);
        for(unsigned i=0;i<64;i++) require(std::abs(energy[i].x-dry[i].x*.84f)<1e-6f,"local PS PBR energy conservation missing");
        // Normal incidence GGX, independently evaluated for a .5 shininess/.5 specular material.
        const float rough=std::pow(2.0f/66,.25f),alpha=rough*rough,alpha2=alpha*alpha;
        const float expectedSpec=(1/(3.141592653589793f*alpha2))*(.25f/(1+2*alpha))*.16f*nativeFalloff;
        a=bytes(generated/"PS_00000204_original.dxbc");b=bytes(generated/"PS_00000204_replacement.dxbc");
        check(device->CreatePixelShader(a.data(),a.size(),nullptr,&nativePS),"spec native");check(device->CreatePixelShader(b.data(),b.size(),nullptr,&replacementPS),"spec replacement");
        auto specResult=pixelDraw(true);
        require(std::abs(specResult[73].x-expectedSpec)<expectedSpec*.002f,"local PS GGX disagrees with independent BRDF");
        // Off-axis light exercises the captured vector rather than a normal-incidence coincidence.
        lightData[1].x=lightData[2].x=100;context->UpdateSubresource(light.Get(),0,nullptr,lightData.data(),0,0);
        const float nl=1/std::sqrt(2.0f),nh=std::sqrt((1+nl)/2),f=.16f+.84f*std::pow(1-nh,5.0f);
        const float denominator=nh*nh*(alpha2-1)+1;
        const float tiltedSpec=alpha2/(3.141592653589793f*denominator*denominator)*(.5f/(nl*(1+alpha)+nl*alpha+nl*(1+alpha)+alpha))*f*nl*std::pow(1-20000.0f/(1024*1024),2.2f);
        auto tilted=pixelDraw(true);require(std::abs(tilted[73].x-tiltedSpec)<tiltedSpec*.003f,"local GGX light/view vector capture is incorrect");
        lightData[1].x=lightData[2].x=0;context->UpdateSubresource(light.Get(),0,nullptr,lightData.data(),0,0);
        auto authored=texture({.75f,.5f,1,.5f});auto* authoredView=authored.Get();context->PSSetShaderResources(45,1,&authoredView);
        auto authoredResult=pixelDraw(true);auto authoredNative=pixelDraw(false);
        require(std::abs(authoredResult[9].x-authoredNative[9].x*.21f)<1e-6f,"authored local metal diffuse energy missing");
        const float authoredSpec=1/(3.141592653589793f*.0625f)*(.25f/1.5f)*.58f*nativeFalloff;
        require(std::abs(authoredResult[73].x-authoredSpec)<authoredSpec*.003f,"authored local roughness/F0 missing");
        authoredView=nullptr;context->PSSetShaderResources(45,1,&authoredView);
        classMask=texture({3.0f/255,0,0,0});bindMaterials();compare(pixelDraw(false),pixelDraw(true),1);
        classMask=texture({2.0f/255,0,0,0});hairData={1,2,0,0};bindMaterials();auto baseHair=pixelDraw(false),enhancedHair=pixelDraw(true);
        require(baseHair[73].x>0 && std::abs(enhancedHair[73].x-baseHair[73].x*2)<1e-6f,"local hair highlight multiplier missing");
        require(enhancedHair[9].x==baseHair[9].x,"hair multiplier changed diffuse");
        hairData={};classMask=texture({0,0,0,0});bindMaterials();
        // Projected lamp colour must tint the replacement BRDF, not only native diffuse.
        auto cookie=texture({.25f,.5f,.75f,1});auto* cookieView=cookie.Get();context->PSSetShaderResources(7,1,&cookieView);auto* cookieSampler=sampler.Get();context->PSSetSamplers(7,1,&cookieSampler);
        a=bytes(generated/"PS_00001204_original.dxbc");b=bytes(generated/"PS_00001204_replacement.dxbc");
        check(device->CreatePixelShader(a.data(),a.size(),nullptr,&nativePS),"cookie native");check(device->CreatePixelShader(b.data(),b.size(),nullptr,&replacementPS),"cookie replacement");
        auto cookieResult=pixelDraw(true);
        require(std::abs(cookieResult[73].x-expectedSpec*.25f)<expectedSpec*.002f && std::abs(cookieResult[73].y-expectedSpec*.5f)<expectedSpec*.002f && std::abs(cookieResult[73].z-expectedSpec*.75f)<expectedSpec*.002f,"projected RGB light cookie lost");
        // The native comparison shadow result must still gate the new specular lobe.
        a=bytes(generated/"PS_00080208_original.dxbc");b=bytes(generated/"PS_00080208_replacement.dxbc");
        check(device->CreatePixelShader(a.data(),a.size(),nullptr,&nativePS),"shadow native");check(device->CreatePixelShader(b.data(),b.size(),nullptr,&replacementPS),"shadow replacement");
        for(unsigned eye=0;eye<2;eye++)for(unsigned row=0;row<4;row++) {F4 identity{};(&identity.x)[row]=1;lightData[13+eye*4+row]=identity;}
        lightData[26].x=1e9f;lightData[27].z=1;context->UpdateSubresource(light.Get(),0,nullptr,lightData.data(),0,0);
        D3D11_TEXTURE2D_DESC shadowDesc{};shadowDesc.Width=shadowDesc.Height=4;shadowDesc.MipLevels=1;shadowDesc.ArraySize=2;shadowDesc.SampleDesc.Count=1;shadowDesc.Format=DXGI_FORMAT_R32_FLOAT;shadowDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> shadow;ComPtr<ID3D11ShaderResourceView> shadowView;check(device->CreateTexture2D(&shadowDesc,nullptr,&shadow),"shadow texture");check(device->CreateShaderResourceView(shadow.Get(),nullptr,&shadowView),"shadow view");auto* shadowRaw=shadowView.Get();context->PSSetShaderResources(5,1,&shadowRaw);
        auto shadowSamplerDesc=sd;shadowSamplerDesc.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;shadowSamplerDesc.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;ComPtr<ID3D11SamplerState> shadowSampler;check(device->CreateSamplerState(&shadowSamplerDesc,&shadowSampler),"shadow sampler");auto* samplerRaw=shadowSampler.Get();context->PSSetSamplers(5,1,&samplerRaw);
        std::array<float,16> shadowPixels;shadowPixels.fill(1);for(unsigned slice=0;slice<2;slice++)context->UpdateSubresource(shadow.Get(),slice,nullptr,shadowPixels.data(),16,0);
        const auto unshadowed=pixelDraw(true);require(unshadowed[73].x>expectedSpec*.9f,"shadow fixture has no lamp highlight");
        shadowPixels.fill(0);for(unsigned slice=0;slice<2;slice++)context->UpdateSubresource(shadow.Get(),slice,nullptr,shadowPixels.data(),16,0);
        const auto shadowed=pixelDraw(true);require(shadowed[73].x==0 && shadowed[9].x==0,"local GGX bypassed native shadow visibility");
        pbrData[0].x=0;bindMaterials();
        for(const auto& pair:kInverseSquareShaders) if(!pair.compute) {
            check(device->CreatePixelShader(pair.original,pair.originalSize,nullptr,&nativePS),"all native variants");
            check(device->CreatePixelShader(pair.replacement,pair.replacementSize,nullptr,&replacementPS),"all replacement variants");
            auto old=pixelDraw(false),neutral=pixelDraw(true);
            for(unsigned i=0;i<128;i++) {
                const float* x=&old[i].x;const float* y=&neutral[i].x;
                for(unsigned lane=0;lane<4;lane++) require((std::isnan(x[lane]) && std::isnan(y[lane])) || x[lane]==y[lane] || std::abs(x[lane]-y[lane])<1e-6f,"neutral material scope changed a native PS variant");
            }
        }
        pbrData[0].x=0;bindMaterials();localEnabled=false;
        // Native tiled records are 64 bytes in VR (the flat donor uses a
        // different layout). This fixture uses the unchanged native radius.
        std::array<float,16> record{};record[4]=record[9]=1024;record[10]=record[11]=record[12]=1;record[14]=1;record[15]=2;
        std::array<UINT,128> list{};list[0]=1;
        auto structured=[&](const void* data,UINT stride){D3D11_BUFFER_DESC d{};d.ByteWidth=d.StructureByteStride=stride;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> resource;ComPtr<ID3D11ShaderResourceView> view;check(device->CreateBuffer(&d,&initial,&resource),"structured buffer");check(device->CreateShaderResourceView(resource.Get(),nullptr,&view),"structured SRV");return view;};
        auto lights=structured(record.data(),64),tiles=structured(list.data(),512);std::array<F4,9> tiledData{};tiledData[0]={0,0,.125f,.125f};tiledData[3]={0,1,1,0};context->UpdateSubresource(tiled.Get(),0,nullptr,tiledData.data(),0,0);cb=tiled.Get();context->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* csInputs[]{depth.Get(),albedo.Get(),normal.Get(),nullptr,material.Get(),nullptr,lights.Get(),tiles.Get()};context->CSSetShaderResources(0,8,csInputs);
        for(const auto* key:{"00000001","00000002"}) {
            a=bytes(generated/(std::string("CS_")+key+"_original.dxbc"));b=bytes(generated/(std::string("CS_")+key+"_replacement.dxbc"));ComPtr<ID3D11ComputeShader> oldCS,newCS;check(device->CreateComputeShader(a.data(),a.size(),nullptr,&oldCS),"native CS");check(device->CreateComputeShader(b.data(),b.size(),nullptr,&newCS),"replacement CS");
            auto compute=[&](bool replacement){context->CSSetShader(replacement?newCS.Get():oldCS.Get(),nullptr,0);ID3D11UnorderedAccessView* outputs[]{uavs[0].Get(),uavs[1].Get()};context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);{auto scope=runtime.scope(context.Get(),true,replacement,localEnabled);context->Dispatch(1,1,1);}ID3D11UnorderedAccessView* nulls[2]{};context->CSSetUnorderedAccessViews(0,2,nulls,nullptr);ComPtr<ID3D11ShaderResourceView> restored;context->CSGetShaderResources(78,1,&restored);require(!restored,"compute t78 leaked");return readback();};
            settings.enabled=false;runtime.applySettings(settings);const auto baseline=compute(false);compare(baseline,compute(true),1);
            settings.enabled=true;runtime.applySettings(settings);compare(baseline,compute(true),ratio);
            settings.enabled=false;runtime.applySettings(settings);localEnabled=true;
            compare(baseline,compute(true),1);
            wetData={{{1,1,.2f,1},{.35f,0,0,0}}};bindMaterials();auto wetLight=compute(true);
            for(unsigned i=0;i<64;i++) require(std::abs(wetLight[i].x-baseline[i].x*.8f)<1e-6f,"tiled wet diffuse missing");
            wetData={};pbrData[0].x=1;bindMaterials();auto pbrLight=compute(true);
            for(unsigned i=0;i<64;i++) require(std::abs(pbrLight[i].x-baseline[i].x*.84f)<1e-6f,"tiled energy conservation missing");
            record[0]=std::bit_cast<float>(16u);lights=structured(record.data(),64);auto* lightView=lights.Get();context->CSSetShaderResources(6,1,&lightView);
            auto tiledSpec=compute(true);require(std::abs(tiledSpec[73].x-expectedSpec)<expectedSpec*.002f,"tiled GGX disagrees with independent BRDF");
            record[0]=std::bit_cast<float>(24u);lights=structured(record.data(),64);lightView=lights.Get();context->CSSetShaderResources(6,1,&lightView);
            auto ambient=compute(false),ambientPbr=compute(true);compare(ambient,ambientPbr,1);
            record[0]=0;lights=structured(record.data(),64);lightView=lights.Get();context->CSSetShaderResources(6,1,&lightView);
            pbrData[0].x=0;bindMaterials();localEnabled=false;
        }
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": all 73 native/replacement shaders, real stereo PS and both tiled CS falloff, local wetness, GGX, energy conservation, projected colour, neutral parity, alpha and binding restoration passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"generated directory required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
