#include "Features/water_effects/WaterEffectsRuntime.h"
#include "support/Logger.h"
#include "WaterCausticsTexture.h"
#include "Features/ibl/IblRuntime.h"
#include <iterator>
namespace csah::water_effects
{
    using Microsoft::WRL::ComPtr;
    ScopedReflectionBindings::ScopedReflectionBindings(ID3D11DeviceContext* context,
        const std::array<ID3D11ShaderResourceView*,9>& views,ID3D11SamplerState* sampler) noexcept:context_(context)
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,9> previous{};context_->PSGetShaderResources(69,7,previous.data());context_->PSGetShaderResources(79,2,previous.data()+7);
        for(UINT i=0;i<9;i++) previous_[i].Attach(previous[i]);context_->PSGetSamplers(14,1,&sampler_);
        if(previous==views && sampler_.Get()==sampler) {context_=nullptr;return;}
        context_->PSSetShaderResources(69,7,views.data());context_->PSSetShaderResources(79,2,views.data()+7);context_->PSSetSamplers(14,1,&sampler);
    }
    ScopedReflectionBindings::~ScopedReflectionBindings()
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,9> previous{};for(UINT i=0;i<9;i++) previous[i]=previous_[i].Get();
        context_->PSSetShaderResources(69,7,previous.data());context_->PSSetShaderResources(79,2,previous.data()+7);auto* sampler=sampler_.Get();context_->PSSetSamplers(14,1,&sampler);
    }
    ScopedCausticBindings::ScopedCausticBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* texture,
        ID3D11ShaderResourceView* parameters,ID3D11SamplerState* sampler) noexcept:context_(context)
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,2> previous{};context_->PSGetShaderResources(65,2,previous.data());
        for(UINT i=0;i<2;i++) previous_[i].Attach(previous[i]);context_->PSGetSamplers(15,1,&sampler_);
        const std::array<ID3D11ShaderResourceView*,2> views{texture,parameters};
        context_->PSSetShaderResources(65,2,views.data());context_->PSSetSamplers(15,1,&sampler);
    }
    ScopedCausticBindings::~ScopedCausticBindings()
    {
        if(!context_) return;
        const std::array<ID3D11ShaderResourceView*,2> previous{previous_[0].Get(),previous_[1].Get()};
        context_->PSSetShaderResources(65,2,previous.data());auto* sampler=sampler_.Get();context_->PSSetSamplers(15,1,&sampler);
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* context,const std::array<ID3D11ShaderResourceView*,3>& views) noexcept:context_(context)
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,3> previous{};context_->PSGetShaderResources(66,3,previous.data());
        for(UINT i=0;i<3;i++) previous_[i].Attach(previous[i]);
        if(previous==views) {context_=nullptr;return;}
        context_->PSSetShaderResources(66,3,views.data());
    }
    ScopedBindings::~ScopedBindings()
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,3> previous{};for(UINT i=0;i<3;i++) previous[i]=previous_[i].Get();
        context_->PSSetShaderResources(66,3,previous.data());
    }
    Runtime& Runtime::get() noexcept {static Runtime runtime;return runtime;}
    void Runtime::applySettings(const Settings& value) noexcept
    {
        const auto s=sanitize(value);scale_.store(s.parallaxScale,std::memory_order_relaxed);
        parallax_.store(s.parallax);caustics_.store(s.caustics);causticStrength_.store(s.causticStrength);
        screenReflections_.store(s.screenReflections);reflectionFlatness_.store(s.reflectionFlatness);reflectionRoughness_.store(s.reflectionRoughness);
        reflections_.store(s.reflections);reflectionStrength_.store(s.reflectionStrength);reflectionDistance_.store(s.reflectionDistance);
        enabled_.store(s.enabled,std::memory_order_release);settingsRevision_.fetch_add(1,std::memory_order_release);
        if(!s.enabled || !s.caustics) world_.invalidate();
    }
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {
        device_=device;context_=context;buffers_={};views_={};missingDraws_=missingCausticDraws_=0;
        publishedSettings_=publishedWorld_=0;failed_=loggedFailure_=causticFailed_=loggedCausticFailure_=loggedCausticPlanes_=false;
        causticSampler_.Reset();causticView_.Reset();causticTexture_.Reset();
        reflectionParameters_.Reset();reflectionParameterView_.Reset();reflectionSampler_.Reset();
        reflectionFailed_=reflectionUploaded_=loggedReflectionFailure_=false;missingReflectionDraws_=0;uploadedReflection_={};
    }
    bool Runtime::ensureBuffer(Ptr<ID3D11Buffer>& buffer,Ptr<ID3D11ShaderResourceView>& view,UINT bytes) noexcept
    {
        if(buffer && view) {D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);if(d.ByteWidth==bytes) return true;}
        buffer.Reset();view.Reset();D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=bytes/16;
        return SUCCEEDED(device_->CreateBuffer(&d,nullptr,&buffer)) && SUCCEEDED(device_->CreateShaderResourceView(buffer.Get(),&v,&view));
    }
    bool Runtime::publishParameters() noexcept
    {
        if(!device_ || !context_) return false;
        const bool existing=buffers_[0] && views_[0];
        if(!ensureBuffer(buffers_[0],views_[0],28*16)) return false;
        const auto revision=settingsRevision_.load(std::memory_order_acquire);
        if(existing && revision==publishedSettings_ && world_.revision()==publishedWorld_) return true;
        const auto frame=world_.read();
        std::array<std::array<float,4>,28> parameters{};
        parameters[0]={requested() && parallax_.load()?1.0f:0.0f,scale_.load(),0,0};
        bool water=false;
        for(std::size_t i=0;i<frame.cells.size();i++) {
            const auto& cell=frame.cells[i];parameters[3+i]={cell.x,cell.y,cell.height,cell.valid};water|=cell.valid>0;
        }
        parameters[1]={causticsRequested()?1.0f:0.0f,causticStrength_.load(),frame.seconds,frame.valid && water?1.0f:0.0f};
        parameters[2]={float(frame.centerX),float(frame.centerY),0,0};
        if(frame.valid && water && !loggedCausticPlanes_) {loggedCausticPlanes_=true;logging::info("Water caustics received its first attached exterior-cell water planes; only matching submerged world positions contribute.");}
        context_->UpdateSubresource(buffers_[0].Get(),0,nullptr,parameters.data(),0,0);
        publishedSettings_=revision;publishedWorld_=frame.revision;return true;
    }
    bool Runtime::ensureCaustics() noexcept
    {
        if(causticView_ && causticSampler_) return true;
        if(causticFailed_ && ++missingCausticDraws_%60!=0) return false;
        const auto create=[&]() noexcept {
            D3D11_TEXTURE2D_DESC d{};d.Width=kWaterCausticWidth;d.Height=kWaterCausticHeight;
            d.MipLevels=static_cast<UINT>(std::size(kWaterCausticMips));d.ArraySize=d.SampleDesc.Count=1;
            d.Format=DXGI_FORMAT_BC4_UNORM;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            std::array<D3D11_SUBRESOURCE_DATA,std::size(kWaterCausticMips)> data{};
            for(std::size_t i=0;i<data.size();i++) data[i]={kWaterCausticBytes+kWaterCausticMips[i].offset,kWaterCausticMips[i].pitch,0};
            if(!causticTexture_ && FAILED(device_->CreateTexture2D(&d,data.data(),&causticTexture_))) return false;
            if(!causticView_ && FAILED(device_->CreateShaderResourceView(causticTexture_.Get(),nullptr,&causticView_))) return false;
            D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxLOD=D3D11_FLOAT32_MAX;
            return causticSampler_ || SUCCEEDED(device_->CreateSamplerState(&s,&causticSampler_));
        };
        if(!create()) {
            causticFailed_=true;missingCausticDraws_=0;
            if(!loggedCausticFailure_) {loggedCausticFailure_=true;logging::error("Water caustics allocation failed; native directional lighting remains active while resources recover.");}
            return false;
        }
        causticFailed_=false;return true;
    }
    ScopedCausticBindings Runtime::scopeCaustics(ID3D11DeviceContext* context) noexcept
    {
        if(!context || context!=context_.Get()) return {nullptr,nullptr,nullptr,nullptr};
        if(!causticsRequested() || !device_ || !ensureCaustics() || !publishParameters()) return {context,nullptr,nullptr,nullptr};
        return {context,causticView_.Get(),views_[0].Get(),causticSampler_.Get()};
    }
    bool Runtime::ensureReflections() noexcept
    {
        if(reflectionParameterView_ && reflectionSampler_) return true;
        if(reflectionFailed_ && ++missingReflectionDraws_%60!=0) return false;
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        if(!ensureBuffer(reflectionParameters_,reflectionParameterView_,96) ||
            (!reflectionSampler_ && FAILED(device_->CreateSamplerState(&sampler,&reflectionSampler_)))) {
            reflectionFailed_=true;missingReflectionDraws_=0;
            if(!loggedReflectionFailure_) {loggedReflectionFailure_=true;logging::error("Water reflection binding allocation failed; native water reflections remain active while resources recover.");}
            return false;
        }
        reflectionFailed_=false;reflectionUploaded_=false;return true;
    }
    ScopedReflectionBindings Runtime::scopeReflections(ID3D11DeviceContext* context,bool water,
        const ibl::SslrEnvironmentView& environment) noexcept
    {
        if(!water || !context || context!=context_.Get()) return {nullptr,{},nullptr};
        if(!reflectionsRequested() || !device_ || !ensureReflections()) return {context,{},nullptr};
        const bool cube=cubeReflectionsRequested() && environment.publishedAvailable && environment.publishedEnvironment && environment.publishedValidity && environment.publishedPosition;
        const bool fx=screenReflections_.load();
        Ptr<ID3D11ShaderResourceView> colour,depth;
        if(fx) {context->PSGetShaderResources(1,1,&colour);context->PSGetShaderResources(7,1,&depth);}
        const auto textureSize=[](ID3D11ShaderResourceView* view,UINT& width,UINT& height) noexcept {
            if(!view) return false;D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view->GetDesc(&vd);if(vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D) return false;
            Ptr<ID3D11Resource> resource;Ptr<ID3D11Texture2D> texture;view->GetResource(&resource);
            if(FAILED(resource.As(&texture))) return false;D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
            width=std::max(1u,td.Width>>vd.Texture2D.MostDetailedMip);height=std::max(1u,td.Height>>vd.Texture2D.MostDetailedMip);
            return td.SampleDesc.Count==1 && width>=2 && width%2==0;
        };
        UINT cw{},ch{},dw{},dh{};
        const bool scene=fx && textureSize(colour.Get(),cw,ch) && textureSize(depth.Get(),dw,dh);
        if(!cube && !fx) return {context,{},nullptr};
        const bool previous=cube && environment.previousAvailable && environment.previousEnvironment && environment.previousValidity && environment.previousPosition;
        const auto& currentOrigin=environment.publishedProbeOrigin;const auto& oldOrigin=environment.previousProbeOrigin;
        D3D11_VIEWPORT viewport{};UINT viewportCount=1;context->RSGetViewports(&viewportCount,&viewport);
        const std::array<float,24> parameters{
            environment.transitionWeight,previous?1.0f:0.0f,cube?1.0f:0.0f,reflectionStrength_.load(),
            currentOrigin.position.x,currentOrigin.position.y,currentOrigin.position.z,currentOrigin.valid?1.0f:0.0f,
            oldOrigin.position.x,oldOrigin.position.y,oldOrigin.position.z,oldOrigin.valid?1.0f:0.0f,
            reflectionDistance_.load(),0,0,0,
            fx?1.0f:0.0f,reflectionFlatness_.load(),reflectionRoughness_.load()*8,scene?1.0f:0.0f,
            viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height};
        if(!reflectionUploaded_ || parameters!=uploadedReflection_) {
            context->UpdateSubresource(reflectionParameters_.Get(),0,nullptr,parameters.data(),0,0);uploadedReflection_=parameters;reflectionUploaded_=true;
        }
        return {context,{cube?environment.publishedEnvironment:nullptr,cube?environment.publishedValidity:nullptr,cube?environment.publishedPosition:nullptr,
            previous?environment.previousEnvironment:nullptr,previous?environment.previousValidity:nullptr,previous?environment.previousPosition:nullptr,
            reflectionParameterView_.Get(),scene?colour.Get():nullptr,scene?depth.Get():nullptr},reflectionSampler_.Get()};
    }
    ScopedBindings Runtime::scopeDraw(ID3D11DeviceContext* context,bool water) noexcept
    {
        if(!water || !context || context!=context_.Get()) return {nullptr,{}};
        const auto scale=scale_.load(std::memory_order_relaxed);
        if(!requested() || !parallax_.load(std::memory_order_relaxed) || scale<=0 || !device_ || (failed_ && ++missingDraws_%60!=0)) return {context,{}};
        std::array<ComPtr<ID3D11Buffer>,2> source;
        for(UINT i=0;i<2;i++) context->VSGetConstantBuffers(i+1,1,&source[i]);
        if(!source[0] || !source[1]) return {context,{}};
        std::array<D3D11_BUFFER_DESC,2> description{};for(UINT i=0;i<2;i++) source[i]->GetDesc(&description[i]);
        if(description[0].ByteWidth<48 || description[1].ByteWidth<240) return {context,{}};
        if(!publishParameters() || !ensureBuffer(buffers_[1],views_[1],description[0].ByteWidth) || !ensureBuffer(buffers_[2],views_[2],description[1].ByteWidth)) {
            failed_=true;missingDraws_=0;
            if(!loggedFailure_) {loggedFailure_=true;logging::error("Water parallax GPU allocation failed; native water sampling remains active while resources recover.");}
            return {context,{}};
        }
        failed_=false;
        for(UINT i=0;i<2;i++) context->CopyResource(buffers_[i+1].Get(),source[i].Get());
        return {context,{views_[0].Get(),views_[1].Get(),views_[2].Get()}};
    }
}
