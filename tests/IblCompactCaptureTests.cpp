#include "Features/ibl/IblCompactCapture.h"
#include "Features/ibl/IblReflectionFreeCapture.h"
#include "Features/ibl/IblEnvironmentUpdater.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>
using Microsoft::WRL::ComPtr;
struct V { float x,y,z,w; };
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void hr(HRESULT v, const char* message) { check(SUCCEEDED(v), message); }
std::vector<char> read(const std::filesystem::path& p) { std::ifstream s(p, std::ios::binary); check(s.good(),"read"); return {std::istreambuf_iterator<char>(s),{}}; }
ComPtr<ID3DBlob> compile(const char* code, const char* entry, const char* model, const D3D_SHADER_MACRO* macros=nullptr, const char* sourceName=nullptr) {
    ComPtr<ID3DBlob> bytes,errors;
    auto result=D3DCompile(code,strlen(code),sourceName,macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,model,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytes,&errors);
    if (FAILED(result) && errors) std::cerr << static_cast<char*>(errors->GetBufferPointer());
    hr(result,"compile"); return bytes;
}
int main(int argc, char** argv) {
 try {
    check(argc>=2,"root argument"); const std::filesystem::path root=argv[1];
    constexpr UINT w=32,h=16,count=w*h,extent=16;
    ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> c;
    hr(D3D11CreateDevice(nullptr,argc>2?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,
       D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c),"device");
    const char* vsCode=R"(
    cbuffer TestCoverage : register(b13) { float4 CoverageRules; };
    struct Out { float4 p: SV_Position;
    #if EXTRA
    float3 uv: TEXCOORD0;
    #endif
    nointerpolation uint eye: EYEINDEX;
    float cull: SV_CullDistance0;
    float clip: SV_ClipDistance0; };
    Out main(uint v:SV_VertexID) {
        const float2 corners[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
        Out o; uint eye=v/6; float2 q=corners[v%6];
        o.p=float4(q.x+eye-1,1-2*q.y,0.5,1); o.eye=eye;
        o.clip=o.p.x+CoverageRules.x;o.cull=eye?CoverageRules.y:1.0f;
    #if EXTRA
        o.uv=float3(q,0);
    #endif
        return o;
    })";
    std::array<ComPtr<ID3D11VertexShader>,2> vs;
    for (UINT i=0;i<2;++i) {
        D3D_SHADER_MACRO defs[]={{"EXTRA",i?"1":"0"},{nullptr,nullptr}};
        auto code=compile(vsCode,"main","vs_5_0",defs);
        hr(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs[i]),"VS");
    }
    std::array<ComPtr<ID3D11Buffer>,13> cb;
    std::array<ID3D11Buffer*,13> rawCb{};
    std::array<std::array<V,85>,13> values{};
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(values[0]);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    for(UINT i=0;i<13;++i) { hr(d->CreateBuffer(&bd,nullptr,&cb[i]),"CB");rawCb[i]=cb[i].Get(); }
    std::array<ComPtr<ID3D11Texture2D>,16> tex;
    std::array<ComPtr<ID3D11ShaderResourceView>,16> srv;
    std::array<ID3D11ShaderResourceView*,16> rawSrv{};
    ComPtr<ID3D11DepthStencilView> sourceDepthView;
    D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    for(UINT i=0;i<16;++i) {
        auto desc=td;
        if (i==4 || i==9) desc.MipLevels=3;
        if (i==7) {desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.BindFlags|=D3D11_BIND_DEPTH_STENCIL;}
        if(i==8){desc.Width=desc.Height=1;desc.ArraySize=252;desc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;}
        hr(d->CreateTexture2D(&desc,nullptr,&tex[i]),"texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;
        if (i==7) {
            view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            D3D11_DEPTH_STENCIL_VIEW_DESC sourceDS{};
            sourceDS.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;sourceDS.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            hr(d->CreateDepthStencilView(tex[i].Get(),&sourceDS,&sourceDepthView),"native depth DSV");
        }
        if(i==8){view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;view.TextureCubeArray.MipLevels=1;view.TextureCubeArray.NumCubes=42;}
        else{view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=desc.MipLevels;}
        hr(d->CreateShaderResourceView(tex[i].Get(),&view,&srv[i]),"SRV");rawSrv[i]=srv[i].Get();
    }
    V zero{};for(UINT i=0;i<252;++i)c->UpdateSubresource(tex[8].Get(),i,nullptr,&zero,sizeof(V),0);
    std::array<std::vector<char>,41> compactBytecodes;
    std::array<csah::ibl::CompactCapture::Bytecode,41> compactDefinitions;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(root/"package/Shaders/Community/VerifiedIBLMaterial"))
        if (entry.path().extension()==".dxbc") paths.push_back(entry.path());
    std::sort(paths.begin(),paths.end());
    check(paths.size()==41,"native family coverage");
    for (size_t i=0;i<41;++i) {
        compactBytecodes[i]=read(root/"package/Shaders/Community/IBLCompactCapture"/paths[i].filename());
        compactDefinitions[i]={compactBytecodes[i].data(),compactBytecodes[i].size()};
    }
    csah::ibl::CompactCapture capture;
    check(capture.initialize(d.Get(),compactDefinitions,extent),"compact initialization");
    const auto updateCode=read(root/"package/Shaders/Community/IBL/UpdateEnvironmentCS.dxbc");
    const auto compactUpdateCode=read(root/"package/Shaders/Community/IBL/UpdateCompactEnvironmentCS.dxbc");
    const auto filterCode=read(root/"package/Shaders/Community/IBL/FilterEnvironmentCS.dxbc");
    const auto reduceCode=read(root/"package/Shaders/Community/IBL/ReduceEnvironmentCS.dxbc");
    std::array<csah::ibl::EnvironmentUpdater,2> updaters;
    std::array<csah::ibl::EnvironmentProvider,2> providers;
    for (UINT i=0;i<2;++i) {
        check(providers[i].initialize(d.Get(),extent),"provider initialization");
        check(updaters[i].initialize(d.Get(),updateCode.data(),updateCode.size(),filterCode.data(),filterCode.size(),
            reduceCode.data(),reduceCode.size(),extent,compactUpdateCode.data(),compactUpdateCode.size()),"updater initialization");
    }
    const auto oraclePath=std::filesystem::absolute(root/"package/Shaders/Community/IBL/CaptureRadianceCS.hlsl");
    const auto oracleBytes=read(oraclePath);
    std::string oracleSource(oracleBytes.begin(),oracleBytes.end());
    auto replace=[&](const std::string& from,const std::string& to) {
        auto index=oracleSource.find(from);check(index!=std::string::npos,"oracle substitution missing");
        oracleSource.replace(index,from.size(),to);
    };
    replace("register(t127)","register(t30)");
    replace("NativeComposite.SampleLevel(NativeMarker, float2(coordinate) + 0.5f, (float)eye).rgb",
        "NativeComposite.Load(int3(coordinate, 0)).rgb");
    const auto oracleCode=compile(oracleSource.c_str(),"main","cs_5_0",nullptr,oraclePath.string().c_str());
    ComPtr<ID3D11ComputeShader> oracle;
    hr(d->CreateComputeShader(oracleCode->GetBufferPointer(),oracleCode->GetBufferSize(),nullptr,&oracle),"oracle CS");
    const auto coverageCode=read(root/"package/Shaders/Community/IBL/CaptureCoveragePS.dxbc");
    ComPtr<ID3D11PixelShader> coveragePS;
    hr(d->CreatePixelShader(coverageCode.data(),coverageCode.size(),nullptr,&coveragePS),"coverage PS");
    ComPtr<ID3D11Texture2D> target,oracleTexture,readback,compactReadback;
    td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    hr(d->CreateTexture2D(&td,nullptr,&target),"target");
    ComPtr<ID3D11RenderTargetView> rt;hr(d->CreateRenderTargetView(target.Get(),nullptr,&rt),"RTV");
    ComPtr<ID3D11ShaderResourceView> nativeRadiance;hr(d->CreateShaderResourceView(target.Get(),nullptr,&nativeRadiance),"reference radiance");
    csah::ibl::ReflectionFreeCaptureResources coverage;
    check(coverage.initialize(d.Get())&&coverage.prepareScratch(rt.Get(),true),"coverage resources");
    check(coverage.scratchDescription().Format==DXGI_FORMAT_R8_UNORM,"coverage must occupy one byte per pixel");
    td.Width=td.Height=extent;td.ArraySize=12;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;hr(d->CreateTexture2D(&td,nullptr,&oracleTexture),"oracle target");
    ComPtr<ID3D11UnorderedAccessView> oracleOutput;hr(d->CreateUnorderedAccessView(oracleTexture.Get(),nullptr,&oracleOutput),"oracle UAV");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    hr(d->CreateTexture2D(&td,nullptr,&readback),"oracle staging");
    hr(d->CreateTexture2D(&td,nullptr,&compactReadback),"compact staging");
    ComPtr<ID3D11Buffer> captureConstants;bd.ByteWidth=16;
    ComPtr<ID3D11Buffer> coverageRules;
    hr(d->CreateBuffer(&bd,nullptr,&coverageRules),"geometry coverage rules");
    auto* geometryConstants=coverageRules.Get();c->VSSetConstantBuffers(13,1,&geometryConstants);
    const std::array<UINT,4> parameters{w,h,extent,0};D3D11_SUBRESOURCE_DATA constantsData{parameters.data(),0,0};
    hr(d->CreateBuffer(&bd,&constantsData,&captureConstants),"capture constants");
    auto* captureBuffer=captureConstants.Get();c->CSSetConstantBuffers(13,1,&captureBuffer);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;hr(d->CreateRasterizerState(&raster,&rs),"raster");
    c->RSSetState(rs.Get());D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};c->RSSetViewports(1,&viewport);
    D3D11_TEXTURE2D_DESC depthDescription{};
    depthDescription.Width=w;depthDescription.Height=h;
    depthDescription.MipLevels=depthDescription.ArraySize=depthDescription.SampleDesc.Count=1;
    depthDescription.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;depthDescription.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;hr(d->CreateTexture2D(&depthDescription,nullptr,&depthTexture),"coverage depth");
    ComPtr<ID3D11DepthStencilView> depthView;hr(d->CreateDepthStencilView(depthTexture.Get(),nullptr,&depthView),"coverage DSV");
    D3D11_DEPTH_STENCIL_DESC depthState{};
    depthState.DepthEnable=TRUE;depthState.DepthFunc=D3D11_COMPARISON_LESS;
    depthState.StencilEnable=TRUE;depthState.StencilReadMask=255;
    depthState.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_EQUAL};
    depthState.BackFace=depthState.FrontFace;
    ComPtr<ID3D11DepthStencilState> depthTest;hr(d->CreateDepthStencilState(&depthState,&depthTest),"coverage depth/stencil rules");
    c->OMSetDepthStencilState(depthTest.Get(),1);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ComPtr<ID3D11SamplerState> sampler;D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    hr(d->CreateSamplerState(&sd,&sampler),"sampler");std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());
    c->PSSetSamplers(0,16,samplers.data());c->CSSetSamplers(0,16,samplers.data());
    c->PSSetConstantBuffers(0,13,rawCb.data());c->CSSetConstantBuffers(0,13,rawCb.data());
    c->PSSetShaderResources(0,16,rawSrv.data());c->CSSetShaderResources(0,16,rawSrv.data());
    size_t tested=0,nonzero=0,litPublications=0;double maxError=0;
    for (size_t shaderIndex=0;shaderIndex<paths.size();++shaderIndex) {
        const auto& path=paths[shaderIndex];
        const auto native=read(path);
        ComPtr<ID3D11PixelShader> ps;hr(d->CreatePixelShader(native.data(),native.size(),nullptr,&ps),"native PS");
        const auto integratedBytes=read(root/"package/Shaders/Community/IBLMaterial"/path.filename());
        ComPtr<ID3D11PixelShader> integrated;hr(d->CreatePixelShader(integratedBytes.data(),integratedBytes.size(),nullptr,&integrated),"integrated PS");
        ComPtr<ID3D11ShaderReflection> reflection;hr(D3DReflect(native.data(),native.size(),IID_PPV_ARGS(&reflection)),"reflection");
        D3D11_SHADER_DESC shaderDesc{};reflection->GetDesc(&shaderDesc);bool extra=false;
        for(UINT i=0;i<shaderDesc.InputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC param{};reflection->GetInputParameterDesc(i,&param);if(strcmp(param.SemanticName,"TEXCOORD")==0)extra=true;}
        c->VSSetShader(vs[extra].Get(),nullptr,0);
        for(UINT scenario=0;scenario<16;++scenario) {
            const V rules{scenario%4==2?0.5F:2.0F,scenario%4==3?-1.0F:1.0F,0,0};
            c->UpdateSubresource(coverageRules.Get(),0,nullptr,&rules,0,0);
            for(UINT b=0;b<13;++b)for(UINT row=0;row<85;++row) values[b][row]={0.3F,0.5F,0.1F,0.8F};
            values[5]={}; values[9]={};
            const float uvScale=(scenario&1)?2.0F:1.0F;
            values[2][0]={uvScale/w,uvScale/h,0,0};values[8][0]={1,0,0,0};
            // Matrices are HLSL rows. Offset both eye origins distinctly.
            for(UINT base: {0u,4u,8u,12u,16u,20u,24u,28u,32u,36u,40u,44u})
                for(UINT row=0;row<4;++row){values[12][base+row]={};reinterpret_cast<float*>(&values[12][base+row])[row]=1;}
            for(UINT eye=0;eye<2;++eye) {
                auto* p=&values[12][4+4*eye];p[0]={1.3F,0,eye?0.1F:-0.1F,0};p[1]={0,1.1F,0.02F,0};p[2]={0,0,1.001F,-1.001F};p[3]={0,0,1,0};
                for(UINT base:{32u,40u}) {auto* inv=&values[12][base+4*eye];inv[0]={1/p[0].x,0,0,-p[0].z/p[0].x};inv[1]={0,1/p[1].y,0,-p[1].z/p[1].y};inv[2]={0,0,0,1};inv[3]={0,0,1/p[2].w,-p[2].z/p[2].w};}
                values[12][59+eye]={float(100+eye*6),20,35,0};
            }
            values[12][71]={0.005F,0,0,0.2F};values[12][76]={0.01F,0.02F,0,0};
            for(UINT b=0;b<13;++b)c->UpdateSubresource(cb[b].Get(),0,nullptr,values[b].data(),0,0);
            std::vector<V> pixels(count);
            for(UINT t=0;t<16;++t) {
                if(t==8)continue;
                if(t==7) {
                    c->ClearDepthStencilView(sourceDepthView.Get(),D3D11_CLEAR_DEPTH,
                        (scenario&4)?0.005F:((scenario&8)?0.999F:0.5F),0);
                    continue;
                }
                for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x) {
                    float f=float((x*7+y*3+scenario)%17)/17;
                    V value{0.15F+f*0.6F,0.25F+f*0.5F,0.3F+f*0.4F,0.5F};
                    if(t==1)value={0.5F,0.5F,-1,0.5F};
                    if(t==3)value={0.3F,0.35F,0.4F,(scenario%4==0)?2.0F/255:(scenario%4==1)?3.0F/255:0.7F};
                    if(t==4||t==5||t==6||t==11||t==12)value={1+f*10,0.5F+f*3,0.3F+f*5,1};
                    if(t==7){float depth=(scenario&4)?0.005F:((scenario&8)?0.999F:0.5F);value={depth,depth,depth,depth};}
                    if(t==14)value={};
                    pixels[y*w+x]=value;
                }
                c->UpdateSubresource(tex[t].Get(),0,nullptr,pixels.data(),w*sizeof(V),0);
                if(t==4 || t==9) for(UINT mip=1;mip<3;++mip) {
                    const UINT mw=w>>mip,mh=h>>mip;
                    std::vector<V> mipPixels(mw*mh);
                    for(UINT n=0;n<mw*mh;++n) {
                        const float f=float((n*3+scenario)%11)/11;
                        mipPixels[n]={0.2F+f,0.4F+f,0.1F+f,0.5F};
                    }
                    c->UpdateSubresource(tex[t].Get(),mip,nullptr,mipPixels.data(),mw*sizeof(V),0);
                }
            }
            D3D11_RECT scissor{(scenario&1)?3:0,(scenario&2)?2:0,(scenario&1)?LONG(w-2):LONG(w),LONG(h)};
            c->RSSetScissorRects(1,&scissor);
            auto* targetRaw=rt.Get();
            std::array<ID3D11ShaderResourceView*,2> nullViews{};c->CSSetShaderResources(30,2,nullViews.data());
            c->ClearDepthStencilView(depthView.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,
                scenario%5==0?0.25F:0.75F,scenario%7==0?0:1);
            c->OMSetRenderTargets(1,&targetRaw,depthView.Get());
            c->PSSetShader((scenario<8?ps:integrated).Get(),nullptr,0);
            const float clear[4]={-1,-1,-1,-1};c->ClearRenderTargetView(rt.Get(),clear);c->Draw(12,0);
            coverage.clearScratch(c.Get());
            {
                csah::ibl::ScopedReflectionFreeCapture transaction(c.Get(),coverage);
                check(transaction.active(),"coverage native-state transaction");
                c->PSSetShader(coveragePS.Get(),nullptr,0);c->Draw(12,0);
                check(transaction.restore(),"coverage state restoration");
            }
            c->OMSetRenderTargets(0,nullptr,nullptr);
            auto* referenceView=nativeRadiance.Get();c->CSSetShaderResources(30,1,&referenceView);
            auto* coverageView=coverage.scratchShaderResource();c->CSSetShaderResources(31,1,&coverageView);
            auto* oracleRaw=oracleOutput.Get();c->CSSetUnorderedAccessViews(0,1,&oracleRaw,nullptr);
            c->CSSetShader(oracle.Get(),nullptr,0);
            c->Dispatch((extent+7)/8,(extent+7)/8,12);
            check(capture.dispatch(c.Get(),coverageView,shaderIndex),"compact dispatch");
            ComPtr<ID3D11ComputeShader> restored;c->CSGetShader(&restored,nullptr,nullptr);
            check(restored.Get()==oracle.Get(),"compact shader state restoration");
            ComPtr<ID3D11Resource> compactTexture;capture.radiance()->GetResource(&compactTexture);
            c->CopyResource(compactReadback.Get(),compactTexture.Get());c->CopyResource(readback.Get(),oracleTexture.Get());
            for(UINT layer=0;layer<12;++layer) {
                D3D11_MAPPED_SUBRESOURCE a{},b{};
                hr(c->Map(readback.Get(),layer,D3D11_MAP_READ,0,&a),"map reference");
                hr(c->Map(compactReadback.Get(),layer,D3D11_MAP_READ,0,&b),"map compact");
                for(UINT y=0;y<extent;++y)for(UINT x=0;x<extent;++x) {
                    const auto* expected=reinterpret_cast<V*>(static_cast<char*>(a.pData)+y*a.RowPitch)+x;
                    const auto* actual=reinterpret_cast<V*>(static_cast<char*>(b.pData)+y*b.RowPitch)+x;
                    check(expected->w==actual->w,"native/compact validity mismatch");
                    if (expected->w==0) continue;
                    for(UINT channel=0;channel<3;++channel) {
                        float e=reinterpret_cast<const float*>(expected)[channel], v=reinterpret_cast<const float*>(actual)[channel];
                        if(e>0)++nonzero;
                        double error=std::abs(double(v)-e)/(1+std::abs(double(e)));maxError=std::max(maxError,error);
                        if(!std::isfinite(v)||error>1e-5) {
                            std::cerr<<path.filename()<<" scenario="<<scenario<<" layer="<<layer<<" pixel="<<x<<','<<y<<" native="<<e<<" compact="<<v<<'\n';
                            throw std::runtime_error("native/compact atlas mismatch");
                        }
                    }
                }
                c->Unmap(readback.Get(),layer);c->Unmap(compactReadback.Get(),layer);
            }
            ++tested;
            if (shaderIndex==9 && scenario>=8 && scenario<12) {
                check(updaters[0].dispatchUpdate(c.Get(),providers[0],nativeRadiance.Get(),rawSrv[7],rawCb[12],false),"full update");
                check(updaters[1].dispatchUpdate(c.Get(),providers[1],capture.radiance(),rawSrv[7],rawCb[12],false),"compact update");
                for(UINT i=0;i<2;++i) {
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                    while(updaters[i].snapshot().pending) {
                        check(updaters[i].consumeUpdate(c.Get(),providers[i])!=csah::ibl::EnvironmentUpdateConsumeResult::failed,"update validation");
                        if (updaters[i].snapshot().pending) {
                            check(std::chrono::steady_clock::now()<deadline,"update timeout");
                            c->Flush();std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
                for(UINT kind=0;kind<3;++kind) {
                    std::array<ComPtr<ID3D11Texture2D>,2> staged;
                    D3D11_TEXTURE2D_DESC publishedDescription{};
                    for(UINT i=0;i<2;++i) {
                        auto* view=kind==0?providers[i].publishedEnvironment():kind==1?providers[i].publishedValidity():providers[i].publishedPosition();
                        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
                        ComPtr<ID3D11Texture2D> texture;hr(resource.As(&texture),"published texture");texture->GetDesc(&publishedDescription);
                        auto staging=publishedDescription;staging.BindFlags=staging.MiscFlags=0;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                        hr(d->CreateTexture2D(&staging,nullptr,&staged[i]),"publication readback");c->CopyResource(staged[i].Get(),texture.Get());
                    }
                    for(UINT slice=0;slice<6;++slice)for(UINT mip=0;mip<publishedDescription.MipLevels;++mip) {
                        const UINT sub=D3D11CalcSubresource(mip,slice,publishedDescription.MipLevels);
                        D3D11_MAPPED_SUBRESOURCE left{},right{};hr(c->Map(staged[0].Get(),sub,D3D11_MAP_READ,0,&left),"left result");hr(c->Map(staged[1].Get(),sub,D3D11_MAP_READ,0,&right),"right result");
                        const auto rowBytes=std::max(1u,extent>>mip)*(kind==2?16:4);
                        for(UINT y=0;y<std::max(1u,extent>>mip);++y)
                            check(std::memcmp(static_cast<char*>(left.pData)+y*left.RowPitch,static_cast<char*>(right.pData)+y*right.RowPitch,rowBytes)==0,
                                "compact/full filtered publication differs");
                        c->Unmap(staged[0].Get(),sub);c->Unmap(staged[1].Get(),sub);
                    }
                }
                if (updaters[0].snapshot().coveredSamples>0) ++litPublications;
            }

        }
    }
    check(nonzero>1000,"parity fixture never observed lit world samples");
    check(litPublications>0,"full/compact publication comparison never observed world geometry");
    std::cout<<"Validated "<<tested<<" shader/scenario pairs, positive channels="<<nonzero<<", max relative error="<<maxError<<'\n';
    if (argc>2) {
        // Same render extent as the user's recorded session. This measures
        // only duplicate capture work, without presentation or game timing.
        constexpr UINT packedWidth=5376,packedHeight=2880,cubeExtent=128;
        check(capture.initialize(d.Get(),compactDefinitions,cubeExtent),"benchmark capture");
        D3D11_TEXTURE2D_DESC big{};
        big.Width=packedWidth;big.Height=packedHeight;big.MipLevels=big.ArraySize=big.SampleDesc.Count=1;
        big.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;big.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> bigTarget;hr(d->CreateTexture2D(&big,nullptr,&bigTarget),"benchmark target");
        ComPtr<ID3D11RenderTargetView> bigView;hr(d->CreateRenderTargetView(bigTarget.Get(),nullptr,&bigView),"benchmark RTV");
        check(coverage.prepareScratch(bigView.Get(),true),"benchmark coverage");
        big.Format=DXGI_FORMAT_R24G8_TYPELESS;big.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> bigDepth;hr(d->CreateTexture2D(&big,nullptr,&bigDepth),"benchmark depth");
        D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11DepthStencilView> bigDS;hr(d->CreateDepthStencilView(bigDepth.Get(),&ds,&bigDS),"benchmark DSV");
        c->ClearDepthStencilView(bigDS.Get(),D3D11_CLEAR_DEPTH,0.999F,0);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> bigDepthView;hr(d->CreateShaderResourceView(bigDepth.Get(),&sv,&bigDepthView),"benchmark depth SRV");
        rawSrv[7]=bigDepthView.Get();c->PSSetShaderResources(0,16,rawSrv.data());
        values[2][0]={1.0F/packedWidth,1.0F/packedHeight,0,0};
        c->UpdateSubresource(cb[2].Get(),0,nullptr,values[2].data(),0,0);
        D3D11_VIEWPORT largeViewport{0,0,float(packedWidth),float(packedHeight),0,1};c->RSSetViewports(1,&largeViewport);
        D3D11_RECT all{0,0,LONG(packedWidth),LONG(packedHeight)};c->RSSetScissorRects(1,&all);
        c->OMSetDepthStencilState(nullptr,0);c->VSSetShader(vs[0].Get(),nullptr,0);
        const V allGeometry{2,1,0,0};c->UpdateSubresource(coverageRules.Get(),0,nullptr,&allGeometry,0,0);
        const auto simpleNative=read(paths[9]);ComPtr<ID3D11PixelShader> nativePS;
        hr(d->CreatePixelShader(simpleNative.data(),simpleNative.size(),nullptr,&nativePS),"benchmark native shader");
        std::array<ID3D11ShaderResourceView*,2> emptyViews{};c->CSSetShaderResources(30,2,emptyViews.data());
        ComPtr<ID3D11Query> interval,start,end;
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT,0};hr(d->CreateQuery(&query,&interval),"timing interval");
        query.Query=D3D11_QUERY_TIMESTAMP;hr(d->CreateQuery(&query,&start),"timing start");hr(d->CreateQuery(&query,&end),"timing end");
        std::array<std::vector<double>,2> timings;
        for(UINT sample=0;sample<40;++sample) for(UINT compact=0;compact<2;++compact) {
            c->Begin(interval.Get());c->End(start.Get());
            auto* benchmarkTarget=compact?coverage.scratchRenderTarget():bigView.Get();c->OMSetRenderTargets(1,&benchmarkTarget,nullptr);
            const float black[4]={0,0,0,0},unwritten[4]={-1,-1,-1,-1};
            c->ClearRenderTargetView(benchmarkTarget,compact?black:unwritten);
            c->PSSetShader(compact?coveragePS.Get():nativePS.Get(),nullptr,0);c->Draw(12,0);
            c->OMSetRenderTargets(0,nullptr,nullptr);c->PSSetShader(nativePS.Get(),nullptr,0);
            if (compact) check(capture.dispatch(c.Get(),coverage.scratchShaderResource(),9),"benchmark dispatch");
            c->End(end.Get());c->End(interval.Get());c->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};UINT64 begin{},finish{};
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(c->GetData(interval.Get(),&frequency,sizeof(frequency),0)==S_FALSE) {
                check(std::chrono::steady_clock::now()<deadline,"benchmark timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            hr(c->GetData(start.Get(),&begin,sizeof(begin),0),"start timestamp");hr(c->GetData(end.Get(),&finish,sizeof(finish),0),"end timestamp");
            if (!frequency.Disjoint && frequency.Frequency && sample>=8) timings[compact].push_back(double(finish-begin)*1000/frequency.Frequency);
        }
        for (auto& row:timings) {check(!row.empty(),"no valid GPU intervals");std::sort(row.begin(),row.end());}
        std::cout<<"5376x2880 capture median GPU: native full HDR="<<timings[0][timings[0].size()/2]
            <<" ms, native mask + compact lighting="<<timings[1][timings[1].size()/2]<<" ms\n";
        std::cout<<"Capture storage: full HDR="<<uint64_t(packedWidth)*packedHeight*8
            <<" bytes; native mask + stereo cube samples="<<uint64_t(packedWidth)*packedHeight+cubeExtent*cubeExtent*12*16<<" bytes\n";
    }
    ComPtr<ID3D11InfoQueue> messages;
    if (SUCCEEDED(d.As(&messages))) for(UINT64 index=0;index<messages->GetNumStoredMessages();++index) {
        SIZE_T size{};messages->GetMessage(index,nullptr,&size);std::vector<std::byte> storage(size);
        auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());messages->GetMessage(index,message,&size);
        if (message->Severity==D3D11_MESSAGE_SEVERITY_ERROR || message->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION)
            throw std::runtime_error(message->pDescription);
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
