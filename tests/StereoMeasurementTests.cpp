#include "diagnostics/stereo_measurement/StereoMeasurement.h"
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <DirectXPackedVector.h>
#include <array>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <limits>
using Microsoft::WRL::ComPtr;
namespace sm=csah::diagnostics::stereo_measurement;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void check(HRESULT hr,const char* message){require(SUCCEEDED(hr),message);}
struct Texture{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11RenderTargetView> target;UINT bytes{};};
struct Fixture {
    static constexpr UINT Width=128,Height=64;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11DeviceContext1> extended;ComPtr<ID3D11InfoQueue> messages;
    std::array<Texture,5> textures;std::array<std::array<float,4>,96> camera{};
    ComPtr<ID3D11Buffer> cameraBuffer,stereoBuffer,sentinel;ComPtr<ID3D11UnorderedAccessView> sentinelView;
    ComPtr<ID3D11ComputeShader> compute;ComPtr<ID3D11Predicate> predicate;
    Fixture(D3D_DRIVER_TYPE driver){
        auto hr=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING)hr=D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        check(hr,"device");check(context.As(&extended),"context1");(void)device.As(&messages);
        constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
        for(UINT i=0;i<5;i++){auto& t=textures[i];t.bytes=4;D3D11_TEXTURE2D_DESC d{};d.Width=Width;d.Height=Height;d.Format=formats[i];d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
            check(device->CreateTexture2D(&d,nullptr,&t.texture),"texture");check(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.view),"SRV");check(device->CreateRenderTargetView(t.texture.Get(),nullptr,&t.target),"RTV");}
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(camera);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&cameraBuffer),"camera");
        bd.ByteWidth=16;const std::array<float,4> stereo{1,0,0,0};D3D11_SUBRESOURCE_DATA initialStereo{stereo.data(),0,0};check(device->CreateBuffer(&bd,&initialStereo,&stereoBuffer),"stereo");
        bd.ByteWidth=sm::kCounterCount*4;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;check(device->CreateBuffer(&bd,nullptr,&sentinel),"sentinel");
        D3D11_UNORDERED_ACCESS_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R32_UINT;vd.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;vd.Buffer.NumElements=sm::kCounterCount;check(device->CreateUnorderedAccessView(sentinel.Get(),&vd,&sentinelView),"sentinel UAV");
        const char* shader="[numthreads(1,1,1)] void main() {}";ComPtr<ID3DBlob> code;check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,nullptr),"canary compile");check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&compute),"canary shader");
        const D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION_PREDICATE,0};check(device->CreatePredicate(&query,&predicate),"predicate");
        sm::Runtime::get().onDeviceCreated(device.Get(),context.Get());sm::Runtime::get().setEnabled(true);
        csah::render::GpuTimingProfiler::setOnDemandGroups(8);
    }
    ~Fixture(){sm::Runtime::get().setEnabled(false);sm::Runtime::get().onDeviceCreated(nullptr,nullptr);csah::render::GpuTimingProfiler::setOnDemandGroups(0);context->ClearState();}
    template<class T,class F> void fill(UINT index,F fn){std::vector<T> data(Width*Height);for(UINT y=0;y<Height;y++)for(UINT x=0;x<Width;x++)data[y*Width+x]=fn(x,y);context->UpdateSubresource(textures[index].texture.Get(),0,nullptr,data.data(),Width*sizeof(T),0);}
    void baseline(){
        context->ClearState();camera={};
        // Deliberately poison the unbound prefix; native b12 starts at float4 16.
        for(UINT i=0;i<16;i++)camera[i].fill(std::numeric_limits<float>::quiet_NaN());
        auto row=[&](UINT i)->auto&{return camera[i+16];};
        row(0)[0]=row(1)[1]=row(2)[2]=row(3)[3]=1;
        row(20)[0]=row(21)[1]=row(22)[2]=row(23)[3]=1;
        for(UINT eye=0;eye<2;eye++){
            row(4+eye*4)={.01f,0,0,0};row(5+eye*4)={0,.01f,0,0};row(6+eye*4)={0,0,.01f,-3.5f};row(7+eye*4)={0,0,0,1};
            row(32+eye*4)={100,0,0,0};row(33+eye*4)={0,100,0,0};row(34+eye*4)={0,0,100,350};row(35+eye*4)={0,0,0,1};
        }
        row(59)={-1.5625f,0,0,0};row(60)={1.5625f,0,0,0};
        fill<std::array<unsigned char,4>>(0,[](UINT,UINT){return std::array<unsigned char,4>{128,64,32,255};});
        fill<std::array<std::uint16_t,2>>(1,[](UINT,UINT){return std::array<std::uint16_t,2>{32768,32768};});
        fill<std::array<unsigned char,4>>(2,[](UINT,UINT){return std::array<unsigned char,4>{64,16,0,0};});
        fill<float>(3,[](UINT,UINT){return .5f;});
        fill<std::array<std::uint16_t,2>>(4,[](UINT,UINT){return std::array<std::uint16_t,2>{0,0};});
    }
    std::vector<std::byte> read(Texture& t){
        D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=0;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"staging");context->CopyResource(staging.Get(),t.texture.Get());D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"read texture");
        std::vector<std::byte> result(Width*Height*t.bytes);for(UINT y=0;y<Height;y++)std::memcpy(result.data()+y*Width*t.bytes,static_cast<const std::byte*>(m.pData)+y*m.RowPitch,Width*t.bytes);context->Unmap(staging.Get(),0);return result;
    }
    std::array<UINT,sm::kCounterCount> take(bool material=true,D3D11_VIEWPORT vp={0,0,float(Width),float(Height),0,1},bool reset=true){
        context->ClearState();auto& runtime=sm::Runtime::get();if(reset)runtime.resetWorld();runtime.beginFrame();
        const auto beforeSubmitted=runtime.snapshot().submitted,beforeCompleted=runtime.snapshot().completed;
        std::array<std::vector<std::byte>,5> before;for(UINT i=0;i<5;i++)before[i]=read(textures[i]);
        context->UpdateSubresource(cameraBuffer.Get(),0,nullptr,camera.data(),0,0);context->RSSetViewports(1,&vp);
        if(material){ID3D11RenderTargetView* targets[]{textures[1].target.Get(),textures[4].target.Get()};context->OMSetRenderTargets(2,targets,nullptr);runtime.observeDraw(context.Get(),true,false);context->OMSetRenderTargets(0,nullptr,nullptr);}
        std::array<ID3D11ShaderResourceView*,5> views{};for(UINT i=0;i<5;i++)views[i]=textures[i].view.Get();context->PSSetShaderResources(0,4,views.data());
        auto* cb=stereoBuffer.Get();context->PSSetConstantBuffers(8,1,&cb);
        cb=cameraBuffer.Get();UINT first=16,count=64;extended->PSSetConstantBuffers1(12,1,&cb,&first,&count);
        UINT oldFirst=32,oldCount=16;extended->CSSetConstantBuffers1(0,1,&cb,&oldFirst,&oldCount);extended->CSSetConstantBuffers1(12,1,&cb,&oldFirst,&oldCount);extended->CSSetConstantBuffers1(8,1,&cb,&oldFirst,&oldCount);
        context->CSSetShader(compute.Get(),nullptr,0);context->CSSetShaderResources(0,5,views.data());auto* out=sentinelView.Get();context->CSSetUnorderedAccessViews(0,1,&out,nullptr);
        const std::array<UINT,sm::kCounterCount> sentinelData{123,456,789};context->UpdateSubresource(sentinel.Get(),0,nullptr,sentinelData.data(),0,0);
        context->Begin(predicate.Get());context->End(predicate.Get());context->SetPredication(predicate.Get(),TRUE);
        runtime.observeDraw(context.Get(),false,true);require(runtime.snapshot().submitted==beforeSubmitted+1,"qualified native ambient sample missing");
        runtime.observeDraw(context.Get(),false,true);require(runtime.snapshot().submitted==beforeSubmitted+1,"duplicate ambient draw sampled twice");
        ComPtr<ID3D11Predicate> restoredPredicate;BOOL predicateValue{};context->GetPredication(&restoredPredicate,&predicateValue);require(restoredPredicate==predicate&&predicateValue==TRUE,"predicate restore");
        ComPtr<ID3D11ComputeShader> restored;context->CSGetShader(&restored,nullptr,nullptr);require(restored==compute,"compute shader restore");
        for(UINT slot:{0,8,12}){ComPtr<ID3D11Buffer> buffer;UINT f{},n{};extended->CSGetConstantBuffers1(slot,1,&buffer,&f,&n);require(buffer==cameraBuffer&&f==32&&n==16,"CS constant window restore");}
        {ComPtr<ID3D11Buffer> buffer;UINT f{},n{};extended->PSGetConstantBuffers1(12,1,&buffer,&f,&n);require(buffer==cameraBuffer&&f==16&&n==64,"PS camera unchanged");}
        ComPtr<ID3D11UnorderedAccessView> restoredOutput;context->CSGetUnorderedAccessViews(0,1,&restoredOutput);require(restoredOutput==sentinelView,"caller UAV restore");
        for(UINT i=0;i<5;i++){ComPtr<ID3D11ShaderResourceView> restoredView;context->CSGetShaderResources(i,1,&restoredView);require(restoredView.Get()==views[i],"CS input restore");if(i<4){context->PSGetShaderResources(i,1,restoredView.ReleaseAndGetAddressOf());require(restoredView.Get()==views[i],"PS input unchanged");}}
        context->SetPredication(nullptr,FALSE);
        // Only the offline test flushes/waits. Runtime readback is DONOTFLUSH/DO_NOT_WAIT.
        context->Flush();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(runtime.snapshot().completed==beforeCompleted){runtime.beginFrame();require(std::chrono::steady_clock::now()<deadline,"readback timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        for(UINT i=0;i<5;i++)require(before[i]==read(textures[i]),"measurement modified a native scene texture");
        D3D11_BUFFER_DESC bd{};sentinel->GetDesc(&bd);bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;bd.BindFlags=0;ComPtr<ID3D11Buffer> copy;
        check(device->CreateBuffer(&bd,nullptr,&copy),"sentinel staging");context->CopyResource(copy.Get(),sentinel.Get());D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&map),"sentinel read");require(std::memcmp(map.pData,sentinelData.data(),sizeof(sentinelData))==0,"measurement wrote caller UAV");context->Unmap(copy.Get(),0);
        return runtime.snapshot().last;
    }
    void validate(){if(!messages)return;for(UINT64 i=0;i<messages->GetNumStoredMessages();i++){SIZE_T size{};check(messages->GetMessage(i,nullptr,&size),"message size");std::vector<std::byte> data(size);auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data());check(messages->GetMessage(i,message,&size),"message");if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){std::cerr<<message->pDescription<<'\n';throw std::runtime_error("D3D warning/error");}}messages->ClearStoredMessages();}
};
void run(D3D_DRIVER_TYPE driver){
    Fixture f(driver);f.baseline();auto v=f.take();require(v[sm::Samples]==256&&v[sm::GeometryMatch]>200&&v[sm::AllWithMotion]==v[sm::GeometryMatch],"matching stereo plane/camera window");
    f.baseline();f.fill<float>(3,[](UINT x,UINT){return x<64?.2f:.5f;});v=f.take();require(v[sm::DepthMismatch]>200&&v[sm::GeometryMatch]==0,"disocclusion rejected");
    f.baseline();f.fill<std::array<std::uint16_t,2>>(1,[](UINT x,UINT){return std::array<std::uint16_t,2>{std::uint16_t(x<64?49151:32768),32768};});v=f.take();require(v[sm::GeometryMatch]>200&&v[sm::NormalMatch]==0&&v[sm::AllMaterialMatch]==0,"normal error distinguished from depth match");
    f.baseline();f.fill<std::array<unsigned char,4>>(0,[](UINT x,UINT){return std::array<unsigned char,4>{static_cast<unsigned char>(x<64?255:128),64,32,255};});v=f.take();require(v[sm::AlbedoMatch]==0&&v[sm::AllMaterialMatch]==0,"albedo mismatch measured");
    f.baseline();f.fill<std::array<unsigned char,4>>(2,[](UINT x,UINT){return std::array<unsigned char,4>{static_cast<unsigned char>(x<64?128:64),16,0,0};});v=f.take();require(v[sm::MaterialMatch]==0,"material mismatch measured");
    f.baseline();f.fill<std::array<std::uint16_t,2>>(4,[](UINT x,UINT){return std::array<std::uint16_t,2>{DirectX::PackedVector::XMConvertFloatToHalf(x<64?.01f:0.0f),0};});v=f.take();require(v[sm::AllMaterialMatch]>200&&v[sm::MotionMatch]==0&&v[sm::MaxMotionError]>9000,"motion-copy error measured independently");
    for(auto depth:{1.0f,.005f,std::numeric_limits<float>::quiet_NaN()}){f.baseline();f.fill<float>(3,[&](UINT,UINT){return depth;});v=f.take();require(v[sm::GeometryMatch]==0&&(v[sm::Sky]+v[sm::FirstPerson]+v[sm::Invalid])==256,"sky first-person or invalid depth rejected");}
    f.baseline();f.camera[16+34][3]=f.camera[16+38][3]=50;v=f.take();require(v[sm::Near]==256,"near receiver exclusion");
    f.baseline();f.camera[16+60][0]=1000;v=f.take();require(v[sm::Outside]==256,"outside other eye excluded");
    f.baseline();v=f.take(false);require(v[sm::GeometryMatch]>200&&v[sm::MotionAvailable]==0&&v[sm::AllWithMotion]==0,"missing motion never counted as agreement");
    f.baseline();v=f.take(true,{16,8,96,48,0,1});require(v[sm::Samples]==144&&v[sm::GeometryMatch]>100,"active viewport, not allocation size");
    f.baseline();f.camera[16+8][3]=.05f;f.camera[16+36][3]=-5;v=f.take();require(v[sm::GeometryMatch]>200,"asymmetric eye projections supported");
    // Across 16 sampled frames every pixel is visited, not one fixed lattice.
    f.baseline();f.fill<float>(3,[](UINT x,UINT y){return x==80&&y==24?1.0f:.5f;});
    UINT skyCount=0;for(UINT phase=0;phase<16;phase++){v=f.take(true,{0,0,128,64,0,1},phase==0);skyCount+=v[sm::Sky];}
    require(skyCount==1,"rotating sample phases visit a thin feature exactly once");
    f.baseline();const std::array<float,4> mono{};f.context->UpdateSubresource(f.stereoBuffer.Get(),0,nullptr,mono.data(),0,0);v=f.take();require(v[sm::NonStereo]==256&&v[sm::GeometryMatch]==0,"mono view is not stereo evidence");
    auto& runtime=sm::Runtime::get();runtime.setEnabled(false);runtime.beginFrame();runtime.observeDraw(f.context.Get(),true,false);runtime.observeDraw(f.context.Get(),false,true);require(runtime.snapshot().submitted==0,"off leaves measurement idle");
    f.validate();std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": stereo agreement, rejection, motion error, viewport, no scene writes and complete state restoration passed\n";
}
int main(){try{run(D3D_DRIVER_TYPE_WARP);run(D3D_DRIVER_TYPE_HARDWARE);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
