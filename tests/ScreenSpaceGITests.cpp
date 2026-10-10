#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "render/Fo4VrCamera.h"
#include "render/DxbcTextureSlots.h"
#include "D3D11AllocationFailure.h"
#include "SkylightingAmbient.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <fstream>
using Microsoft::WRL::ComPtr;
using V=csah::render::CameraVector;
namespace
{
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
    HRESULT STDMETHODCALLTYPE failTexture(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D** output)
    {if(output) *output=nullptr;return E_OUTOFMEMORY;}
    constexpr UINT Width=32,Height=16;
    struct Constants {UINT width{Width},height{Height},frame{},history{1};float x{},y{},vw{Width*4},vh{Height*2};float radius{100},thickness{32},power{2},strength{1};UINT gi{1},specular{1},temporal{},tiled{};};
    struct Texture {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11UnorderedAccessView> output;};
    struct Fixture
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        std::array<ComPtr<ID3D11ComputeShader>,4> shader;
        ComPtr<ID3D11Buffer> constants,camera,previousCamera;
        Constants settings;
        Fixture(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
        {
            D3D_FEATURE_LEVEL level;check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
            const std::array names{L"PrepareCS.hlsl",L"TraceCS.hlsl",L"FilterCS.hlsl",L"CaptureCS.hlsl"};
            for(std::size_t i=0;i<names.size();i++) {
                ComPtr<ID3DBlob> code,error;auto p=root/L"package/Shaders/Community/ScreenSpaceGI"/names[i];
                const auto hr=D3DCompileFromFile(p.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
                if(error) std::cerr.write(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());check(hr,"compile");
                if(i==0) {require(csah::render::declaresTexture2D(code->GetBufferPointer(),code->GetBufferSize(),0),"native texture slot metadata");require(!csah::render::declaresTexture2D(code->GetBufferPointer(),code->GetBufferSize(),11),"stale tiled slot metadata");require(!csah::render::declaresTexture2D(code->GetBufferPointer(),16,0),"truncated DXBC metadata");}
                check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader[i]),"compute shader");
            }
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&constants),"constants");
            bd.ByteWidth=sizeof(csah::render::Fo4VrFrameBuffer);check(device->CreateBuffer(&bd,nullptr,&camera),"camera");check(device->CreateBuffer(&bd,nullptr,&previousCamera),"previous camera");
            csah::render::Fo4VrFrameBuffer data{};
            data.view=data.inverseView={V{1,0,0,0},V{0,1,0,0},V{0,0,1,0},V{0,0,0,1}};
            for(auto& projection:data.projection) projection={V{1,0,0,0},V{0,1,0,0},V{0,0,1,0},V{0,0,1,0}};
            context->UpdateSubresource(camera.Get(),0,nullptr,&data,0,0);context->UpdateSubresource(previousCamera.Get(),0,nullptr,&data,0,0);
            auto* cb=camera.Get();context->CSSetConstantBuffers(12,1,&cb);cb=previousCamera.Get();context->CSSetConstantBuffers(10,1,&cb);cb=constants.Get();context->CSSetConstantBuffers(13,1,&cb);
        }
        Texture texture()
        {
            Texture t;D3D11_TEXTURE2D_DESC d{};d.Width=Width;d.Height=Height;d.ArraySize=2;d.MipLevels=d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            check(device->CreateTexture2D(&d,nullptr,&t.texture),"texture");check(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.view),"SRV");check(device->CreateUnorderedAccessView(t.texture.Get(),nullptr,&t.output),"UAV");return t;
        }
        void upload(Texture& t,const std::vector<V>& pixels)
        {
            for(UINT eye=0;eye<2;eye++) context->UpdateSubresource(t.texture.Get(),eye,nullptr,pixels.data()+eye*Width*Height,Width*sizeof(V),0);
        }
        std::vector<V> read(Texture& t)
        {
            D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"staging");context->CopyResource(staging.Get(),t.texture.Get());std::vector<V> result(Width*Height*2);
            for(UINT eye=0;eye<2;eye++) {D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),eye,D3D11_MAP_READ,0,&map),"readback");
                for(UINT y=0;y<Height;y++) {
                    const auto* row=static_cast<const std::byte*>(map.pData)+y*map.RowPitch;
                    auto* output=result.data()+eye*Width*Height+y*Width;
                    if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
                        const auto half=[](std::uint16_t value) {
                            const auto exponent=(value>>10)&31,mantissa=value&1023;
                            const float magnitude=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?std::numeric_limits<float>::infinity():std::ldexp(float(mantissa+1024),int(exponent)-25);
                            return value&0x8000?-magnitude:magnitude;
                        };
                        for(UINT x=0;x<Width;x++) {std::array<std::uint16_t,4> packed{};std::memcpy(packed.data(),row+x*8,8);output[x]={half(packed[0]),half(packed[1]),half(packed[2]),half(packed[3])};}
                    } else std::memcpy(output,row,Width*sizeof(V));
                }
                context->Unmap(staging.Get(),eye);}
            return result;
        }
        void dispatch(UINT index,const std::array<ID3D11ShaderResourceView*,8>& inputs,Texture& a,Texture& b)
        {
            context->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0);context->CSSetShader(shader[index].Get(),nullptr,0);
            context->CSSetShaderResources(0,8,inputs.data());const std::array<ID3D11UnorderedAccessView*,2> outputs{a.output.Get(),b.output.Get()},none{};
            context->CSSetUnorderedAccessViews(0,2,outputs.data(),nullptr);context->Dispatch(Width/8,Height/8,2);
            context->CSSetUnorderedAccessViews(0,2,none.data(),nullptr);const std::array<ID3D11ShaderResourceView*,8> noInputs{};context->CSSetShaderResources(0,8,noInputs.data());
        }
    };
    void checkAmbientConsumer(Fixture& f,const std::filesystem::path& root)
    {
        ComPtr<ID3DBlob> vertex,pixel,error;
        const char* vsSource="cbuffer EyeData:register(b0){uint Eye;};struct Out{float4 p:SV_POSITION;nointerpolation uint eye:EYEINDEX;};Out main(uint i:SV_VertexID){Out o;float2 uv=float2((i<<1)&2,i&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.eye=Eye;return o;}";
        check(D3DCompile(vsSource,std::strlen(vsSource),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error),"ambient fixture vertex");
        auto path=root/L"package/Shaders/Community/Skylighting/SkylightingAmbientTransform.hlsl";
        check(D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"PSMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error),"ambient fixture pixel");
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
        check(f.device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs),"ambient VS");check(f.device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps),"ambient PS");
        f.context->VSSetShader(vs.Get(),nullptr,0);f.context->PSSetShader(ps.Get(),nullptr,0);f.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;check(f.device->CreateRasterizerState(&rd,&raster),"ambient raster");f.context->RSSetState(raster.Get());
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=320;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> sky,eye;
        check(f.device->CreateBuffer(&bd,nullptr,&sky),"ambient sky constants");std::array<V,20> zeros{};f.context->UpdateSubresource(sky.Get(),0,nullptr,zeros.data(),0,0);
        auto* cb=sky.Get();f.context->PSSetConstantBuffers(13,1,&cb);bd.ByteWidth=16;check(f.device->CreateBuffer(&bd,nullptr,&eye),"ambient eye constants");cb=eye.Get();f.context->VSSetConstantBuffers(0,1,&cb);
        std::array<ComPtr<ID3D11Texture2D>,4> textures;std::array<ComPtr<ID3D11RenderTargetView>,4> targets;std::array<ID3D11RenderTargetView*,4> outputs{};
        D3D11_TEXTURE2D_DESC td{};td.Width=Width*4;td.Height=Height*2;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        for(UINT i=0;i<4;i++) {check(f.device->CreateTexture2D(&td,nullptr,&textures[i]),"ambient output");check(f.device->CreateRenderTargetView(textures[i].Get(),nullptr,&targets[i]),"ambient RTV");outputs[i]=targets[i].Get();}
        auto indirect=f.texture();std::vector<V> light(Width*Height*2);
        for(UINT i=0;i<light.size();i++) light[i]=i<Width*Height?V{.9f,0,0,.5f}:V{0,0,.9f,.5f};f.upload(indirect,light);
        auto* view=indirect.view.Get();f.context->PSSetShaderResources(57,1,&view);
        f.context->OMSetRenderTargets(4,outputs.data(),nullptr);
        for(UINT side=0;side<2;side++) {
            std::array<UINT,4> data{side,0,0,0};f.context->UpdateSubresource(eye.Get(),0,nullptr,data.data(),0,0);
            D3D11_VIEWPORT viewport{float(side*Width*2),0,Width*2,Height*2,0,1};f.context->RSSetViewports(1,&viewport);f.context->Draw(3,0);
        }
        f.context->OMSetRenderTargets(0,nullptr,nullptr);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(f.device->CreateTexture2D(&td,nullptr,&staging),"ambient readback");
        for(UINT target=0;target<4;target++) {
            f.context->CopyResource(staging.Get(),textures[target].Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(f.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"ambient map");
            for(UINT y=0;y<td.Height;y++) for(UINT x=0;x<td.Width;x++) {
                const auto value=reinterpret_cast<const V*>(static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch)[x];
                if(target==0) require(std::abs(value.x-.5508f)<.002f && std::abs(value.y-.6809625f)<.002f && std::abs(value.z-.811125f)<.002f && value.w==1,"ambient multibounce AO or alpha changed");
                if(target==1) require(value.x==1 && value.y==1 && value.z==1 && value.w==1,"diffuse AO changed native specular visibility");
                if(target==2) {const V expected=x<Width*2?V{.3f,0,0,0}:V{0,0,.3f,0};require(std::abs(value.x-expected.x)<1e-4f && std::abs(value.z-expected.z)<1e-4f && value.y==0 && value.w==0,"ambient GI scale, eye or alpha changed");}
                if(target==3) require(value.x==0 && value.y==0 && value.z==0 && value.w==0,"disabled specular GI changed specular output");
            }
            f.context->Unmap(staging.Get(),0);
        }
        // Compare an actual native ambient variant with its composed DXBC.
        // Shader-object creation alone does not prove a bytecode splice has
        // the correct operand count or adds irradiance at the intended output.
        auto originalPath=root/L"package/Shaders/Community/VerifiedDFLightAmbient/DFLightAmbient_1d065bd1_2912.dxbc";
        std::ifstream stream(originalPath,std::ios::binary);std::vector<char> originalBytes((std::istreambuf_iterator<char>(stream)),{});
        require(originalBytes.size()==2912,"native ambient fixture missing");ComPtr<ID3D11PixelShader> original,composed;
        check(f.device->CreatePixelShader(originalBytes.data(),originalBytes.size(),nullptr,&original),"native ambient fixture");
        const auto& contract=csah_skylighting_ambient_contracts[6];
        check(f.device->CreatePixelShader(contract.replacementBytecode,contract.replacementBytecodeLength,nullptr,&composed),"composed native fixture");
        ComPtr<ID3D11Buffer> nativePass;f.context->PSGetConstantBuffers(2,1,&nativePass);
        std::array<V,30> pass{};pass[0]={1.0f/(Width*4),1.0f/(Height*2),0,0};pass[29]={1,1,0,0};pass[8]=pass[9]=pass[10]={0,0,0,1};
        f.context->UpdateSubresource(nativePass.Get(),0,nullptr,pass.data(),0,0);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;check(f.device->CreateSamplerState(&sd,&sampler),"native sampler");std::array<ID3D11SamplerState*,3> samplers{sampler.Get(),sampler.Get(),sampler.Get()};f.context->PSSetSamplers(1,3,samplers.data());
        std::array<std::vector<V>,2> baseline;
        for(UINT phase=0;phase<3;phase++) {
            if(phase==2) {ID3D11ShaderResourceView* noParameters{};f.context->PSSetShaderResources(59,1,&noParameters);}
            f.context->PSSetShader(phase?composed.Get():original.Get(),nullptr,0);f.context->OMSetRenderTargets(2,outputs.data(),nullptr);
            for(UINT side=0;side<2;side++) {std::array<UINT,4> data{side,0,0,0};f.context->UpdateSubresource(eye.Get(),0,nullptr,data.data(),0,0);D3D11_VIEWPORT viewport{float(side*Width*2),0,Width*2,Height*2,0,1};f.context->RSSetViewports(1,&viewport);f.context->Draw(3,0);}
            f.context->OMSetRenderTargets(0,nullptr,nullptr);
            for(UINT target=0;target<2;target++) {
                f.context->CopyResource(staging.Get(),textures[target].Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(f.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"native comparison map");
                if(!phase) baseline[target].resize(td.Width*td.Height);
                for(UINT y=0;y<td.Height;y++) for(UINT x=0;x<td.Width;x++) {
                    const auto value=reinterpret_cast<const V*>(static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch)[x];
                    if(!phase) baseline[target][y*td.Width+x]=value;
                    else {
                        auto expected=baseline[target][y*td.Width+x];
                        if(target==0 && phase==1) {expected.x=expected.x*.5508f+(x<Width*2?.3f:0);expected.y*=.6809625f;expected.z=expected.z*.811125f+(x>=Width*2?.3f:0);}
                        require(std::isfinite(value.x)&&std::abs(value.x-expected.x)<.002f && std::abs(value.y-expected.y)<.002f && std::abs(value.z-expected.z)<.002f && value.w==expected.w,"native ambient composition changed the wrong lobe, irradiance or alpha");
                    }
                }
                f.context->Unmap(staging.Get(),0);
            }
        }
        D3D11_VIEWPORT viewport{0,0,Width*4,Height*2,0,1};f.context->RSSetViewports(1,&viewport);
    }
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        Fixture f(root,driver);auto position=f.texture(),normal=f.texture(),oldPosition=f.texture(),oldNormal=f.texture(),radiance=f.texture();
        for(const auto& contract:csah_skylighting_ambient_contracts) {
            ComPtr<ID3D11PixelShader> candidate;
            check(f.device->CreatePixelShader(contract.replacementBytecode,contract.replacementBytecodeLength,nullptr,&candidate),"native ambient shader composition rejected by D3D11");
        }
        auto diffuse=f.texture(),specular=f.texture(),filtered=f.texture(),filteredSpecular=f.texture(),oldDiffuse=f.texture(),oldSpecular=f.texture();
        std::vector<V> positions(Width*Height*2),normals(positions.size(),{0,0,-1,.5f}),light(positions.size());
        for(UINT eye=0;eye<2;eye++) for(UINT y=0;y<Height;y++) for(UINT x=0;x<Width;x++) {
            const auto i=eye*Width*Height+y*Width+x;
            positions[i]={((x+.5f)/Width*2-1)*100,(1-(y+.5f)/Height*2)*100,100,1};
            light[i]=eye?V{0,0,4,1}:V{4,0,0,1};
        }
        f.upload(position,positions);f.upload(oldPosition,positions);f.upload(normal,normals);f.upload(oldNormal,normals);f.upload(radiance,light);
        const std::array<ID3D11ShaderResourceView*,8> trace{position.view.Get(),normal.view.Get(),oldPosition.view.Get(),oldNormal.view.Get(),radiance.view.Get()};
        f.dispatch(1,trace,diffuse,specular);auto flat=f.read(diffuse);
        for(auto v:flat) require(std::isfinite(v.w)&&v.w>.9999f && v.x<1e-5f && v.y<1e-5f && v.z<1e-5f,"flat plane must not self-occlude or bounce");
        for(UINT eye=0;eye<2;eye++) for(UINT y=0;y<Height;y++) for(UINT x=19;x<Width;x++) {
            const auto i=eye*Width*Height+y*Width+x;positions[i].x*=.7f;positions[i].y*=.7f;positions[i].z=70;normals[i]={-1,0,0,.5f};
        }
        f.upload(position,positions);f.upload(oldPosition,positions);f.upload(normal,normals);f.upload(oldNormal,normals);
        f.dispatch(1,trace,diffuse,specular);auto corner=f.read(diffuse);float minAO=1,maxGI=0;
        for(UINT i=0;i<corner.size();i++) {
            const auto v=corner[i];require(std::isfinite(v.x)&&std::isfinite(v.z)&&v.w>=0&&v.w<=1,"finite bounded corner");
            if(i<Width*Height) require(v.y<1e-6f && v.z<1e-6f,"right-eye light leaked left");
            else require(v.x<1e-6f && v.y<1e-6f,"left-eye light leaked right");
            minAO=(std::min)(minAO,v.w);maxGI=(std::max)(maxGI,v.x+v.z);
        }
        require(minAO<.95f,"corner occlusion missing");require(maxGI>.001f,"visible indirect light missing");
        f.settings.history=0;f.dispatch(1,trace,diffuse,specular);auto reset=f.read(diffuse);
        for(const auto v:reset) require(v.x==0&&v.y==0&&v.z==0,"reset used stale radiance");
        f.settings.history=1;f.settings.gi=0;f.dispatch(1,trace,diffuse,specular);auto aoOnly=f.read(diffuse);
        for(const auto v:aoOnly) require(v.x==0&&v.y==0&&v.z==0,"AO-only mode emits GI");
        f.settings.gi=1;f.dispatch(1,trace,diffuse,specular);
        std::vector<V> stale(positions.size(),{100,100,100,.1f});f.upload(oldDiffuse,stale);f.upload(oldSpecular,stale);
        const std::array<ID3D11ShaderResourceView*,8> filter{position.view.Get(),normal.view.Get(),oldPosition.view.Get(),oldNormal.view.Get(),diffuse.view.Get(),specular.view.Get(),oldDiffuse.view.Get(),oldSpecular.view.Get()};
        f.settings.temporal=0;f.dispatch(2,filter,filtered,filteredSpecular);auto baseline=f.read(filtered);
        auto moved=positions;for(auto& v:moved) v.z+=10000;f.upload(oldPosition,moved);
        f.settings.temporal=1;f.dispatch(2,filter,filtered,filteredSpecular);auto rejected=f.read(filtered);
        for(UINT i=0;i<rejected.size();i++) require(std::abs(rejected[i].x-baseline[i].x)<1e-6f && std::abs(rejected[i].z-baseline[i].z)<1e-6f && std::abs(rejected[i].w-baseline[i].w)<1e-6f,"disocclusion retained history");
        f.upload(oldPosition,positions);f.dispatch(2,filter,filtered,filteredSpecular);auto clamped=f.read(filtered);
        for(auto v:clamped) require(v.x<10 && v.y<1e-6f && v.z<10 && v.w>=0 && v.w<=1,"temporal neighbourhood clamp failed");
        std::cout<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"Hardware")<<": stereo AO/GI, no self-occlusion, reset, disocclusion and temporal clamp passed\n";

        // Exercise the actual render transaction with native packed inputs,
        // including GPU preparation, per-eye outputs, capture and restoration.
        std::array<ComPtr<ID3D11Texture2D>,6> native;
        std::array<ComPtr<ID3D11ShaderResourceView>,6> nativeViews;
        const std::array formats{DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
        for(UINT i=0;i<native.size();i++) {
            D3D11_TEXTURE2D_DESC td{};td.Width=Width*4;td.Height=Height*2;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=formats[i];td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            check(f.device->CreateTexture2D(&td,nullptr,&native[i]),"native input");check(f.device->CreateShaderResourceView(native[i].Get(),nullptr,&nativeViews[i]),"native view");
            if(i<3) {
                std::vector<std::uint32_t> pixels(td.Width*td.Height,i==0?0x80008000u:i==1?0x80808080u:std::bit_cast<std::uint32_t>(.51f));
                f.context->UpdateSubresource(native[i].Get(),0,nullptr,pixels.data(),td.Width*4,0);
            } else {
                std::vector<V> pixels(td.Width*td.Height,i==3?V{.2f,.5f,.8f,1}:i==4?V{}:V{1,1,1,0});
                f.context->UpdateSubresource(native[i].Get(),0,nullptr,pixels.data(),td.Width*sizeof(V),0);
            }
        }
        std::array<V,30> passData{};passData[0]={1.0f/(Width*4),1.0f/(Height*2),0,0};passData[29]={1,1,0,0};
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(passData);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> pass,stereo;check(f.device->CreateBuffer(&bd,nullptr,&pass),"native pass");f.context->UpdateSubresource(pass.Get(),0,nullptr,passData.data(),0,0);
        bd.ByteWidth=16;check(f.device->CreateBuffer(&bd,nullptr,&stereo),"stereo");const V stereoData{1,0,0,0};f.context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);
        csah::render::Fo4VrFrameBuffer cameraData{};
        cameraData.view=cameraData.inverseView={V{1,0,0,0},V{0,1,0,0},V{0,0,1,0},V{0,0,0,1}};
        for(auto& inverse:cameraData.inverseProjection) inverse={V{1,0,0,0},V{0,1,0,0},V{0,0,0,1},V{0,0,.01f,0}};
        for(auto& projection:cameraData.projection) projection={V{1,0,0,0},V{0,1,0,0},V{0,0,0,100},V{0,0,1,0}};
        cameraData.inverseFirstPersonProjection=cameraData.inverseProjection;
        f.context->UpdateSubresource(f.camera.Get(),0,nullptr,&cameraData,0,0);
        auto* cb=pass.Get();f.context->PSSetConstantBuffers(2,1,&cb);cb=stereo.Get();f.context->PSSetConstantBuffers(8,1,&cb);cb=f.camera.Get();f.context->PSSetConstantBuffers(12,1,&cb);
        D3D11_VIEWPORT viewport{0,0,Width*4,Height*2,0,1};f.context->RSSetViewports(1,&viewport);
        const std::array<ID3D11ShaderResourceView*,4> ambientInputs{nativeViews[3].Get(),nativeViews[0].Get(),nativeViews[1].Get(),nativeViews[2].Get()};
        f.context->PSSetShaderResources(0,4,ambientInputs.data());
        auto* sentinel=radiance.view.Get();f.context->PSSetShaderResources(57,1,&sentinel);
        auto& runtime=csah::screen_space_gi::Runtime::get();runtime.onDeviceCreated(f.device.Get(),f.context.Get());
        csah::screen_space_gi::Settings runtimeSettings;runtimeSettings.enabled=true;runtime.applySettings(runtimeSettings);runtime.beginFrame();
        f.context->CSSetShader(f.shader[3].Get(),nullptr,0);
        {
            auto scope=runtime.scopeAmbient(f.context.Get(),true);
            ComPtr<ID3D11ShaderResourceView> selected;f.context->PSGetShaderResources(57,1,&selected);require(selected && selected.Get()!=sentinel,"runtime failed to publish ambient output");
            Texture generated;ComPtr<ID3D11Resource> resource;selected->GetResource(&resource);check(resource.As(&generated.texture),"generated texture");
            auto pixels=f.read(generated);for(auto v:pixels) require(std::isfinite(v.w)&&v.w>.99f && v.x==0 && v.y==0 && v.z==0,"first frame must have neutral plane lighting");
            ComPtr<ID3D11ComputeShader> restored;f.context->CSGetShader(&restored,nullptr,nullptr);require(restored.Get()==f.shader[3].Get(),"compute shader was not restored");
            checkAmbientConsumer(f,root);
        }
        ComPtr<ID3D11ShaderResourceView> restored;f.context->PSGetShaderResources(57,1,&restored);require(restored.Get()==sentinel,"pixel SRV was not restored");
        auto* source=nativeViews[4].Get();f.context->PSSetShaderResources(4,1,&source);source=nativeViews[5].Get();f.context->PSSetShaderResources(5,1,&source);
        runtime.captureRadiance(f.context.Get(),false);runtime.beginFrame();
        {auto scope=runtime.scopeAmbient(f.context.Get(),true);ComPtr<ID3D11ShaderResourceView> selected;f.context->PSGetShaderResources(57,1,&selected);require(selected && selected.Get()!=sentinel,"second frame missing");}
        runtimeSettings.enabled=false;runtime.applySettings(runtimeSettings);
        {auto scope=runtime.scopeAmbient(f.context.Get(),true);ComPtr<ID3D11ShaderResourceView> selected;f.context->PSGetShaderResources(57,1,&selected);require(!selected,"disabled GI bound output");}
        f.context->PSGetShaderResources(57,1,restored.ReleaseAndGetAddressOf());require(restored.Get()==sentinel,"disabled scope did not restore foreign slot");
        runtime.onDeviceCreated(f.device.Get(),f.context.Get());runtimeSettings.enabled=true;runtime.applySettings(runtimeSettings);runtime.beginFrame();
        {
            DeviceFailureScope failure(f.device.Get(),5,reinterpret_cast<void*>(&failTexture));
            auto scope=runtime.scopeAmbient(f.context.Get(),true);ComPtr<ID3D11ShaderResourceView> selected;f.context->PSGetShaderResources(57,1,&selected);require(!selected,"failed allocation published partial GI");
        }
        for(UINT frame=0;frame<60;frame++) runtime.beginFrame();
        {auto scope=runtime.scopeAmbient(f.context.Get(),true);ComPtr<ID3D11ShaderResourceView> selected;f.context->PSGetShaderResources(57,1,&selected);require(selected && selected.Get()!=sentinel,"allocation did not recover");}
        runtime.onDeviceCreated(nullptr,nullptr);
        // Reconstruct the same packed eye coordinates at reduced sizes in a
        // full-size allocation. The inactive capacity contains invalid depth.
        cb=f.constants.Get();f.context->CSSetConstantBuffers(13,1,&cb);cb=f.camera.Get();f.context->CSSetConstantBuffers(12,1,&cb);
        cb=stereo.Get();f.context->CSSetConstantBuffers(8,1,&cb);cb=pass.Get();f.context->CSSetConstantBuffers(2,1,&cb);
        for(const auto rect:std::array<D3D11_VIEWPORT,4>{{{0,0,84,22,0,1},{0,0,74,18,0,1},{0,0,64,16,0,1},{4,2,42,10,0,1}}}) {
            f.settings.x=rect.TopLeftX;f.settings.y=rect.TopLeftY;f.settings.vw=rect.Width;f.settings.vh=rect.Height;
            std::vector<float> depths(Width*4*Height*2,1);
            for(UINT y=UINT(rect.TopLeftY);y<UINT(rect.TopLeftY+rect.Height);y++)for(UINT x=UINT(rect.TopLeftX);x<UINT(rect.TopLeftX+rect.Width);x++)
                depths[y*Width*4+x]=(x-UINT(rect.TopLeftX))<UINT(rect.Width/2)?.5f:.75f;
            f.context->UpdateSubresource(native[2].Get(),0,nullptr,depths.data(),Width*4*sizeof(float),0);
            f.dispatch(0,{nativeViews[0].Get(),nativeViews[2].Get(),nativeViews[1].Get()},position,normal);
            const auto prepared=f.read(position);
            for(UINT eyeIndex=0;eyeIndex<2;eyeIndex++)for(UINT y=0;y<Height;y++)for(UINT x=0;x<Width;x++) {
                const auto p=prepared[(eyeIndex*Height+y)*Width+x];const float projected=(eyeIndex?.75f:.5f)*1.01f-.01f;
                const float expectedX=((float(x)+.5f)/Width*2-1)/(.01f*projected);
                const float expectedY=(1-(float(y)+.5f)/Height*2)/(.01f*projected);
                require(p.w==1 && std::abs(p.x-expectedX)<.001f && std::abs(p.y-expectedY)<.001f && std::abs(p.z-100/projected)<.001f,"reduced GI reconstruction sampled the wrong eye, depth or coordinate system");
            }
        }
        f.context->ClearState();
        std::cout<<"Native ambient transaction, first-frame neutrality, capture, disable and state restoration passed\n";
    }
}
int main(int argc,char** argv)
{
    try {
        std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
        require(argc>1,"source root required");csah::screen_space_gi::Settings settings;
        require(!settings.enabled && settings.indirectLight && !settings.specular,"safe defaults");
        settings.radius=std::numeric_limits<float>::quiet_NaN();settings.strength=std::numeric_limits<float>::infinity();settings.thickness=-10;
        settings=csah::screen_space_gi::sanitize(settings);require(settings.radius==256 && settings.strength==1 && settings.thickness==1,"settings bounds");
        run(std::filesystem::path(argv[1]),D3D_DRIVER_TYPE_WARP);run(std::filesystem::path(argv[1]),D3D_DRIVER_TYPE_HARDWARE);return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
