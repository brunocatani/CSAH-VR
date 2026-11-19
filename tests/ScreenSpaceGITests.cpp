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
#include <initializer_list>
#include <cstring>
#include <bit>
using Microsoft::WRL::ComPtr;
using V=csah::render::CameraVector;
namespace
{
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    void check(HRESULT value,const char* message) {require(SUCCEEDED(value),message);}
    HRESULT STDMETHODCALLTYPE failTexture(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D** output)
    {if(output) *output=nullptr;return E_OUTOFMEMORY;}
    constexpr UINT Width=32,Height=16,Mips=5;
    // Mirrors cbuffer Settings in ScreenSpaceGICommon.hlsli.
    struct Constants
    {
        UINT width{Width},height{Height},frame{},history{1};
        float x{},y{},vw{Width*4},vh{Height*2};
        float aoRadius{1},thickness{32},power{2},strength{1};
        UINT gi{1},specular{1},temporal{},tiled{};
        float effectRadius{100},giRadius{1},minScreenRadius{},saturation{1};
        float compensation{},compensationDistance{100},fadeStart{40000},fadeScale{1.0f/10000};
        float depthDisocclusion{.1f},blurRadius{2},distanceNormalisation{2};UINT maxFrames{16};
        UINT slices{4},steps{8},blur{1},resolutionMode{};
        float pbr[16]{};
    };
    static_assert(sizeof(Constants)==192);
    enum Pass : UINT {Prepare,PrefilterPosition,Reproject,PrefilterRadiance,Trace,Blur,Capture,PassCount};
    struct Texture
    {
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view,top;ComPtr<ID3D11UnorderedAccessView> output;
        std::array<ComPtr<ID3D11UnorderedAccessView>,Mips-1> mips;
    };
    struct Fixture
    {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        std::array<ComPtr<ID3D11ComputeShader>,PassCount> shader;
        ComPtr<ID3D11Buffer> constants,camera,previousCamera,stereo;
        ComPtr<ID3D11ShaderResourceView> noise;
        Constants settings;
        Fixture(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
        {
            D3D_FEATURE_LEVEL level;check(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"device");
            const std::array names{L"PrepareCS.hlsl",L"PrefilterPositionCS.hlsl",L"ReprojectCS.hlsl",L"PrefilterRadianceCS.hlsl",L"TraceCS.hlsl",L"BlurCS.hlsl",L"CaptureCS.hlsl"};
            for(std::size_t i=0;i<names.size();i++) {
                ComPtr<ID3DBlob> code,error;auto p=root/L"package/Shaders/Community/ScreenSpaceGI"/names[i];
                const auto hr=D3DCompileFromFile(p.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
                if(error) std::cerr.write(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());check(hr,"compile");
                if(i==Prepare) {require(csah::render::declaresTexture2D(code->GetBufferPointer(),code->GetBufferSize(),0),"native texture slot metadata");require(!csah::render::declaresTexture2D(code->GetBufferPointer(),code->GetBufferSize(),11),"stale tiled slot metadata");require(!csah::render::declaresTexture2D(code->GetBufferPointer(),16,0),"truncated DXBC metadata");}
                check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader[i]),"compute shader");
            }
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Constants);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&bd,nullptr,&constants),"constants");
            bd.ByteWidth=sizeof(csah::render::Fo4VrFrameBuffer);check(device->CreateBuffer(&bd,nullptr,&camera),"camera");check(device->CreateBuffer(&bd,nullptr,&previousCamera),"previous camera");
            csah::render::Fo4VrFrameBuffer data{};
            data.view=data.inverseView={V{1,0,0,0},V{0,1,0,0},V{0,0,1,0},V{0,0,0,1}};
            for(auto& projection:data.projection) projection={V{1,0,0,0},V{0,1,0,0},V{0,0,1,0},V{0,0,1,0}};
            // Pixel scale reconstruction: view x grows linearly with clip x.
            for(auto& inverse:data.inverseProjection) inverse={V{1,0,0,0},V{0,1,0,0},V{0,0,0,1},V{0,0,.01f,0}};
            data.inverseFirstPersonProjection=data.inverseProjection;
            context->UpdateSubresource(camera.Get(),0,nullptr,&data,0,0);context->UpdateSubresource(previousCamera.Get(),0,nullptr,&data,0,0);
            // Stereo 1 makes the fixture's view rays eye-local: x/z and y/z are
            // each eye's own NDC, the space the synthetic scenes are built in.
            bd.ByteWidth=16;check(device->CreateBuffer(&bd,nullptr,&stereo),"stereo");const V stereoData{1,0,0,0};context->UpdateSubresource(stereo.Get(),0,nullptr,&stereoData,0,0);
            auto* cb=camera.Get();context->CSSetConstantBuffers(12,1,&cb);cb=previousCamera.Get();context->CSSetConstantBuffers(10,1,&cb);cb=constants.Get();context->CSSetConstantBuffers(13,1,&cb);
            cb=stereo.Get();context->CSSetConstantBuffers(8,1,&cb);
            D3D11_TEXTURE2D_DESC nd{};nd.Width=128;nd.Height=8192;nd.MipLevels=nd.ArraySize=nd.SampleDesc.Count=1;nd.Format=DXGI_FORMAT_R8G8_UNORM;nd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            std::vector<std::uint16_t> half(128*8192,0x8080);const D3D11_SUBRESOURCE_DATA initial{half.data(),256,0};
            ComPtr<ID3D11Texture2D> noiseTexture;check(device->CreateTexture2D(&nd,&initial,&noiseTexture),"noise");check(device->CreateShaderResourceView(noiseTexture.Get(),nullptr,&noise),"noise view");
        }
        Texture texture(UINT mips=1,DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            Texture t;D3D11_TEXTURE2D_DESC d{};d.Width=Width;d.Height=Height;d.ArraySize=2;d.MipLevels=mips;d.SampleDesc.Count=1;
            d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            check(device->CreateTexture2D(&d,nullptr,&t.texture),"texture");check(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.view),"SRV");
            D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=d.Format;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;u.Texture2DArray.ArraySize=2;
            check(device->CreateUnorderedAccessView(t.texture.Get(),&u,&t.output),"UAV");
            if(mips>1) {
                D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=d.Format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;s.Texture2DArray.MipLevels=1;s.Texture2DArray.ArraySize=2;
                check(device->CreateShaderResourceView(t.texture.Get(),&s,&t.top),"top SRV");
                for(UINT m=1;m<mips;m++) {u.Texture2DArray.MipSlice=m;check(device->CreateUnorderedAccessView(t.texture.Get(),&u,&t.mips[m-1]),"mip UAV");}
            }
            return t;
        }
        template<class T> void upload(Texture& t,const std::vector<T>& pixels)
        {
            D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);
            for(UINT eye=0;eye<2;eye++) context->UpdateSubresource(t.texture.Get(),D3D11CalcSubresource(0,eye,d.MipLevels),nullptr,pixels.data()+eye*Width*Height,Width*sizeof(T),0);
        }
        // Signed inverse view depth texture (R32_FLOAT), mip 0 of both eyes.
        std::vector<float> readDepth(Texture& t)
        {
            D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"depth staging");context->CopyResource(staging.Get(),t.texture.Get());
            std::vector<float> result(Width*Height*2);
            for(UINT eye=0;eye<2;eye++) {D3D11_MAPPED_SUBRESOURCE map{};const UINT sub=D3D11CalcSubresource(0,eye,d.MipLevels);check(context->Map(staging.Get(),sub,D3D11_MAP_READ,0,&map),"depth readback");
                for(UINT y=0;y<Height;y++) std::memcpy(result.data()+eye*Width*Height+y*Width,static_cast<const std::byte*>(map.pData)+y*map.RowPitch,Width*sizeof(float));
                context->Unmap(staging.Get(),sub);}
            return result;
        }
        std::vector<V> read(Texture& t)
        {
            D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"staging");context->CopyResource(staging.Get(),t.texture.Get());std::vector<V> result(Width*Height*2);
            for(UINT eye=0;eye<2;eye++) {D3D11_MAPPED_SUBRESOURCE map{};const UINT sub=D3D11CalcSubresource(0,eye,d.MipLevels);check(context->Map(staging.Get(),sub,D3D11_MAP_READ,0,&map),"readback");
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
                context->Unmap(staging.Get(),sub);}
            return result;
        }
        void dispatch(Pass pass,std::initializer_list<ID3D11ShaderResourceView*> inputs,std::initializer_list<ID3D11UnorderedAccessView*> outputs,UINT width=Width,UINT height=Height)
        {
            std::array<ID3D11ShaderResourceView*,8> views{};std::copy(inputs.begin(),inputs.end(),views.begin());
            std::array<ID3D11UnorderedAccessView*,4> targets{};std::copy(outputs.begin(),outputs.end(),targets.begin());
            const std::array<ID3D11ShaderResourceView*,8> noInputs{};const std::array<ID3D11UnorderedAccessView*,4> none{};
            context->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0);context->CSSetShader(shader[pass].Get(),nullptr,0);
            context->CSSetShaderResources(0,8,views.data());context->CSSetUnorderedAccessViews(0,4,targets.data(),nullptr);
            context->Dispatch((width+7)/8,(height+7)/8,2);
            context->CSSetUnorderedAccessViews(0,4,none.data(),nullptr);context->CSSetShaderResources(0,8,noInputs.data());
        }
        void mips(Pass pass,Texture& t) {dispatch(pass,{t.top.Get()},{t.mips[0].Get(),t.mips[1].Get(),t.mips[2].Get(),t.mips[3].Get()},Width/2,Height/2);}
    };
    // One trace frame through the real pass chain: position and radiance mips,
    // reprojection of the given history, trace and blur.
    struct Chain
    {
        Fixture& f;
        Texture position,oldPosition,normal,radiance,radianceMips,luma,chroma,specular,oldLuma,oldChroma,oldSpecular,
            remappedLuma,remappedChroma,remappedSpecular,tracedLuma,tracedChroma;
        explicit Chain(Fixture& fixture):f(fixture),position(f.texture(Mips,DXGI_FORMAT_R32_FLOAT)),oldPosition(f.texture(1,DXGI_FORMAT_R32_FLOAT)),normal(f.texture()),radiance(f.texture()),
            radianceMips(f.texture(Mips)),luma(f.texture()),chroma(f.texture()),specular(f.texture()),oldLuma(f.texture()),oldChroma(f.texture()),
            oldSpecular(f.texture()),remappedLuma(f.texture()),remappedChroma(f.texture()),remappedSpecular(f.texture()),tracedLuma(f.texture()),tracedChroma(f.texture()) {}
        void reproject()
        {
            f.mips(PrefilterPosition,position);
            f.dispatch(Reproject,{position.view.Get(),oldPosition.view.Get(),radiance.view.Get(),oldLuma.view.Get(),oldChroma.view.Get(),oldSpecular.view.Get()},
                {radianceMips.output.Get(),remappedLuma.output.Get(),remappedChroma.output.Get(),remappedSpecular.output.Get()});
            f.mips(PrefilterRadiance,radianceMips);
        }
        void trace()
        {
            f.dispatch(Trace,{position.view.Get(),normal.view.Get(),radianceMips.view.Get(),f.noise.Get(),remappedLuma.view.Get(),remappedChroma.view.Get(),remappedSpecular.view.Get()},
                {tracedLuma.output.Get(),tracedChroma.output.Get(),specular.output.Get()});
            f.dispatch(Blur,{position.view.Get(),normal.view.Get(),tracedLuma.view.Get(),tracedChroma.view.Get()},{luma.output.Get(),chroma.output.Get()});
        }
        void frame() {reproject();trace();}
    };
    // SH with only the constant band: ZH3 irradiance is then L0 * sqrt(pi)/2.
    constexpr float kConstantBand=.9f/.88622692545275801f;
    void checkAmbientConsumer(Fixture& f,const std::filesystem::path& root)
    {
        ComPtr<ID3DBlob> vertex,pixel,error;
        const char* vsSource="cbuffer EyeData:register(b0){uint Eye;};struct Out{float4 p:SV_POSITION;nointerpolation uint eye:EYEINDEX;};Out main(uint i:SV_VertexID){Out o;float2 uv=float2((i<<1)&2,i&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.eye=Eye;return o;}";
        check(D3DCompile(vsSource,std::strlen(vsSource),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error),"ambient fixture vertex");
        auto path=root/L"package/Shaders/Community/Skylighting/SkylightingAmbientTransform.hlsl";
        const auto compiled=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"PSMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error);
        if(error) std::cerr.write(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());check(compiled,"ambient fixture pixel");
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
        // Left eye bounces orange light, right eye blue; both carry AO 0.5.
        auto luma=f.texture(),chroma=f.texture();std::vector<V> lumaPixels(Width*Height*2,V{kConstantBand,0,0,0}),chromaPixels(lumaPixels.size());
        for(UINT i=0;i<chromaPixels.size();i++) chromaPixels[i]=i<Width*Height?V{.45f,0,.5f,4}:V{-.45f,0,.5f,4};
        f.upload(luma,lumaPixels);f.upload(chroma,chromaPixels);
        auto* view=luma.view.Get();f.context->PSSetShaderResources(57,1,&view);view=chroma.view.Get();f.context->PSSetShaderResources(65,1,&view);
        f.context->OMSetRenderTargets(4,outputs.data(),nullptr);
        for(UINT side=0;side<2;side++) {
            std::array<UINT,4> data{side,0,0,0};f.context->UpdateSubresource(eye.Get(),0,nullptr,data.data(),0,0);
            D3D11_VIEWPORT viewport{float(side*Width*2),0,Width*2,Height*2,0,1};f.context->RSSetViewports(1,&viewport);f.context->Draw(3,0);
        }
        f.context->OMSetRenderTargets(0,nullptr,nullptr);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(f.device->CreateTexture2D(&td,nullptr,&staging),"ambient readback");
        // The composed native shader must apply exactly these transform outputs.
        std::vector<V> transformVisibility(td.Width*td.Height),transformSpecular(td.Width*td.Height);
        for(UINT target=0;target<4;target++) {
            f.context->CopyResource(staging.Get(),textures[target].Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(f.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"ambient map");
            for(UINT y=0;y<td.Height;y++) for(UINT x=0;x<td.Width;x++) {
                const auto value=reinterpret_cast<const V*>(static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch)[x];
                if(target==0) require(std::abs(value.x-std::sqrt(.5508f))<.002f && std::abs(value.y-std::sqrt(.6809625f))<.002f && std::abs(value.z-std::sqrt(.811125f))<.002f && value.w==1,"ambient multibounce AO root or alpha changed");
                if(target==1) {require(value.x>0 && value.x<1 && value.x==value.y && value.y==value.z && value.w==1,"specular occlusion missing or out of range");transformVisibility[y*td.Width+x]=value;}
                if(target==2) require(value.x==0 && value.y==0 && value.z==0 && value.w==0,"ambient pass added the bounce the composite owns");
                if(target==3) {require(std::isfinite(value.x) && value.x>=0 && value.y>=0 && value.z>=0 && value.w==0,"specular bounce not finite");transformSpecular[y*td.Width+x]=value;}
            }
            f.context->Unmap(staging.Get(),0);
        }
        // Compare an actual native ambient variant with its composed DXBC.
        // Shader-object creation alone does not prove a bytecode splice has
        // the correct operand count or scales the intended output.
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
        ComPtr<ID3D11ShaderResourceView> parameters;f.context->PSGetShaderResources(59,1,&parameters);
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
                    if(!phase) {baseline[target][y*td.Width+x]=value;continue;}
                    auto expected=baseline[target][y*td.Width+x];
                    if(target==1 && phase==1) {
                        // Native lobe times specular occlusion, plus the bounce.
                        const auto& o=transformVisibility[y*td.Width+x];const auto& s=transformSpecular[y*td.Width+x];
                        expected.x=expected.x*o.x+s.x;expected.y=expected.y*o.y+s.y;expected.z=expected.z*o.z+s.z;
                    }
                    if(target==0 && phase==1) {expected.x*=std::sqrt(.5508f);expected.y*=std::sqrt(.6809625f);expected.z*=std::sqrt(.811125f);}
                    require(std::isfinite(value.x)&&std::abs(value.x-expected.x)<.002f && std::abs(value.y-expected.y)<.002f && std::abs(value.z-expected.z)<.002f && value.w==expected.w,"native ambient composition changed the wrong lobe, irradiance or alpha");
                }
                f.context->Unmap(staging.Get(),0);
            }
        }
        // The composite-time pass scales a deferred accumulator by the root of
        // the multibounce occlusion and adds the bounce once, per eye: the
        // constant band evaluates to 0.9 luma, tinted by each eye's chroma.
        {
            auto composePath=root/L"package/Shaders/Community/ScreenSpaceGI/ComposeOcclusion.hlsl";
            ComPtr<ID3DBlob> composeVertex,composePixel,composeScale;
            check(D3DCompileFromFile(composePath.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"VSMain","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&composeVertex,&error),"compose vertex");
            check(D3DCompileFromFile(composePath.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"PSMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&composePixel,&error),"compose pixel");
            check(D3DCompileFromFile(composePath.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"PSScale","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&composeScale,&error),"compose scale pixel");
            ComPtr<ID3D11VertexShader> cvs;ComPtr<ID3D11PixelShader> cps,scaleShader;
            check(f.device->CreateVertexShader(composeVertex->GetBufferPointer(),composeVertex->GetBufferSize(),nullptr,&cvs),"compose VS");
            check(f.device->CreatePixelShader(composePixel->GetBufferPointer(),composePixel->GetBufferSize(),nullptr,&cps),"compose PS");
            check(f.device->CreatePixelShader(composeScale->GetBufferPointer(),composeScale->GetBufferSize(),nullptr,&scaleShader),"compose scale PS");
            D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=TRUE;
            blend.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;blend.RenderTarget[0].DestBlend=D3D11_BLEND_SRC1_COLOR;blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ZERO;blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ONE;blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D11BlendState> blendState;check(f.device->CreateBlendState(&blend,&blendState),"compose blend");
            auto* parameterView=parameters.Get();f.context->PSSetShaderResources(59,1,&parameterView);
            const float one[4]{1,1,1,1};f.context->ClearRenderTargetView(targets[0].Get(),one);f.context->ClearRenderTargetView(targets[1].Get(),one);
            f.context->VSSetShader(cvs.Get(),nullptr,0);f.context->OMSetBlendState(blendState.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT full{0,0,Width*4,Height*2,0,1};f.context->RSSetViewports(1,&full);
            // Deferred accumulator through PSMain, tiled accumulator through PSScale.
            for(UINT i=0;i<2;i++) {f.context->PSSetShader(i?scaleShader.Get():cps.Get(),nullptr,0);auto* accumulator=targets[i].Get();f.context->OMSetRenderTargets(1,&accumulator,nullptr);f.context->Draw(3,0);}
            f.context->OMSetRenderTargets(0,nullptr,nullptr);f.context->OMSetBlendState(nullptr,nullptr,0xFFFFFFFFu);
            for(UINT i=0;i<2;i++) {
                f.context->CopyResource(staging.Get(),textures[i].Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(f.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"compose map");
                for(UINT y=0;y<td.Height;y++) for(UINT x=0;x<td.Width;x++) {
                    const auto value=reinterpret_cast<const V*>(static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch)[x];
                    const float co=x<Width*2?.45f:-.45f,bounce=i?0.0f:1.0f;
                    const V expected{std::sqrt(.5508f)+bounce*(.9f+co)/3,std::sqrt(.6809625f)+bounce*.9f/3,std::sqrt(.811125f)+bounce*(.9f-co)/3,1};
                    require(std::abs(value.x-expected.x)<.003f && std::abs(value.y-expected.y)<.003f && std::abs(value.z-expected.z)<.003f && value.w==1,
                        i?"tiled accumulator lost the occlusion root or received the bounce":"composite occlusion root, SH bounce, eye or alpha changed");
                }
                f.context->Unmap(staging.Get(),0);
            }
        }
        D3D11_VIEWPORT viewport{0,0,Width*4,Height*2,0,1};f.context->RSSetViewports(1,&viewport);
    }
    void run(const std::filesystem::path& root,D3D_DRIVER_TYPE driver)
    {
        Fixture f(root,driver);
        for(const auto& contract:csah_skylighting_ambient_contracts) {
            ComPtr<ID3D11PixelShader> candidate;
            check(f.device->CreatePixelShader(contract.replacementBytecode,contract.replacementBytecodeLength,nullptr,&candidate),"native ambient shader composition rejected by D3D11");
        }
        Chain c(f);
        // Geometry is signed inverse view depth; the shaders rebuild each
        // position along the fixture's eye-local view ray.
        std::vector<float> positions(Width*Height*2,1.0f/100);
        std::vector<V> normals(positions.size(),{0,0,-1,.5f}),light(positions.size());
        for(UINT eye=0;eye<2;eye++) for(UINT y=0;y<Height;y++) for(UINT x=0;x<Width;x++) {
            const auto i=eye*Width*Height+y*Width+x;
            light[i]=eye?V{0,0,4,1}:V{4,0,0,1};
        }
        f.upload(c.position,positions);f.upload(c.oldPosition,positions);f.upload(c.normal,normals);f.upload(c.radiance,light);
        // Textures created without initial data are undefined in D3D11.
        const std::vector<V> empty(positions.size());f.upload(c.oldLuma,empty);f.upload(c.oldChroma,empty);f.upload(c.oldSpecular,empty);
        c.frame();auto flatLuma=f.read(c.luma),flatChroma=f.read(c.chroma);
        for(UINT i=0;i<flatLuma.size();i++) require(flatChroma[i].z>.9999f && std::abs(flatLuma[i].x)<1e-5f && std::abs(flatChroma[i].x)<1e-5f,"flat plane must not self-occlude or bounce");
        for(UINT eye=0;eye<2;eye++) for(UINT y=0;y<Height;y++) for(UINT x=19;x<Width;x++) {
            const auto i=eye*Width*Height+y*Width+x;positions[i]=1.0f/70;normals[i]={-1,0,0,.5f};
        }
        f.upload(c.position,positions);f.upload(c.oldPosition,positions);f.upload(c.normal,normals);
        c.frame();auto cornerLuma=f.read(c.luma),cornerChroma=f.read(c.chroma);float minAO=1,maxGI=0;
        for(UINT i=0;i<cornerLuma.size();i++) {
            const auto l=cornerLuma[i];const auto k=cornerChroma[i];
            require(std::isfinite(l.x)&&std::isfinite(k.x)&&k.z>=0&&k.z<=1,"finite bounded corner");
            // Red source light has positive Co, blue negative.
            if(i<Width*Height) require(k.x>=-1e-5f,"right-eye light leaked left");
            else require(k.x<=1e-5f,"left-eye light leaked right");
            minAO=(std::min)(minAO,k.z);maxGI=(std::max)(maxGI,l.x);
        }
        require(minAO<.95f,"corner occlusion missing");require(maxGI>.001f,"visible indirect light missing");
        f.settings.history=0;c.frame();auto reset=f.read(c.luma);
        for(const auto v:reset) require(v.x==0&&v.y==0&&v.z==0&&v.w==0,"reset used stale radiance");
        f.settings.history=1;f.settings.gi=0;c.frame();auto aoOnly=f.read(c.luma),aoOnlyChroma=f.read(c.chroma);
        for(UINT i=0;i<aoOnly.size();i++) require(aoOnly[i].x==0&&aoOnlyChroma[i].x==0&&aoOnlyChroma[i].y==0,"AO-only mode emits GI");
        f.settings.gi=1;
        // History survives an unmoved surface and is dropped on disocclusion.
        f.settings.temporal=1;
        std::vector<V> staleLuma(positions.size(),{100,0,0,0}),staleChroma(positions.size(),{1,0,.25f,8});
        f.upload(c.oldLuma,staleLuma);f.upload(c.oldChroma,staleChroma);f.upload(c.oldSpecular,std::vector<V>(positions.size()));
        c.reproject();auto kept=f.read(c.remappedLuma),keptChroma=f.read(c.remappedChroma);
        for(UINT i=0;i<kept.size();i++) require(std::abs(kept[i].x-100)<.5f && std::abs(keptChroma[i].w-9)<1e-3f,"static history was not retained or counted");
        auto moved=positions;for(auto& v:moved) v=1/(1/v+10000);f.upload(c.oldPosition,moved);
        c.reproject();auto rejected=f.read(c.remappedLuma),rejectedChroma=f.read(c.remappedChroma);
        for(UINT i=0;i<rejected.size();i++) require(rejected[i].x==0 && rejectedChroma[i].w==1,"disocclusion retained history");
        // A history outlier is clamped to its neighbourhood while young.
        f.upload(c.oldPosition,positions);staleChroma.assign(positions.size(),{0,0,1,1});
        const UINT outlier=5*Width+5;staleLuma.assign(positions.size(),{.01f,0,0,0});staleLuma[outlier]={1e4f,0,0,0};
        f.upload(c.oldLuma,staleLuma);f.upload(c.oldChroma,staleChroma);
        f.settings.blur=0;c.frame();auto clamped=f.read(c.luma);
        require(clamped[outlier].x<100,"temporal neighbourhood clamp failed");
        auto unblurred=f.read(c.tracedLuma);
        for(UINT i=0;i<unblurred.size();i++) require(unblurred[i].x==clamped[i].x,"disabled blur changed the trace");
        f.settings.blur=1;f.settings.temporal=0;
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
        auto* sentinel=c.radiance.view.Get();f.context->PSSetShaderResources(57,1,&sentinel);
        auto& runtime=csah::screen_space_gi::Runtime::get();runtime.onDeviceCreated(f.device.Get(),f.context.Get());
        csah::screen_space_gi::Settings runtimeSettings;runtimeSettings.enabled=true;runtime.applySettings(runtimeSettings);runtime.beginFrame();
        f.context->CSSetShader(f.shader[Capture].Get(),nullptr,0);
        {
            csah::pbr::FrameData material{};material.enabled=1;material.legacyMaterials=1;material.environmentFresnel=1;material.roughnessMultiplier=1.7f;material.baseF0Multiplier=.45f;
            auto scope=runtime.scopeAmbient(f.context.Get(),true,nullptr,material,nativeViews[3].Get());
            ComPtr<ID3D11ShaderResourceView> parameters;f.context->PSGetShaderResources(59,1,&parameters);
            require(parameters,"runtime failed to publish GI parameters");
            D3D11_SHADER_RESOURCE_VIEW_DESC pd{};parameters->GetDesc(&pd);require(pd.Buffer.NumElements==12,"GI lost PBR parameter rows");
            ComPtr<ID3D11Resource> data;parameters->GetResource(&data);ComPtr<ID3D11Buffer> source;check(data.As(&source),"GI parameter buffer");
            D3D11_BUFFER_DESC copyDesc{};source->GetDesc(&copyDesc);copyDesc.BindFlags=0;copyDesc.Usage=D3D11_USAGE_STAGING;copyDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Buffer> copy;check(f.device->CreateBuffer(&copyDesc,nullptr,&copy),"GI parameter readback");f.context->CopyResource(copy.Get(),source.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};check(f.context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped),"GI parameter map");
            csah::pbr::FrameData transferred{};std::memcpy(&transferred,static_cast<const char*>(mapped.pData)+128,sizeof(transferred));f.context->Unmap(copy.Get(),0);
            require(transferred.enabled==1 && transferred.roughnessMultiplier==1.7f && transferred.baseF0Multiplier==.45f,"GI did not transport live PBR settings");
            ComPtr<ID3D11ShaderResourceView> surface;f.context->PSGetShaderResources(64,1,&surface);require(surface.Get()==nativeViews[3].Get(),"GI surface classification missing");
            ComPtr<ID3D11ShaderResourceView> selected,selectedChroma;f.context->PSGetShaderResources(57,1,&selected);f.context->PSGetShaderResources(65,1,&selectedChroma);
            require(selected && selected.Get()!=sentinel && selectedChroma,"runtime failed to publish ambient output");
            Texture generated,generatedChroma;ComPtr<ID3D11Resource> resource;selected->GetResource(&resource);check(resource.As(&generated.texture),"generated texture");
            selectedChroma->GetResource(resource.ReleaseAndGetAddressOf());check(resource.As(&generatedChroma.texture),"generated chroma");
            auto pixels=f.read(generated),chromaPixels=f.read(generatedChroma);
            for(UINT i=0;i<pixels.size();i++) require(pixels[i].x==0 && chromaPixels[i].z>.99f,"first frame must have neutral plane lighting");
            ComPtr<ID3D11ComputeShader> restored;f.context->CSGetShader(&restored,nullptr,nullptr);require(restored.Get()==f.shader[Capture].Get(),"compute shader was not restored");
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
        auto position=f.texture(1,DXGI_FORMAT_R32_FLOAT),normal=f.texture();
        for(const auto rect:std::array<D3D11_VIEWPORT,4>{{{0,0,84,22,0,1},{0,0,74,18,0,1},{0,0,64,16,0,1},{4,2,42,10,0,1}}}) {
            f.settings.x=rect.TopLeftX;f.settings.y=rect.TopLeftY;f.settings.vw=rect.Width;f.settings.vh=rect.Height;
            std::vector<float> depths(Width*4*Height*2,1);
            for(UINT y=UINT(rect.TopLeftY);y<UINT(rect.TopLeftY+rect.Height);y++)for(UINT x=UINT(rect.TopLeftX);x<UINT(rect.TopLeftX+rect.Width);x++)
                depths[y*Width*4+x]=(x-UINT(rect.TopLeftX))<UINT(rect.Width/2)?.5f:.75f;
            f.context->UpdateSubresource(native[2].Get(),0,nullptr,depths.data(),Width*4*sizeof(float),0);
            f.dispatch(Prepare,{nativeViews[0].Get(),nativeViews[2].Get(),nativeViews[1].Get()},{position.output.Get(),normal.output.Get()});
            const auto prepared=f.readDepth(position);
            // Each eye's half holds its own native depth: a world surface at
            // view depth 100 / projected, stored as its positive inverse.
            for(UINT eyeIndex=0;eyeIndex<2;eyeIndex++)for(UINT y=0;y<Height;y++)for(UINT x=0;x<Width;x++) {
                const auto p=prepared[(eyeIndex*Height+y)*Width+x];const float projected=(eyeIndex?.75f:.5f)*1.01f-.01f;
                require(p>0 && std::abs(1/p-100/projected)<.001f,"reduced GI reconstruction sampled the wrong eye, depth or domain");
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
        require(!settings.enabled && settings.indirectLight && !settings.specular && settings.aoPower==4 && settings.resolutionMode==1,"upstream defaults");
        settings.aoRadius=std::numeric_limits<float>::quiet_NaN();settings.strength=std::numeric_limits<float>::infinity();settings.thickness=-10;
        settings.slices=40;settings.fadeStart=60000;settings.fadeEnd=1000;
        settings=csah::screen_space_gi::sanitize(settings);
        require(settings.aoRadius==256 && settings.strength==1 && settings.thickness==0 && settings.slices==10 && settings.fadeEnd>settings.fadeStart,"settings bounds");
        run(std::filesystem::path(argv[1]),D3D_DRIVER_TYPE_WARP);run(std::filesystem::path(argv[1]),D3D_DRIVER_TYPE_HARDWARE);return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
