#include "Features/volumetric_lighting/NativeFrameSnapshot.h"
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
using Microsoft::WRL::ComPtr;
namespace
{
    struct F4 {float x,y,z,w;};
    void require(bool test,const char* message) {if(!test) throw std::runtime_error(message);}
    void check(HRESULT hr,const char* message) {require(SUCCEEDED(hr),message);}
    ComPtr<ID3DBlob> compile(const std::filesystem::path& path,const char* profile)
    {
        ComPtr<ID3DBlob> code,error;auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
        if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"compile");return code;
    }
    void scatteringViewport(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& root)
    {
        context->ClearState();
        const auto path=root/"package/Shaders/Community/SubsurfaceScattering/SubsurfaceScatteringCS.hlsl";
        ComPtr<ID3DBlob> code,error;
        check(D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error),"scattering compile");
        ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"scattering shader");
        std::array<F4,24> colour{},albedo{};std::array<float,24> depth{},skin{};
        colour.fill({0,0,4,1});albedo.fill({1,1,1,1});depth.fill(.5f);skin.fill(3.0f/255);
        colour[10]=colour[11]={3,0,0,1};colour[12]=colour[13]={0,3,0,1};
        D3D11_TEXTURE2D_DESC d{};d.Width=8;d.Height=3;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<ComPtr<ID3D11Texture2D>,4> textures;std::array<ComPtr<ID3D11ShaderResourceView>,4> views;
        for(unsigned i=0;i<4;i++) {
            d.Format=(i==0 || i==3)?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R32_FLOAT;
            const void* data=i==0?static_cast<const void*>(colour.data()):i==1?static_cast<const void*>(depth.data()):i==2?static_cast<const void*>(skin.data()):static_cast<const void*>(albedo.data());
            D3D11_SUBRESOURCE_DATA initial{data,8u*((i==0 || i==3)?16u:4u),0};
            check(device->CreateTexture2D(&d,&initial,&textures[i]),"scattering input");check(device->CreateShaderResourceView(textures[i].Get(),nullptr,&views[i]),"scattering SRV");
            auto* v=views[i].Get();context->CSSetShaderResources(i,1,&v);
        }
        ComPtr<ID3D11Texture2D> output,readback;ComPtr<ID3D11UnorderedAccessView> uav;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        check(device->CreateTexture2D(&d,nullptr,&output),"scattering output");check(device->CreateUnorderedAccessView(output.Get(),nullptr,&uav),"scattering UAV");
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&d,nullptr,&readback),"scattering readback");
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> buffer;check(device->CreateBuffer(&bd,nullptr,&buffer),"scattering constants");
        auto* b=buffer.Get();context->CSSetConstantBuffers(0,1,&b);context->CSSetShader(shader.Get(),nullptr,0);
        for(unsigned axis=0;axis<2;axis++) {
            std::array<F4,4> parameters{{{8,3,axis?0.0f:1.0f,axis?1.0f:0.0f},{1440,1,.001f,3.0f/255},{.5f/255,0,0,0},{2,1,4,1}}};
            context->UpdateSubresource(buffer.Get(),0,nullptr,parameters.data(),0,0);auto* u=uav.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);context->Dispatch(1,1,1);u=nullptr;context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
            context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"scattering map");
            for(unsigned y=0;y<3;y++)for(unsigned x=0;x<8;x++) {
                const auto p=reinterpret_cast<const F4*>(static_cast<const char*>(m.pData)+y*m.RowPitch)[x];const auto expected=colour[y*8+x];
                require(std::abs(p.x-expected.x)<1e-5f && std::abs(p.y-expected.y)<1e-5f && std::abs(p.z-expected.z)<1e-5f,"skin scattering crossed an active eye or viewport boundary");
            }
            context->Unmap(readback.Get(),0);
        }
        context->ClearState();
    }
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
        auto code=compile(root/"package/Shaders/Community/VolumetricLighting/HeightFogPS.hlsl","ps_5_0");ComPtr<ID3D11PixelShader> pixel;
        check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel),"fog PS");
        code=compile(root/"package/Shaders/Community/VolumetricLighting/FullscreenVS.hlsl","vs_5_0");ComPtr<ID3D11VertexShader> vertex;
        check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"fullscreen VS");
        auto buffer=[&](UINT bytes){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> b;check(device->CreateBuffer(&d,nullptr,&b),"buffer");return b;};
        auto camera=buffer(77*16),stereo=buffer(16),parameters=buffer(64);
        std::array<F4,77> cameraData{};cameraData[20].x=cameraData[21].y=cameraData[22].z=1;
        for(unsigned eye=0;eye<2;eye++) {auto row=32+eye*4;cameraData[row].x=1;cameraData[row+1].y=1;cameraData[row+2].w=1000;cameraData[row+3].w=1;}
        cameraData[60].z=8192;cameraData[73]={.2f,.4f,.6f,0};cameraData[76].x=.0001f;
        const F4 stereoData{1,0,0,0};context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);
        auto* b=camera.Get();context->PSSetConstantBuffers(2,1,&b);b=stereo.Get();context->PSSetConstantBuffers(1,1,&b);b=parameters.Get();context->PSSetConstantBuffers(4,1,&b);
        D3D11_TEXTURE2D_DESC td{};td.Width=4;td.Height=1;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<F4,4> white{};for(auto& p:white)p={1,1,1,1};D3D11_SUBRESOURCE_DATA init{white.data(),sizeof(white),0};
        ComPtr<ID3D11Texture2D> scene,depth,target,readback;ComPtr<ID3D11ShaderResourceView> sceneView,depthView;ComPtr<ID3D11RenderTargetView> targetView;
        check(device->CreateTexture2D(&td,&init,&scene),"scene");check(device->CreateShaderResourceView(scene.Get(),nullptr,&sceneView),"scene SRV");
        td.Format=DXGI_FORMAT_R32_FLOAT;std::array<float,4> depths{.005f,.5f,.5f,1};init={depths.data(),sizeof(depths),0};
        check(device->CreateTexture2D(&td,&init,&depth),"depth");check(device->CreateShaderResourceView(depth.Get(),nullptr,&depthView),"depth SRV");
        td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;check(device->CreateTexture2D(&td,nullptr,&target),"target");check(device->CreateRenderTargetView(target.Get(),nullptr,&targetView),"RTV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&readback),"readback");
        ID3D11ShaderResourceView* sources[]{sceneView.Get(),depthView.Get()};context->PSSetShaderResources(0,2,sources);auto* rtv=targetView.Get();context->OMSetRenderTargets(1,&rtv,nullptr);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_VIEWPORT viewport{0,0,4,1,0,1};context->RSSetViewports(1,&viewport);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster),"raster");context->RSSetState(raster.Get());
        std::array<F4,4> fog{{{.0001f,1024,0,0},{1,1,.9f,0},{0,0,4,1},{0,0,4,1}}};
        auto draw=[&](){context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);context->UpdateSubresource(parameters.Get(),0,nullptr,fog.data(),0,0);context->Draw(3,0);context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"readback map");std::array<F4,4> out;std::memcpy(out.data(),m.pData,sizeof(out));context->Unmap(readback.Get(),0);return out;};
        const auto stereoExpected=draw();
        const F4 geometryStereoFlag{0,0,0,0};context->UpdateSubresource(stereo.Get(),0,nullptr,&geometryStereoFlag,0,0);
        auto independentStereo=draw();for(unsigned i=0;i<4;i++) require(std::abs(stereoExpected[i].x-independentStereo[i].x)<1e-7,"fog depended on geometry stereo flag");
        context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);
        ComPtr<ID3D11Buffer> frozen;
        require(csah::volumetric_lighting::snapshotConstantBuffer(device.Get(),context.Get(),camera.Get(),frozen),"snapshot capture");
        auto overwritten=cameraData;overwritten[73]={9,9,9,0};context->UpdateSubresource(camera.Get(),0,nullptr,overwritten.data(),0,0);
        D3D11_BUFFER_DESC snapshotRead{};snapshotRead.ByteWidth=sizeof(cameraData);snapshotRead.Usage=D3D11_USAGE_STAGING;snapshotRead.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> snapshotStaging;check(device->CreateBuffer(&snapshotRead,nullptr,&snapshotStaging),"snapshot staging");context->CopyResource(snapshotStaging.Get(),frozen.Get());
        D3D11_MAPPED_SUBRESOURCE snap{};check(context->Map(snapshotStaging.Get(),0,D3D11_MAP_READ,0,&snap),"snapshot read");
        require(std::memcmp(snap.pData,cameraData.data(),sizeof(cameraData))==0,"retained camera data changed with native buffer reuse");context->Unmap(snapshotStaging.Get(),0);
        auto output=draw();require(output[0].x==1,"first-person depth fogged");require(output[1].x<.99f && output[2].x>.999f,"per-eye absolute altitude lost");
        // Independent midpoint quadrature of the exponential medium.
        double integral=0;for(unsigned i=0;i<10000;i++) integral+=std::exp(-(i+.5)*.1/1024)*.1;
        const double expected=1-(1-std::exp(-.0001*integral))*.8;require(std::abs(output[1].x-expected)<.0001,"analytic ray integral disagrees with quadrature");
        fog[0].x=0;output=draw();require(output[1].x==1 && output[2].x==1,"zero density not neutral");fog[0].x=.0001f;fog[1].x=0;output=draw();require(output[1].x==1,"disable not neutral");fog[1].x=1;
        fog[1].x=2;const auto interior=draw();
        require(std::abs(interior[1].x-interior[2].x)<1e-6f,"interior fog depends on absolute room altitude");
        cameraData[59].z=-22000;cameraData[60].z=22000;const auto shiftedInterior=draw();
        require(std::abs(shiftedInterior[1].x-interior[1].x)<1e-6f && std::abs(shiftedInterior[2].x-interior[2].x)<1e-6f,"interior altitude changed medium");
        require(std::abs(interior[1].x-(1-(1-std::exp(-.1))*.8))<.0001,"interior homogeneous fog integral mismatch");
        cameraData[59].z=0;cameraData[60].z=10000;fog[1].x=1;
        // Rotate view Z into world X: a horizontal ray must use the stable limit.
        cameraData[20]={0,0,1,0};cameraData[22]={1,0,0,0};output=draw();require(std::abs(output[1].x-(1-(1-std::exp(-.1))*.8))<.0001,"horizontal limit or inverse-view rotation failed");
        fog[0].z=22000;fog[0].y=64;fog[1].z=.6f;output=draw();require(std::isfinite(output[1].x) && std::abs(output[1].x-.52f)<.0001,"deep medium overflow/opacity cap");
        fog[0].w=2000;output=draw();require(output[1].x==1,"clear distance ignored");
        const auto savedCamera=cameraData;const auto savedFog=fog;
        for(unsigned eye=0;eye<2;eye++) {auto row=32+eye*4;cameraData[row]={1,0,0,0};cameraData[row+1]={0,1,0,0};cameraData[row+2]={0,0,0,1};cameraData[row+3]={0,0,-1,1};}
        cameraData[20]={1,0,0,0};cameraData[21]={0,0,-1,0};cameraData[22]={0,1,0,0};cameraData[59].z=cameraData[60].z=0;
        fog={{{.0001f,1024,0,0},{1,0,.9f,0},{0,0,4,1},{0,0,4,1}}};depths.fill(1);context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),sizeof(depths),0);
        output=draw();require(output[0].x<.5f && std::abs(output[0].x-output[2].x)<1e-6f && std::abs(output[1].x-output[3].x)<1e-6f,"sky horizon stereo seam or far-plane divide");
        depths={.005f,.5f,.5f,1};context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),sizeof(depths),0);cameraData=savedCamera;fog=savedFog;
        // The real volumetric composite must use shadow visibility only when
        // opted in, and preserve the analytic result when the mix is zero.
        cameraData[20]={1,0,0,0};cameraData[22]={0,0,1,0};fog={{{.0001f,1024,0,0},{1,1,.9f,0},{0,0,4,1},{0,0,4,1}}};
        const auto analytic=draw();
        code=compile(root/"package/Shaders/Community/VolumetricLighting/CompositeVolumePS.hlsl","ps_5_0");ComPtr<ID3D11PixelShader> composite;
        check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&composite),"volumetric composite");context->PSSetShader(composite.Get(),nullptr,0);
        auto image=buffer(5*16),frame=buffer(8*16);std::array<F4,5> imageData{};std::array<F4,8> frameData{};frameData[2]={3000,1000,1,0};
        context->UpdateSubresource(image.Get(),0,nullptr,imageData.data(),0,0);context->UpdateSubresource(frame.Get(),0,nullptr,frameData.data(),0,0);
        b=image.Get();context->PSSetConstantBuffers(0,1,&b);b=frame.Get();context->PSSetConstantBuffers(1,1,&b);b=stereo.Get();context->PSSetConstantBuffers(3,1,&b);
        D3D11_TEXTURE2D_DESC zeroDesc{};zeroDesc.Width=4;zeroDesc.Height=1;zeroDesc.MipLevels=zeroDesc.ArraySize=1;zeroDesc.SampleDesc.Count=1;zeroDesc.Format=DXGI_FORMAT_R32_FLOAT;zeroDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::array<float,4> zeros{};D3D11_SUBRESOURCE_DATA zeroInit{zeros.data(),sizeof(zeros),0};ComPtr<ID3D11Texture2D> zeroTexture;ComPtr<ID3D11ShaderResourceView> zeroView;
        check(device->CreateTexture2D(&zeroDesc,&zeroInit,&zeroTexture),"zero volume");check(device->CreateShaderResourceView(zeroTexture.Get(),nullptr,&zeroView),"zero volume SRV");
        ID3D11ShaderResourceView* compositeInputs[]{zeroView.Get(),depthView.Get(),depthView.Get(),sceneView.Get(),zeroView.Get()};context->PSSetShaderResources(0,5,compositeInputs);
        output=draw();require(std::abs(output[1].x-analytic[1].x)<.00001,"combined path changed analytic fog at zero volume mix");
        fog[1].w=1;output=draw();require(output[1].x<analytic[1].x-.005f,"volume occlusion did not shade fog scattering");
        // Reduced and offset regions in the same allocation must still select
        // the right-eye origin. Depth and output viewports can differ.
        const auto validateRegions=[&]() {
            fog={{{.0001f,1024,0,0},{1,1,.9f,0},{1,0,2,1},{0,0,2,1}}};
            depths={.5f,.5f,.005f,.005f};
            context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),sizeof(depths),0);
            viewport={1,0,2,1,0,1};context->RSSetViewports(1,&viewport);
            auto reduced=draw();require(reduced[1].x<.99f && reduced[2].x>.999f,"reduced fog viewport crossed the stereo seam or sampled inactive depth");
            fog[2]={0,0,4,1};viewport={0,0,4,1,0,1};context->RSSetViewports(1,&viewport);
            auto expanded=draw();require(expanded[1].x<.99f && expanded[2].x>.999f,"fog output and depth regions were conflated");
        };
        context->PSSetShader(pixel.Get(),nullptr,0);context->PSSetShaderResources(0,2,sources);validateRegions();
        context->PSSetShader(composite.Get(),nullptr,0);context->PSSetShaderResources(0,5,compositeInputs);validateRegions();
        const auto common=(root/"package/Shaders/Community/VolumetricLighting/VolumetricCommon.hlsli").generic_string();
        const std::string computeSource="#include \""+common+"\"\ncbuffer Fixture:register(b4){float4 P;} RWTexture2D<float4> Out:register(u0); [numthreads(4,1,1)] void main(uint3 id:SV_DispatchThreadID){Out[id.xy]=SampleDensity(0,P.xyz).xxxx;}";
        ComPtr<ID3DBlob> computeCode,computeError;check(D3DCompile(computeSource.data(),computeSource.size(),nullptr,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&computeCode,&computeError),"density shader compile");
        ComPtr<ID3D11ComputeShader> densityShader;check(device->CreateComputeShader(computeCode->GetBufferPointer(),computeCode->GetBufferSize(),nullptr,&densityShader),"density shader");
        zeroDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;zeroDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;ComPtr<ID3D11Texture2D> densityTexture;ComPtr<ID3D11UnorderedAccessView> densityOutput;
        check(device->CreateTexture2D(&zeroDesc,nullptr,&densityTexture),"density output");check(device->CreateUnorderedAccessView(densityTexture.Get(),nullptr,&densityOutput),"density UAV");
        frameData[2]={3000,1000,.003f,1};context->UpdateSubresource(frame.Get(),0,nullptr,frameData.data(),0,0);
        b=camera.Get();context->CSSetConstantBuffers(2,1,&b);b=frame.Get();context->CSSetConstantBuffers(3,1,&b);b=parameters.Get();context->CSSetConstantBuffers(4,1,&b);context->CSSetShader(densityShader.Get(),nullptr,0);
        auto density=[&](F4 view){context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);fog[0]=view;context->UpdateSubresource(parameters.Get(),0,nullptr,fog.data(),0,0);auto* u=densityOutput.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);context->Dispatch(1,1,1);u=nullptr;context->CSSetUnorderedAccessViews(0,1,&u,nullptr);context->CopyResource(readback.Get(),densityTexture.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"density readback");float result=*static_cast<const float*>(m.pData);context->Unmap(readback.Get(),0);return result;};
        const auto fixedWorldDensity=density({500,-200,900,0});cameraData[20]={0,0,1,0};cameraData[22]={1,0,0,0};
        require(std::abs(fixedWorldDensity-density({900,-200,500,0}))<.000001,"volume density rotates with the headset");
        scatteringViewport(device.Get(),context.Get(),root);
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": analytic integration, stereo altitude, first-person exclusion, horizontal ray, rotation, opacity cap and neutral gates passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"root required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
