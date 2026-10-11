#include "Features/water_effects/WaterEffectsRuntime.h"
#include "Features/ibl/IblRuntime.h"
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
    struct F4 {float x,y,z,w;};
    void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
    void check(HRESULT hr,const char* message) {require(SUCCEEDED(hr),message);}
    ComPtr<ID3DBlob> compile(const std::string& source,const char* entry,const char* target,const char* origin=nullptr)
    {
        ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source.data(),source.size(),origin,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"compile failed");return code;
    }
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        constexpr UINT width=128,height=64;
        auto include=(root/"package/Shaders/Community/WaterEffects/WaterReflections.hlsl").generic_string();
        std::string source="#include \""+include+"\"\ncbuffer Fixture:register(b0){float4 Normal;} float4 Test(float4 p:SV_Position):SV_Target0 {WaterRaytraceInput i;i.packedUv=(p.xy-WaterReflectionParameters[5].xy)/WaterReflectionParameters[5].zw;i.eyeIndex=i.packedUv.x<.5?0:1;i.waterNormal=Normal.xyz;float3 ray=incidentViewFromScreen(i.packedUv,i.eyeIndex!=0);i.eyeRelativeReceiver=float3(0,0,100/max(.001,-ray.y));return TraceScreenWaterReflection(i);}";
        auto code=compile(source,"Test","ps_5_0",include.c_str());ComPtr<ID3D11PixelShader> pixel;check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel),"trace PS");
        code=compile("float4 main(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}","main","vs_5_0");ComPtr<ID3D11VertexShader> vertex;check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"VS");
        auto buffer=[&](UINT bytes){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> b;check(device->CreateBuffer(&d,nullptr,&b),"buffer");return b;};
        auto camera=buffer(61*16),fixture=buffer(16);std::array<F4,61> frame{};
        // World Z is up; the camera looks along world Y. A horizontal water
        // plane is 100 units below the eyes; a vertical wall is 1000 ahead.
        frame[0]={1,0,0,0};frame[1]={0,0,1,0};frame[2]={0,1,0,0};frame[20]=frame[0];frame[21]=frame[1];frame[22]=frame[2];
        for(UINT eye=0;eye<2;eye++) {
            auto p=4+eye*4;frame[p]={1,0,0,0};frame[p+1]={0,1,0,0};frame[p+2]={0,0,1,-1};frame[p+3]={0,0,1,0};
            auto q=32+eye*4;frame[q]={1,0,0,0};frame[q+1]={0,1,0,0};frame[q+2]={0,0,0,1};frame[q+3]={0,0,-1,1};
        }
        frame[59]={-3,0,100,0};frame[60]={3,0,100,0};context->UpdateSubresource(camera.Get(),0,nullptr,frame.data(),0,0);auto* b=camera.Get();context->PSSetConstantBuffers(12,1,&b);b=fixture.Get();context->PSSetConstantBuffers(0,1,&b);
        F4 normal{0,0,1,0};context->UpdateSubresource(fixture.Get(),0,nullptr,&normal,0,0);
        D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<F4> colours(width*height);for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++)colours[y*width+x]=x<width/2?F4{3,0,0,1}:F4{0,3,0,1};
        const float wallDepth=(.999f+.01f)/1.01f;std::vector<F4> depths(width*height,F4{wallDepth,0,0,0});
        ComPtr<ID3D11Texture2D> scene,depth,target,readback;ComPtr<ID3D11ShaderResourceView> sceneView,depthView;ComPtr<ID3D11RenderTargetView> rtv;
        D3D11_SUBRESOURCE_DATA initial{colours.data(),width*sizeof(F4),0};
        auto sceneDescription=td;sceneDescription.Width/=2;sceneDescription.Height/=2;
        std::vector<F4> halfColours(sceneDescription.Width*sceneDescription.Height);
        for(UINT y=0;y<sceneDescription.Height;y++)for(UINT x=0;x<sceneDescription.Width;x++)halfColours[y*sceneDescription.Width+x]=x<sceneDescription.Width/2?F4{3,0,0,1}:F4{0,3,0,1};
        D3D11_SUBRESOURCE_DATA halfInitial{halfColours.data(),sceneDescription.Width*sizeof(F4),0};
        check(device->CreateTexture2D(&sceneDescription,&halfInitial,&scene),"half resolution scene");check(device->CreateShaderResourceView(scene.Get(),nullptr,&sceneView),"scene SRV");initial.pSysMem=depths.data();check(device->CreateTexture2D(&td,&initial,&depth),"depth");check(device->CreateShaderResourceView(depth.Get(),nullptr,&depthView),"depth SRV");
        td.BindFlags=D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&td,nullptr,&target),"target");check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"RTV");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&readback),"readback");
        auto* v=sceneView.Get();context->PSSetShaderResources(1,1,&v);v=depthView.Get();context->PSSetShaderResources(7,1,&v);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&vp);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
        auto& runtime=csah::water_effects::Runtime::get();runtime.onDeviceCreated(device.Get(),context.Get());csah::water_effects::Settings settings;settings.enabled=true;settings.reflections=false;settings.screenReflections=true;runtime.applySettings(settings);
        csah::ibl::SslrEnvironmentView environment;
        require(!csah::water_effects::Settings{}.reflections && !csah::water_effects::Settings{}.screenReflections && csah::water_effects::Settings{}.reflectionDistance==0,
            "water reflections must be independent opt-ins without a hidden sea-distance cutoff");
        // A global cube can be available throughout all four water modes.
        ComPtr<ID3D11Texture2D> cube;ComPtr<ID3D11ShaderResourceView> cubeView;
        D3D11_TEXTURE2D_DESC cubeDesc{};cubeDesc.Width=cubeDesc.Height=cubeDesc.MipLevels=cubeDesc.SampleDesc.Count=1;
        cubeDesc.ArraySize=6;cubeDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        cubeDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;cubeDesc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        check(device->CreateTexture2D(&cubeDesc,nullptr,&cube),"mode cube");check(device->CreateShaderResourceView(cube.Get(),nullptr,&cubeView),"mode cube view");
        environment.publishedAvailable=environment.previousAvailable=true;
        environment.publishedEnvironment=environment.publishedValidity=environment.publishedPosition=cubeView.Get();
        environment.previousEnvironment=environment.previousValidity=environment.previousPosition=cubeView.Get();
        D3D11_BUFFER_DESC readDesc{};readDesc.ByteWidth=96;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> modeReadback;check(device->CreateBuffer(&readDesc,nullptr,&modeReadback),"mode readback");
        for(unsigned mode=0;mode<5;mode++) {
            settings.enabled=mode!=4;settings.reflections=(mode&1)!=0 || mode==4;settings.screenReflections=(mode&2)!=0 || mode==4;
            runtime.applySettings(settings);
            const bool expectedCube=settings.enabled&&settings.reflections,expectedFx=settings.enabled&&settings.screenReflections;
            require(runtime.cubeReflectionsRequested()==expectedCube,"water cube request leaked from FX or global state");
            {
                auto binding=runtime.scopeReflections(context.Get(),true,environment);
                for(UINT slot=69;slot<75;slot++) {ComPtr<ID3D11ShaderResourceView> selected;context->PSGetShaderResources(slot,1,&selected);
                    require(bool(selected)==expectedCube,"current/previous water cube did not follow its own toggle");}
                for(UINT slot:{79u,80u}) {ComPtr<ID3D11ShaderResourceView> selected;context->PSGetShaderResources(slot,1,&selected);
                    require(bool(selected)==expectedFx,"FX scene input did not follow its own toggle");}
                ComPtr<ID3D11ShaderResourceView> selected;context->PSGetShaderResources(75,1,&selected);
                require(bool(selected)==(expectedCube||expectedFx),"water master/combined disabled mode is not neutral");
                if(selected) {
                    ComPtr<ID3D11Resource> resource;selected->GetResource(&resource);context->CopyResource(modeReadback.Get(),resource.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(modeReadback.Get(),0,D3D11_MAP_READ,0,&mapped),"mode parameters");
                    std::array<F4,6> values{};std::memcpy(values.data(),mapped.pData,sizeof(values));context->Unmap(modeReadback.Get(),0);
                    require(values[0].z==(expectedCube?1.f:0.f) && values[0].y==(expectedCube?1.f:0.f) && values[4].x==(expectedFx?1.f:0.f),
                        "GPU reflection flags coupled the independent switches");
                }
            }
        }
        environment={};settings.enabled=true;settings.reflections=false;settings.screenReflections=true;runtime.applySettings(settings);
        const auto draw=[&](){auto* out=rtv.Get();context->OMSetRenderTargets(1,&out,nullptr);{auto binding=runtime.scopeReflections(context.Get(),true,environment);context->Draw(3,0);}context->OMSetRenderTargets(0,nullptr,nullptr);ComPtr<ID3D11ShaderResourceView> a,c;context->PSGetShaderResources(79,1,&a);context->PSGetShaderResources(80,1,&c);require(!a&&!c,"scene aliases leaked");context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"readback map");std::vector<F4> result(width*height);for(UINT y=0;y<height;y++)std::memcpy(result.data()+y*width,static_cast<const char*>(m.pData)+y*m.RowPitch,width*sizeof(F4));context->Unmap(readback.Get(),0);return result;};
        const auto reflected=draw();unsigned hits[2]{};
        for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++){auto p=reflected[y*width+x];require(std::isfinite(p.w),"nonfinite trace");if(p.w>.05f){auto eye=x/(width/2);hits[eye]++;require(eye==0?(p.x>2.9f&&p.y==0):(p.y>2.9f&&p.x==0),"reflection sampled the other eye's colour");}}
        require(hits[0]>50 && hits[1]>50,"planar water ray failed to hit the wall in both eyes");
        // Flattening must recover the same planar reflection despite ripples.
        normal={.5f,0,.8660254f,0};context->UpdateSubresource(fixture.Get(),0,nullptr,&normal,0,0);settings.reflectionFlatness=1;runtime.applySettings(settings);const auto flattened=draw();
        for(UINT i=0;i<reflected.size();i++)require(std::abs(flattened[i].w-reflected[i].w)<.0001f,"flatness did not recover the mirror plane");
        for(UINT y=0;y<height;y++)for(UINT x=width/2;x<width;x++)depths[y*width+x]={1,0,0,0};
        context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),width*sizeof(F4),0);const auto missingEye=draw();unsigned leftHits=0;
        for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++) {auto p=missingEye[y*width+x];if(x<width/2) leftHits+=p.w>.05f;else require(p.w==0,"other eye's depth fabricated an own-eye reflection");}
        require(leftHits>50,"missing right-eye data disabled valid left-eye reflections");
        depths.assign(width*height,F4{1,0,0,0});context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),width*sizeof(F4),0);for(const auto& p:draw())require(p.w==0,"cleared sky depth produced an SSR hit");
        depths.assign(width*height,F4{.005f,0,0,0});context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),width*sizeof(F4),0);for(const auto& p:draw())require(p.w==0,"first-person geometry leaked into water SSR");
        // Four reduced eye extents, including an odd width and a nonzero
        // origin. Unused texture capacity is poisoned blue/first-person.
        normal={0,0,1,0};context->UpdateSubresource(fixture.Get(),0,nullptr,&normal,0,0);
        for(const auto rect:std::array<D3D11_VIEWPORT,4>{{{0,0,84,42,0,1},{0,0,74,36,0,1},{0,0,64,32,0,1},{4,2,42,22,0,1}}}) {
            vp=rect;context->RSSetViewports(1,&vp);
            depths.assign(width*height,F4{.005f,0,0,0});
            for(UINT y=UINT(vp.TopLeftY);y<UINT(vp.TopLeftY+vp.Height);y++)for(UINT x=UINT(vp.TopLeftX);x<UINT(vp.TopLeftX+vp.Width);x++)depths[y*width+x]={wallDepth,0,0,0};
            halfColours.assign(halfColours.size(),F4{0,0,9,1});
            for(UINT y=UINT(vp.TopLeftY/2);y<UINT((vp.TopLeftY+vp.Height)/2);y++)for(UINT x=UINT(vp.TopLeftX/2);x<UINT((vp.TopLeftX+vp.Width)/2);x++)halfColours[y*sceneDescription.Width+x]=float(x)+.5f<(vp.TopLeftX+vp.Width/2)/2?F4{3,0,0,1}:F4{0,3,0,1};
            context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),width*sizeof(F4),0);
            context->UpdateSubresource(scene.Get(),0,nullptr,halfColours.data(),sceneDescription.Width*sizeof(F4),0);
            auto scaled=draw();unsigned scaledHits[2]{};
            for(UINT y=UINT(vp.TopLeftY);y<UINT(vp.TopLeftY+vp.Height);y++)for(UINT x=UINT(vp.TopLeftX);x<UINT(vp.TopLeftX+vp.Width);x++) {
                const auto p=scaled[y*width+x];if(p.w>.05f) {
                    const auto eye=(x-UINT(vp.TopLeftX))/UINT(vp.Width/2);scaledHits[eye]++;
                    require(eye==0?(p.x>2.9f&&p.y<.001f&&p.z<.001f):(p.y>2.9f&&p.x<.001f&&p.z<.001f),"reduced SSR sampled another eye or inactive allocation");
                }
            }
            require(scaledHits[0]>8 && scaledHits[1]>8,"reduced SSR lost one eye");
        }
        runtime.onDeviceCreated(nullptr,nullptr);context->ClearState();
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": stereo wall hits "<<hits[0]<<"/"<<hits[1]<<", own-eye colour, flatness, sky/first-person rejection and bindings passed\n";
    }
}
int main(int argc,char** argv)
{
    try{require(argc>1,"root required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
