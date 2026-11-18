#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "render/ComputeStateScope.h"
#include "render/Fo4VrCamera.h"
#include "support/Logger.h"
#include "ScreenSpaceGIShaders.h"
#include "resources.h"
#include <d3d11_1.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
namespace csah::screen_space_gi
{
    using Microsoft::WRL::ComPtr;
    namespace
    {
        // Compute bindings one SSGI pass may use, saved and restored as a block.
        constexpr UINT kComputeInputs=8,kComputeOutputs=4;
        // Render thread only: set for the whole composite-time pass, including
        // the state restore that rebinds the native composite.
        bool gComposeRender{};
        struct ComposeRenderScope final
        {
            ComposeRenderScope() noexcept {gComposeRender=true;}
            ~ComposeRenderScope() {gComposeRender=false;}
            ComposeRenderScope(const ComposeRenderScope&)=delete;
            ComposeRenderScope& operator=(const ComposeRenderScope&)=delete;
        };
        bool textureDescription(ID3D11ShaderResourceView* view,D3D11_TEXTURE2D_DESC& result) noexcept
        {
            if(!view) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC description{};view->GetDesc(&description);
            if(description.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || description.Texture2D.MostDetailedMip!=0) return false;
            ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture))) return false;
            texture->GetDesc(&result);
            return result.ArraySize==1 && result.SampleDesc.Count==1;
        }
        // Saves and restores exactly the graphics state one composite-time
        // fullscreen pass touches, on the render thread inside the native
        // composite draw. Pixel constant buffers keep their bound ranges:
        // FO4VR binds its camera buffer with offsets.
        class ScopedComposeState final
        {
        public:
            static constexpr UINT kResources=kConsumerFirstSlot+kConsumerViewCount,kConstants=13,kClasses=256;
            explicit ScopedComposeState(ID3D11DeviceContext* context) noexcept:context_(context)
            {
                context_->QueryInterface(IID_PPV_ARGS(&context1_));
                context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets_.data(),&depth_);
                context_->OMGetBlendState(&blend_,blendFactor_.data(),&sampleMask_);
                context_->OMGetDepthStencilState(&depthState_,&stencilRef_);
                context_->RSGetState(&raster_);
                viewportCount_=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                context_->RSGetViewports(&viewportCount_,viewports_.data());
                context_->IAGetInputLayout(&layout_);
                context_->IAGetPrimitiveTopology(&topology_);
                vertexClassCount_=kClasses;context_->VSGetShader(&vertex_,vertexClasses_.data(),&vertexClassCount_);
                pixelClassCount_=kClasses;context_->PSGetShader(&pixel_,pixelClasses_.data(),&pixelClassCount_);
                geometryClassCount_=kClasses;context_->GSGetShader(&geometry_,geometryClasses_.data(),&geometryClassCount_);
                hullClassCount_=kClasses;context_->HSGetShader(&hull_,hullClasses_.data(),&hullClassCount_);
                domainClassCount_=kClasses;context_->DSGetShader(&domain_,domainClasses_.data(),&domainClassCount_);
                context_->PSGetShaderResources(0,kResources,resources_.data());
                if(context1_) context1_->PSGetConstantBuffers1(0,kConstants,constants_.data(),firstConstant_.data(),constantCount_.data());
                else context_->PSGetConstantBuffers(0,kConstants,constants_.data());
            }
            ~ScopedComposeState()
            {
                // Render targets first, so the accumulators are no longer
                // bound for output when the composite inputs are restored.
                context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets_.data(),depth_);
                context_->OMSetBlendState(blend_,blendFactor_.data(),sampleMask_);
                context_->OMSetDepthStencilState(depthState_,stencilRef_);
                context_->RSSetState(raster_);
                context_->RSSetViewports(viewportCount_,viewports_.data());
                context_->IASetInputLayout(layout_);
                context_->IASetPrimitiveTopology(topology_);
                context_->VSSetShader(vertex_,vertexClasses_.data(),vertexClassCount_);
                context_->PSSetShader(pixel_,pixelClasses_.data(),pixelClassCount_);
                context_->GSSetShader(geometry_,geometryClasses_.data(),geometryClassCount_);
                context_->HSSetShader(hull_,hullClasses_.data(),hullClassCount_);
                context_->DSSetShader(domain_,domainClasses_.data(),domainClassCount_);
                context_->PSSetShaderResources(0,kResources,resources_.data());
                if(context1_) context1_->PSSetConstantBuffers1(0,kConstants,constants_.data(),firstConstant_.data(),constantCount_.data());
                else context_->PSSetConstantBuffers(0,kConstants,constants_.data());
                const auto release=[](auto* value) noexcept {if(value) value->Release();};
                for(auto* value:targets_) release(value);
                release(depth_);release(blend_);release(depthState_);release(raster_);release(layout_);
                release(vertex_);release(pixel_);release(geometry_);release(hull_);release(domain_);
                for(UINT i=0;i<vertexClassCount_;i++) release(vertexClasses_[i]);
                for(UINT i=0;i<pixelClassCount_;i++) release(pixelClasses_[i]);
                for(UINT i=0;i<geometryClassCount_;i++) release(geometryClasses_[i]);
                for(UINT i=0;i<hullClassCount_;i++) release(hullClasses_[i]);
                for(UINT i=0;i<domainClassCount_;i++) release(domainClasses_[i]);
                for(auto* value:resources_) release(value);
                for(auto* value:constants_) release(value);
            }
            ScopedComposeState(const ScopedComposeState&)=delete;
            ScopedComposeState& operator=(const ScopedComposeState&)=delete;
        private:
            ID3D11DeviceContext* context_{};
            ComPtr<ID3D11DeviceContext1> context1_;
            std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets_{};
            ID3D11DepthStencilView* depth_{};
            ID3D11BlendState* blend_{};std::array<float,4> blendFactor_{};UINT sampleMask_{};
            ID3D11DepthStencilState* depthState_{};UINT stencilRef_{};
            ID3D11RasterizerState* raster_{};
            std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports_{};UINT viewportCount_{};
            ID3D11InputLayout* layout_{};D3D11_PRIMITIVE_TOPOLOGY topology_{};
            ID3D11VertexShader* vertex_{};ID3D11PixelShader* pixel_{};ID3D11GeometryShader* geometry_{};
            ID3D11HullShader* hull_{};ID3D11DomainShader* domain_{};
            std::array<ID3D11ClassInstance*,kClasses> vertexClasses_{},pixelClasses_{},geometryClasses_{},hullClasses_{},domainClasses_{};
            UINT vertexClassCount_{},pixelClassCount_{},geometryClassCount_{},hullClassCount_{},domainClassCount_{};
            std::array<ID3D11ShaderResourceView*,kResources> resources_{};
            std::array<ID3D11Buffer*,kConstants> constants_{};
            std::array<UINT,kConstants> firstConstant_{},constantCount_{};
        };
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* context,const ConsumerViews& views) noexcept:context_(context)
    {
        if(!context_) return;
        ConsumerViews previous{};
        context_->PSGetShaderResources(kConsumerFirstSlot,kConsumerViewCount,previous.data());
        for(std::size_t i=0;i<kConsumerViewCount;i++) previous_[i].Attach(previous[i]);
        context_->PSSetShaderResources(kConsumerFirstSlot,kConsumerViewCount,views.data());
    }
    ScopedBindings::~ScopedBindings()
    {
        if(!context_) return;
        ConsumerViews previous{};
        for(std::size_t i=0;i<kConsumerViewCount;i++) previous[i]=previous_[i].Get();
        context_->PSSetShaderResources(kConsumerFirstSlot,kConsumerViewCount,previous.data());
    }
    Runtime& Runtime::get() noexcept {static Runtime runtime;return runtime;}
    void Runtime::applySettings(const Settings& value) noexcept
    {
        const auto s=sanitize(value);
        {const std::scoped_lock lock(settingsMutex_);pending_=s;}
        enabled_.store(s.enabled,std::memory_order_release);resetHistory();
    }
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {
        device_=device;context_=context;shaders_={};constants_.Reset();parameterView_.Reset();previousCamera_.Reset();noise_.Reset();
        positions_={};normals_={};luma_={};chroma_={};specular_={};
        remappedLuma_={};remappedChroma_={};remappedSpecular_={};tracedLuma_={};tracedChroma_={};radiance_={};radianceMips_={};
        composeVertex_.Reset();composePixel_.Reset();composeBlend_.Reset();composeRaster_.Reset();composeDepth_.Reset();
        composeConstants_={};composeTargets_={};frameNative_={};frameAuthored_.Reset();frameSurface_.Reset();frameStereo_.Reset();frameCamera_.Reset();emissive_.Reset();
        frame_=preparedFrame_=radianceFrame_=composedFrame_=nextRetry_=0;width_=height_=current_=0;ready_=historyValid_=loggedFailure_=loggedActive_=false;
        compositeScope_={};generationTiming_.reset();compositeTiming_.reset();
        if(device_ && context_) {
            using Group=render::GpuTimingProfiler::Group;
            if(!generationTiming_.initialize(device_.Get(),context_.Get(),"Screen Space GI generation",
                    {"prepare + position mips","reproject + radiance mips","trace","blur"},4,120,1,Group::ScreenSpaceGI) ||
                !compositeTiming_.initialize(device_.Get(),context_.Get(),"Screen Space GI composite",
                    {"radiance capture","occlusion compose",nullptr,nullptr},2,120,1,Group::ScreenSpaceGI))
                logging::warn("Screen Space GI could not allocate GPU timing queries; rendering remains active without performance telemetry.");
        }
        resetHistory();
    }
    void Runtime::beginFrame() noexcept
    {
        ++frame_;
        {const std::scoped_lock lock(settingsMutex_);settings_=pending_;}
        historyValid_=requested() && radianceFrame_+1==frame_ && preparedFrame_+1==frame_;
        if(reset_.exchange(false,std::memory_order_acq_rel)) historyValid_=false;
        current_^=1;ready_=false;
    }
    bool Runtime::createTexture(Texture& value,DXGI_FORMAT format,UINT width,UINT height,UINT mips) noexcept
    {
        value={};D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=mips;d.ArraySize=2;
        d.Format=format;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&value.texture)) ||
            FAILED(device_->CreateShaderResourceView(value.texture.Get(),nullptr,&value.view))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=format;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;u.Texture2DArray.ArraySize=2;
        if(FAILED(device_->CreateUnorderedAccessView(value.texture.Get(),&u,&value.output))) return false;
        if(mips==1) return true;
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MostDetailedMip=0;s.Texture2DArray.MipLevels=1;s.Texture2DArray.ArraySize=2;
        if(FAILED(device_->CreateShaderResourceView(value.texture.Get(),&s,&value.topView))) return false;
        for(UINT mip=1;mip<mips;mip++) {
            u.Texture2DArray.MipSlice=mip;
            if(FAILED(device_->CreateUnorderedAccessView(value.texture.Get(),&u,&value.mipOutputs[mip-1]))) return false;
        }
        return true;
    }
    bool Runtime::createNoise() noexcept
    {
        // Upstream's EA fastnoise texture, embedded as RCDATA: a 128-byte DDS
        // header, then mip 0 as 128x8192 L8A8 (64 frames of 128x128).
        constexpr UINT kWidth=128,kHeight=8192,kHeader=128;
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Runtime::get),&module)) return false;
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(IDR_SCREEN_SPACE_GI_NOISE),RT_RCDATA);
        const auto loaded=resource?LoadResource(module,resource):nullptr;
        const auto* bytes=loaded?static_cast<const unsigned char*>(LockResource(loaded)):nullptr;
        const auto size=resource?SizeofResource(module,resource):0;
        if(!bytes || size<kHeader+kWidth*kHeight*2) return false;
        std::uint32_t height{},width{},bits{};
        std::memcpy(&height,bytes+12,4);std::memcpy(&width,bytes+16,4);std::memcpy(&bits,bytes+88,4);
        if(std::memcmp(bytes,"DDS ",4)!=0 || width!=kWidth || height!=kHeight || bits!=16) return false;
        D3D11_TEXTURE2D_DESC d{};d.Width=kWidth;d.Height=kHeight;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8_UNORM;
        d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{bytes+kHeader,kWidth*2,0};
        Ptr<ID3D11Texture2D> texture;
        return SUCCEEDED(device_->CreateTexture2D(&d,&initial,&texture)) && SUCCEEDED(device_->CreateShaderResourceView(texture.Get(),nullptr,&noise_));
    }
    bool Runtime::failAllocation() noexcept
    {
        nextRetry_=frame_+60;historyValid_=false;
        if(!loggedFailure_) {loggedFailure_=true;logging::error("Screen Space GI GPU allocation failed; native ambient lighting remains active. Retrying at render cadence.");}
        return false;
    }
    bool Runtime::ensureResources(UINT width,UINT height) noexcept
    {
        if(!device_ || !context_) return false;
        if(width==width_ && height==height_ && constants_ && parameterView_ && noise_ && shaders_.back() && radianceMips_.output) return true;
        if(frame_<nextRetry_) return false;
        width_=height_=0;historyValid_=false;
        const auto create=[&]() noexcept {
            for(std::size_t i=0;i<shaders_.size();i++) {
                if(!shaders_[i] && FAILED(device_->CreateComputeShader(kScreenSpaceGIShaders[i].data,kScreenSpaceGIShaders[i].size,nullptr,&shaders_[i]))) return false;
            }
            if(!constants_) {
                D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(Constants);d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                if(FAILED(device_->CreateBuffer(&d,nullptr,&constants_))) return false;
            }
            if(!parameterView_) {
                D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(Constants);d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                Ptr<ID3D11Buffer> buffer;if(FAILED(device_->CreateBuffer(&d,nullptr,&buffer))) return false;
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
                view.Buffer.NumElements=sizeof(Constants)/16;
                if(FAILED(device_->CreateShaderResourceView(buffer.Get(),&view,&parameterView_))) return false;
            }
            if(!noise_ && !createNoise()) return false;
            constexpr auto half=DXGI_FORMAT_R16G16B16A16_FLOAT;
            for(UINT i=0;i<2;i++) {
                if(!createTexture(positions_[i],DXGI_FORMAT_R32G32B32A32_FLOAT,width,height,kMipCount) ||
                    !createTexture(normals_[i],half,width,height) || !createTexture(luma_[i],half,width,height) ||
                    !createTexture(chroma_[i],half,width,height) || !createTexture(specular_[i],half,width,height)) return false;
            }
            return createComposeResources() &&
                createTexture(remappedLuma_,half,width,height) && createTexture(remappedChroma_,half,width,height) &&
                createTexture(remappedSpecular_,half,width,height) && createTexture(tracedLuma_,half,width,height) &&
                createTexture(tracedChroma_,half,width,height) && createTexture(radiance_,half,width,height) &&
                createTexture(radianceMips_,half,width,height,kMipCount);
        };
        if(!create()) return failAllocation();
        width_=width;height_=height;nextRetry_=0;
        return true;
    }
    void Runtime::dispatch(Pass pass,std::initializer_list<ID3D11ShaderResourceView*> inputs,
        std::initializer_list<ID3D11UnorderedAccessView*> outputs,UINT width,UINT height) noexcept
    {
        std::array<ID3D11ShaderResourceView*,kComputeInputs> views{};std::copy(inputs.begin(),inputs.end(),views.begin());
        std::array<ID3D11UnorderedAccessView*,kComputeOutputs> targets{};std::copy(outputs.begin(),outputs.end(),targets.begin());
        const std::array<ID3D11ShaderResourceView*,kComputeInputs> noInputs{};
        const std::array<ID3D11UnorderedAccessView*,kComputeOutputs> noOutputs{};
        context_->CSSetShaderResources(0,kComputeInputs,views.data());context_->CSSetUnorderedAccessViews(0,kComputeOutputs,targets.data(),nullptr);
        context_->CSSetShader(shaders_[static_cast<std::size_t>(pass)].Get(),nullptr,0);
        context_->Dispatch((std::max(width,1u)+7)/8,(std::max(height,1u)+7)/8,2);
        context_->CSSetUnorderedAccessViews(0,kComputeOutputs,noOutputs.data(),nullptr);context_->CSSetShaderResources(0,kComputeInputs,noInputs.data());
    }
    bool Runtime::prepareAmbient(ID3D11DeviceContext* context, ID3D11ShaderResourceView* authored, const pbr::FrameData& materialSettings, ID3D11ShaderResourceView* surface) noexcept
    {
        if(preparedFrame_==frame_) return ready_;
        if(frame_<nextRetry_) return reject(Rejection::retryWait);
        std::array<ComPtr<ID3D11ShaderResourceView>,3> inputs;
        for(UINT i=0;i<3;i++) context->PSGetShaderResources(i+1,1,inputs[i].GetAddressOf());
        D3D11_TEXTURE2D_DESC normal{},depth{},material{};
        const bool described=textureDescription(inputs[0].Get(),normal) & textureDescription(inputs[1].Get(),material) & textureDescription(inputs[2].Get(),depth);
        if(!described ||
            normal.Format!=DXGI_FORMAT_R16G16_UNORM || material.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
            normal.Width!=depth.Width || normal.Height!=depth.Height || material.Width!=depth.Width || material.Height!=depth.Height ||
            depth.Width%2 || depth.Width<=depth.Height) {
            lastRejectedFormats_[0].store(normal.Format,std::memory_order_relaxed);
            lastRejectedFormats_[1].store(material.Format,std::memory_order_relaxed);
            lastRejectedFormats_[2].store(depth.Format,std::memory_order_relaxed);
            return reject(Rejection::gbufferContract);
        }
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
        if(count!=1 || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.Width<16 || viewport.Height<8 ||
            viewport.TopLeftX<0 || viewport.TopLeftY<0 || viewport.TopLeftX+viewport.Width>depth.Width || viewport.TopLeftY+viewport.Height>depth.Height) return reject(Rejection::viewport);
        // The viewport packs both eyes side by side; each eye slice is half its width.
        const auto& s=settings_;
        const float scale=1.0f/static_cast<float>(1<<s.resolutionMode);
        const auto width=static_cast<UINT>(std::ceil(viewport.Width*.5f*scale)),height=static_cast<UINT>(std::ceil(viewport.Height*scale));
        ComPtr<ID3D11Buffer> stereo,camera;
        context->PSGetConstantBuffers(8,1,&stereo);context->PSGetConstantBuffers(12,1,&camera);
        if(!stereo || !camera) return reject(Rejection::cameraBuffers);
        D3D11_BUFFER_DESC d{};camera->GetDesc(&d);
        if(d.ByteWidth<sizeof(render::Fo4VrFrameBuffer)) return reject(Rejection::cameraBuffers);
        if(!ensureResources(width,height)) return reject(Rejection::resources);
        if(previousCamera_) {D3D11_BUFFER_DESC old{};previousCamera_->GetDesc(&old);if(old.ByteWidth!=d.ByteWidth) {previousCamera_.Reset();historyValid_=false;}}
        if(!previousCamera_) {d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device_->CreateBuffer(&d,nullptr,&previousCamera_))) {(void)failAllocation();return reject(Rejection::resources);}
            context->CopyResource(previousCamera_.Get(),camera.Get());historyValid_=false;}
        if(uploaded_.originX!=viewport.TopLeftX || uploaded_.originY!=viewport.TopLeftY ||
            uploaded_.viewportWidth!=viewport.Width || uploaded_.viewportHeight!=viewport.Height) historyValid_=false;
        if(std::memcmp(&uploaded_.material,&materialSettings,sizeof(materialSettings))!=0) historyValid_=false;
        // Upstream UpdateConstants: radii as fractions of the larger one, the
        // minimum screen radius in internal pixels, and compensation reaching
        // the AO radius.
        const float effect=std::max(s.aoRadius,s.giRadius);
        uploaded_={.width=width,.height=height,.frame=static_cast<UINT>(frame_),.history=historyValid_?1u:0u,
            .originX=viewport.TopLeftX,.originY=viewport.TopLeftY,.viewportWidth=viewport.Width,.viewportHeight=viewport.Height,
            .aoRadius=s.aoRadius/effect,.thickness=s.thickness,.aoPower=s.aoPower,.strength=s.strength,
            .gi=s.indirectLight?1u:0u,.specular=s.specular?1u:0u,.temporal=s.temporal?1u:0u,.tiled=uploaded_.tiled,
            .effectRadius=effect,.giRadius=s.giRadius/effect,.minScreenRadius=s.minScreenRadius*static_cast<float>(width),.saturation=s.saturation,
            .distanceCompensation=s.distanceCompensation,.compensationMaxDistance=s.aoRadius,.fadeStart=s.fadeStart,.fadeScale=1.0f/(s.fadeEnd-s.fadeStart),
            .depthDisocclusion=s.depthDisocclusion,.blurRadius=s.blurRadius,.distanceNormalisation=s.distanceNormalisation,.maxAccumFrames=static_cast<UINT>(s.maxAccumFrames),
            .slices=static_cast<UINT>(s.slices),.steps=static_cast<UINT>(s.steps),.blur=s.blur?1u:0u,.resolutionMode=static_cast<UINT>(s.resolutionMode),
            .material=materialSettings};
        context->UpdateSubresource(constants_.Get(),0,nullptr,&uploaded_,0,0);
        // Separate typed buffer for pixel consumers; never bind a CB as an SRV.
        ComPtr<ID3D11Resource> parameters;parameterView_->GetResource(&parameters);
        context->UpdateSubresource(parameters.Get(),0,nullptr,&uploaded_,0,0);
        render::ScopedComputeState restore(context,{.firstShaderResource=0,.shaderResourceCount=kComputeInputs,.firstUnorderedAccess=0,.unorderedAccessCount=kComputeOutputs,.firstConstantBuffer=2,.constantBufferCount=12});
        if(!restore.captured()) return reject(Rejection::computeState);
        ID3D11Buffer* cb=stereo.Get();context->CSSetConstantBuffers(8,1,&cb);
        cb=previousCamera_.Get();context->CSSetConstantBuffers(10,1,&cb);cb=camera.Get();context->CSSetConstantBuffers(12,1,&cb);
        cb=constants_.Get();context->CSSetConstantBuffers(13,1,&cb);
        const auto previous=current_^1;
        const UINT mipWidth=std::max(width/2,1u),mipHeight=std::max(height/2,1u);
        auto& position=positions_[current_];auto& surfaceNormal=normals_[current_];
        generationTiming_.poll();
        auto timing=generationTiming_.begin();
        dispatch(Pass::prepare,{inputs[0].Get(),inputs[2].Get(),inputs[1].Get(),authored,surface},{position.output.Get(),surfaceNormal.output.Get()},width,height);
        dispatch(Pass::prefilterPosition,{position.topView.Get()},
            {position.mipOutputs[0].Get(),position.mipOutputs[1].Get(),position.mipOutputs[2].Get(),position.mipOutputs[3].Get()},mipWidth,mipHeight);
        timing.mark();
        dispatch(Pass::reproject,{position.view.Get(),positions_[previous].view.Get(),radiance_.view.Get(),luma_[previous].view.Get(),chroma_[previous].view.Get(),specular_[previous].view.Get()},
            {radianceMips_.output.Get(),remappedLuma_.output.Get(),remappedChroma_.output.Get(),remappedSpecular_.output.Get()},width,height);
        dispatch(Pass::prefilterRadiance,{radianceMips_.topView.Get()},
            {radianceMips_.mipOutputs[0].Get(),radianceMips_.mipOutputs[1].Get(),radianceMips_.mipOutputs[2].Get(),radianceMips_.mipOutputs[3].Get()},mipWidth,mipHeight);
        timing.mark();
        dispatch(Pass::trace,{position.view.Get(),surfaceNormal.view.Get(),radianceMips_.view.Get(),noise_.Get(),remappedLuma_.view.Get(),remappedChroma_.view.Get(),remappedSpecular_.view.Get(),emissive_.Get()},
            {tracedLuma_.output.Get(),tracedChroma_.output.Get(),specular_[current_].output.Get()},width,height);
        timing.mark();
        dispatch(Pass::blur,{position.view.Get(),surfaceNormal.view.Get(),tracedLuma_.view.Get(),tracedChroma_.view.Get()},
            {luma_[current_].output.Get(),chroma_[current_].output.Get()},width,height);
        timing.finish();
        context->CopyResource(previousCamera_.Get(),camera.Get());
        frameNative_={inputs[0],inputs[1],inputs[2]};frameAuthored_=authored;frameSurface_=surface;frameStereo_=stereo;frameCamera_=camera;
        preparedFrame_=frame_;ready_=true;preparedFrames_.fetch_add(1,std::memory_order_relaxed);
        if(!loggedActive_) {loggedActive_=true;logging::info("Screen Space GI active: two isolated {}x{} eye slices, {} slices x {} steps, AO={}, indirect={}, experimentalSpecular={}, temporal={}, blur={}; current geometry and prior-frame surface lighting.",
            width_,height_,uploaded_.slices,uploaded_.steps,uploaded_.aoPower,uploaded_.gi!=0,uploaded_.specular!=0,uploaded_.temporal!=0,uploaded_.blur!=0);}
        return true;
    }
    ConsumerViews Runtime::consumerViews(ID3D11ShaderResourceView* albedo,ID3D11ShaderResourceView* authored,ID3D11ShaderResourceView* surface) const noexcept
    {
        // t57 luma, t58 specular, t59 parameters, t60 albedo, t61 authored PBR,
        // t62 positions, t63 normals, t64 surface class, t65 chroma and AO.
        return {luma_[current_].view.Get(),specular_[current_].view.Get(),parameterView_.Get(),albedo,authored,
            positions_[current_].view.Get(),normals_[current_].view.Get(),surface,chroma_[current_].view.Get()};
    }
    ScopedBindings Runtime::scopeAmbient(ID3D11DeviceContext* context,bool ambient,ID3D11ShaderResourceView* authored, const pbr::FrameData& material, ID3D11ShaderResourceView* surface) noexcept
    {
        if(!ambient || context!=context_.Get()) return {nullptr,{}};
        ambientScopes_.fetch_add(1,std::memory_order_relaxed);
        if(!requested() || !prepareAmbient(context,authored,material,surface)) return {context,{}};
        ComPtr<ID3D11ShaderResourceView> albedo;context->PSGetShaderResources(0,1,&albedo);
        return {context,consumerViews(albedo.Get(),authored,surface)};
    }
    void Runtime::captureRadiance(ID3D11DeviceContext* context,bool tiledActive) noexcept
    {
        if(!requested() || context!=context_.Get() || !ready_ || preparedFrame_!=frame_ || radianceFrame_==frame_) return;
        const std::array<UINT,4> slots{0,4,5,11};std::array<ComPtr<ID3D11ShaderResourceView>,4> inputs;
        for(std::size_t i=0;i<inputs.size();i++) context->PSGetShaderResources(slots[i],1,&inputs[i]);
        D3D11_TEXTURE2D_DESC albedo{},emissive{},diffuse{},tiled{};
        if(!textureDescription(inputs[0].Get(),albedo)||!textureDescription(inputs[1].Get(),emissive)||!textureDescription(inputs[2].Get(),diffuse) ||
            albedo.Width!=diffuse.Width || albedo.Height!=diffuse.Height || emissive.Width!=albedo.Width || emissive.Height!=albedo.Height) return;
        if(tiledActive && (!textureDescription(inputs[3].Get(),tiled) || tiled.Width!=albedo.Width || tiled.Height!=albedo.Height)) return;
        emissive_=inputs[1];
        uploaded_.tiled=tiledActive?1u:0u;
        render::ScopedComputeState restore(context,{.firstShaderResource=0,.shaderResourceCount=kComputeInputs,.firstUnorderedAccess=0,.unorderedAccessCount=kComputeOutputs,.firstConstantBuffer=2,.constantBufferCount=12});
        if(!restore.captured()) return;
        context->UpdateSubresource(constants_.Get(),0,nullptr,&uploaded_,0,0);
        auto* cb=constants_.Get();context->CSSetConstantBuffers(13,1,&cb);
        compositeTiming_.poll();
        compositeScope_=compositeTiming_.begin();
        dispatch(Pass::capture,{inputs[0].Get(),inputs[1].Get(),inputs[2].Get(),inputs[3].Get(),positions_[current_].view.Get()},{radiance_.output.Get()},width_,height_);
        compositeScope_.mark();
        radianceFrame_=frame_;radianceCaptures_.fetch_add(1,std::memory_order_relaxed);
    }
    bool Runtime::internalRenderActive() noexcept {return gComposeRender;}
    bool Runtime::createComposeResources() noexcept
    {
        if(composeVertex_ && composePixel_ && composeBlend_ && composeRaster_ && composeDepth_ && composeConstants_[0] && composeConstants_[1]) return true;
        if(FAILED(device_->CreateVertexShader(kScreenSpaceGIComposeVS.data,kScreenSpaceGIComposeVS.size,nullptr,&composeVertex_)) ||
            FAILED(device_->CreatePixelShader(kScreenSpaceGIComposePS.data,kScreenSpaceGIComposePS.size,nullptr,&composePixel_))) return false;
        // dest * Target1 + Target0: the accumulator takes the root of the
        // occlusion and the added bounce; its alpha is kept.
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].BlendEnable=TRUE;
        blend.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;blend.RenderTarget[0].DestBlend=D3D11_BLEND_SRC1_COLOR;blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ZERO;blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ONE;blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(FAILED(device_->CreateBlendState(&blend,&composeBlend_)) || FAILED(device_->CreateRasterizerState(&raster,&composeRaster_)) ||
            FAILED(device_->CreateDepthStencilState(&depth,&composeDepth_))) return false;
        for(UINT i=0;i<2;i++) {
            const std::array<UINT,4> value{i,0,0,0};
            D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(value);d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA initial{value.data(),0,0};
            if(FAILED(device_->CreateBuffer(&d,&initial,&composeConstants_[i]))) return false;
        }
        return true;
    }
    ID3D11RenderTargetView* Runtime::composeTarget(std::size_t index,ID3D11ShaderResourceView* accumulator) noexcept
    {
        if(!accumulator) return nullptr;
        ComPtr<ID3D11Resource> resource;accumulator->GetResource(&resource);
        auto& target=composeTargets_[index];
        if(target.view && target.resource.Get()==resource.Get()) return target.view.Get();
        target={};
        ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture))) return nullptr;
        D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);
        if(!(description.BindFlags&D3D11_BIND_RENDER_TARGET) || description.ArraySize!=1 || description.SampleDesc.Count!=1) return nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};accumulator->GetDesc(&view);
        D3D11_RENDER_TARGET_VIEW_DESC output{};output.Format=view.Format;output.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if(FAILED(device_->CreateRenderTargetView(resource.Get(),&output,&target.view))) {target={};return nullptr;}
        target.resource=resource;
        return target.view.Get();
    }
    void Runtime::composeOcclusion(ID3D11DeviceContext* context,bool tiled) noexcept
    {
        if(!requested() || context!=context_.Get() || !ready_ || preparedFrame_!=frame_ || composedFrame_==frame_) return;
        composedFrame_=frame_;
        // Native DFComposite inputs: t0 albedo, t5 deferred diffuse, t11 tiled diffuse.
        std::array<ComPtr<ID3D11ShaderResourceView>,3> inputs;
        context->PSGetShaderResources(0,1,inputs[0].GetAddressOf());
        context->PSGetShaderResources(5,1,inputs[1].GetAddressOf());
        context->PSGetShaderResources(11,1,inputs[2].GetAddressOf());
        const std::array<ID3D11RenderTargetView*,2> targets{composeTarget(0,inputs[1].Get()),tiled?composeTarget(1,inputs[2].Get()):nullptr};
        D3D11_TEXTURE2D_DESC accumulator{};
        if(!inputs[0] || !targets[0] || (tiled && !targets[1]) || !textureDescription(inputs[1].Get(),accumulator)) {
            composeFailures_.fetch_add(1,std::memory_order_relaxed);
            if(!loggedComposeFailure_) {
                loggedComposeFailure_=true;
                logging::error("Screen Space GI could not bind the composite diffuse accumulators (albedo={}, deferred={}, tiled={}/{}); this frame keeps only the ambient half of the occlusion.",
                    static_cast<bool>(inputs[0]),targets[0]!=nullptr,tiled,targets[1]!=nullptr);
            }
            return;
        }
        {
            const ComposeRenderScope internal;
            ScopedComposeState state(context);
            const D3D11_VIEWPORT viewport{0,0,static_cast<float>(accumulator.Width),static_cast<float>(accumulator.Height),0,1};
            context->RSSetViewports(1,&viewport);
            context->RSSetState(composeRaster_.Get());
            context->OMSetBlendState(composeBlend_.Get(),nullptr,0xFFFFFFFFu);
            context->OMSetDepthStencilState(composeDepth_.Get(),0);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(composeVertex_.Get(),nullptr,0);
            context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
            context->PSSetShader(composePixel_.Get(),nullptr,0);
            // The accumulators become render targets: no composite input may stay bound.
            const std::array<ID3D11ShaderResourceView*,ScopedComposeState::kResources> none{};
            context->PSSetShaderResources(0,ScopedComposeState::kResources,none.data());
            const std::array<ID3D11ShaderResourceView*,3> native{frameNative_[0].Get(),frameNative_[1].Get(),frameNative_[2].Get()};
            context->PSSetShaderResources(1,3,native.data());
            const auto gi=consumerViews(inputs[0].Get(),frameAuthored_.Get(),frameSurface_.Get());
            context->PSSetShaderResources(kConsumerFirstSlot,kConsumerViewCount,gi.data());
            ID3D11Buffer* buffer=frameStereo_.Get();context->PSSetConstantBuffers(8,1,&buffer);
            buffer=frameCamera_.Get();context->PSSetConstantBuffers(12,1,&buffer);
            for(UINT i=0;i<(tiled?2u:1u);i++) {
                // The bounce is added once, to the deferred accumulator.
                buffer=composeConstants_[i==0?1:0].Get();context->PSSetConstantBuffers(0,1,&buffer);
                context->OMSetRenderTargets(1,&targets[i],nullptr);
                context->Draw(3,0);
            }
        }
        compositeScope_.finish();
        composedFrames_.fetch_add(1,std::memory_order_relaxed);
    }
    bool Runtime::reject(Rejection reason) noexcept
    {
        rejections_[static_cast<std::size_t>(reason)].fetch_add(1,std::memory_order_relaxed);
        return false;
    }
    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        RuntimeSnapshot result{.enabled=requested(),.ambientScopes=ambientScopes_.load(std::memory_order_relaxed),
            .preparedFrames=preparedFrames_.load(std::memory_order_relaxed),.radianceCaptures=radianceCaptures_.load(std::memory_order_relaxed),
            .composedFrames=composedFrames_.load(std::memory_order_relaxed),.composeFailures=composeFailures_.load(std::memory_order_relaxed)};
        for(std::size_t i=0;i<kRejectionCount;i++) result.rejections[i]=rejections_[i].load(std::memory_order_relaxed);
        for(std::size_t i=0;i<3;i++) result.lastRejectedFormats[i]=lastRejectedFormats_[i].load(std::memory_order_relaxed);
        return result;
    }
}
