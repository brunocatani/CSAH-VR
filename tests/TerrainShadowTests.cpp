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
#include <vector>
using Microsoft::WRL::ComPtr;
namespace
{
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    void check(HRESULT hr,const char* message) {require(SUCCEEDED(hr),message);}
    ComPtr<ID3DBlob> compile(const std::filesystem::path& file)
    {
        ComPtr<ID3DBlob> code,error;const auto hr=D3DCompileFromFile(file.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&error);
        if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"shader compilation failed");return code;
    }
    struct F4 {float x,y,z,w;};
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        const auto hr=D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        check(hr,"D3D11 test device");require(level>=D3D_FEATURE_LEVEL_11_0,"D3D11 feature level");
        auto code=compile(root/"package/Shaders/Community/TerrainShadows/ShadowUpdateCS.hlsl");ComPtr<ID3D11ComputeShader> scan;
        check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&scan),"scan shader");
        constexpr UINT width=257,height=65;
        D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> terrain,shadow,staging;ComPtr<ID3D11ShaderResourceView> terrainView,shadowView;ComPtr<ID3D11UnorderedAccessView> shadowWrite;
        check(device->CreateTexture2D(&td,nullptr,&terrain),"terrain texture");check(device->CreateShaderResourceView(terrain.Get(),nullptr,&terrainView),"terrain SRV");
        td.Format=DXGI_FORMAT_R32G32_FLOAT;td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;check(device->CreateTexture2D(&td,nullptr,&shadow),"shadow texture");
        check(device->CreateShaderResourceView(shadow.Get(),nullptr,&shadowView),"shadow SRV");check(device->CreateUnorderedAccessView(shadow.Get(),nullptr,&shadowWrite),"shadow UAV");
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging),"readback texture");
        auto buffer=[&](UINT size,UINT flags) {D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.BindFlags=flags;ComPtr<ID3D11Buffer> b;check(device->CreateBuffer(&d,nullptr,&b),"buffer");return b;};
        auto update=buffer(16,D3D11_BIND_CONSTANT_BUFFER),sun=buffer(32,D3D11_BIND_CONSTANT_BUFFER),camera=buffer(23*16,D3D11_BIND_CONSTANT_BUFFER);
        std::array<F4,23> cameraData{};cameraData[20].x=cameraData[21].y=cameraData[22].z=1;context->UpdateSubresource(camera.Get(),0,nullptr,cameraData.data(),0,0);
        auto scanMap=[&](const std::vector<float>& values,F4 light) {
            context->UpdateSubresource(terrain.Get(),0,nullptr,values.data(),width*sizeof(float),0);
            std::array<F4,2> lights{{{},light}};context->UpdateSubresource(sun.Get(),0,nullptr,lights.data(),0,0);
            context->CSSetShader(scan.Get(),nullptr,0);auto* srv=terrainView.Get();context->CSSetShaderResources(0,1,&srv);auto* uav=shadowWrite.Get();context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
            auto* b=update.Get();context->CSSetConstantBuffers(0,1,&b);b=sun.Get();context->CSSetConstantBuffers(2,1,&b);b=camera.Get();context->CSSetConstantBuffers(12,1,&b);
            for(UINT i=0;i<(width+127)/128;i++) {
                struct {UINT slice;float spacing;float pad[2];} data{i,512,{}};
                context->UpdateSubresource(update.Get(),0,nullptr,&data,0,0);context->Dispatch(width,1,1);
            }
            uav=nullptr;context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);context->CopyResource(staging.Get(),shadow.Get());
            D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"shadow readback");
            std::vector<std::array<float,2>> result(width*height);for(UINT y=0;y<height;y++) std::memcpy(result.data()+y*width,static_cast<const char*>(map.pData)+map.RowPitch*y,width*8);
            context->Unmap(staging.Get(),0);return result;
        };
        std::vector<float> flat(width*height,0);auto lit=scanMap(flat,{-1,0,1,0});
        for(const auto& h:lit) require(std::abs(h[0])<.001f && std::abs(h[1])<.001f,"flat terrain self shadows");
        auto ridge=flat;for(UINT y=0;y<height;y++) ridge[y*width+126]=4096;
        auto east=scanMap(ridge,{-1,0,1,0});
        require(east[32*width+125][0]==0,"shadow propagated toward light");
        require(east[32*width+130][0]>2000 && east[32*width+130][1]<2100 && east[32*width+130][1]>1800,"ridge failed across 128-texel dispatch boundary");
        require(east[32*width+150][0]==0,"shadow failed to end");
        auto west=scanMap(ridge,{1,0,1,0});require(west[32*width+122][0]>2000 && west[32*width+130][0]==0,"negative major direction failed");
        auto vertical=scanMap(ridge,{0,0,1,0});require(vertical[32*width+130][0]==0,"vertical sun shadowed flat ground");
        ridge=flat;for(UINT x=0;x<width;x++) ridge[30*width+x]=4096;
        auto north=scanMap(ridge,{0,-1,1,0});require(north[34*width+128][0]>2000 && north[29*width+128][0]==0,"Y-major propagation failed");
        auto south=scanMap(ridge,{0,1,1,0});require(south[26*width+128][0]>2000 && south[34*width+128][0]==0,"negative Y propagation failed");
        // Exercise diagonal wrap rejection at the map edge: opposite edges
        // must never occlude one another as the minor coordinate wraps.
        ridge=flat;ridge[0]=4096;auto diagonal=scanMap(ridge,{-1,-1,1,0});
        require(diagonal[(height-1)*width+1][0]==0,"minor-axis wrap leaked shadow across map boundary");
        ridge=flat;for(UINT y=0;y<height;y++) ridge[y*width+126]=4096;
        (void)scanMap(ridge,{-1,0,1,0});
        const auto include=(root/"package/Shaders/Community/TerrainShadows/TerrainShadows.hlsli").generic_string();
        const std::string source="#include \""+include+"\"\ncbuffer Fixture:register(b1){float4 Position;} RWTexture2D<float> Output:register(u0); [numthreads(1,1,1)] void main(){Output[uint2(0,0)]=TerrainVisibility(Position.xyz);}";
        ComPtr<ID3DBlob> receiverCode,error;check(D3DCompile(source.data(),source.size(),nullptr,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&receiverCode,&error),"receiver compile");
        ComPtr<ID3D11ComputeShader> receiver;check(device->CreateComputeShader(receiverCode->GetBufferPointer(),receiverCode->GetBufferSize(),nullptr,&receiver),"receiver shader");
        auto parameters=buffer(32,D3D11_BIND_SHADER_RESOURCE),fixture=buffer(16,D3D11_BIND_CONSTANT_BUFFER);
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;vd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;vd.Buffer.NumElements=2;
        ComPtr<ID3D11ShaderResourceView> parameterView;check(device->CreateShaderResourceView(parameters.Get(),&vd,&parameterView),"receiver parameters");
        td.Width=td.Height=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output,readback;ComPtr<ID3D11UnorderedAccessView> outputView;
        check(device->CreateTexture2D(&td,nullptr,&output),"receiver output");check(device->CreateUnorderedAccessView(output.Get(),nullptr,&outputView),"receiver UAV");
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&readback),"receiver readback");
        auto visibility=[&](F4 position,float strength,bool available=true) {
            const std::array<F4,2> p{{{-8192,12288,512,256},{float(width),float(height),strength,1}}};
            context->UpdateSubresource(parameters.Get(),0,nullptr,p.data(),0,0);context->UpdateSubresource(fixture.Get(),0,nullptr,&position,0,0);
            context->CSSetShader(receiver.Get(),nullptr,0);ID3D11ShaderResourceView* views[]{shadowView.Get(),available?parameterView.Get():nullptr};context->CSSetShaderResources(76,2,views);
            auto* b=fixture.Get();context->CSSetConstantBuffers(1,1,&b);auto* u=outputView.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);context->Dispatch(1,1,1);
            u=nullptr;context->CSSetUnorderedAccessViews(0,1,&u,nullptr);context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"visibility readback");
            const auto result=*static_cast<const float*>(mapped.pData);context->Unmap(readback.Get(),0);return result;
        };
        F4 point{-8192+130*512,12288+32*512,0,0};
        require(visibility(point,1)<.01f,"ridge did not shadow world receiver");
        require(visibility(point,0)>.99f && visibility(point,1,false)>.99f,"disabled/missing map is not neutral");
        point.z=6000;require(visibility(point,1)>.99f,"receiver above terrain shadow was darkened");
        point.x=-8193;point.z=0;require(visibility(point,1)>.99f,"receiver outside negative map extent was darkened");
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": flat ground, ridge, slice boundary, both axes/signs, vertical sun and map edge passed\n";
    }
}
int main(int argc,char** argv)
{
    try {require(argc>1,"source root required");run(argv[1],D3D_DRIVER_TYPE_WARP);run(argv[1],D3D_DRIVER_TYPE_HARDWARE);return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
