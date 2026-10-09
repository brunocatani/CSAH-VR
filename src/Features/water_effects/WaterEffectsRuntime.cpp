#include "Features/water_effects/WaterEffectsRuntime.h"
#include "support/Logger.h"
namespace csah::water_effects
{
    using Microsoft::WRL::ComPtr;
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
    {const auto s=sanitize(value);scale_.store(s.parallaxScale,std::memory_order_relaxed);enabled_.store(s.enabled,std::memory_order_release);}
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {device_=device;context_=context;buffers_={};views_={};missingDraws_=0;publishedScale_=-1;failed_=loggedFailure_=false;}
    bool Runtime::ensureBuffer(Ptr<ID3D11Buffer>& buffer,Ptr<ID3D11ShaderResourceView>& view,UINT bytes) noexcept
    {
        if(buffer && view) {D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);if(d.ByteWidth==bytes) return true;}
        buffer.Reset();view.Reset();D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=bytes/16;
        return SUCCEEDED(device_->CreateBuffer(&d,nullptr,&buffer)) && SUCCEEDED(device_->CreateShaderResourceView(buffer.Get(),&v,&view));
    }
    ScopedBindings Runtime::scopeDraw(ID3D11DeviceContext* context,bool water) noexcept
    {
        if(!water || !context || context!=context_.Get()) return {nullptr,{}};
        const auto scale=scale_.load(std::memory_order_relaxed);
        if(!requested() || scale<=0 || !device_ || (failed_ && ++missingDraws_%60!=0)) return {context,{}};
        std::array<ComPtr<ID3D11Buffer>,2> source;
        for(UINT i=0;i<2;i++) context->VSGetConstantBuffers(i+1,1,&source[i]);
        if(!source[0] || !source[1]) return {context,{}};
        std::array<D3D11_BUFFER_DESC,2> description{};for(UINT i=0;i<2;i++) source[i]->GetDesc(&description[i]);
        if(description[0].ByteWidth<48 || description[1].ByteWidth<240) return {context,{}};
        const bool newParameters=!buffers_[0] || !views_[0];
        if(!ensureBuffer(buffers_[0],views_[0],16) || !ensureBuffer(buffers_[1],views_[1],description[0].ByteWidth) || !ensureBuffer(buffers_[2],views_[2],description[1].ByteWidth)) {
            failed_=true;missingDraws_=0;
            if(!loggedFailure_) {loggedFailure_=true;logging::error("Water parallax GPU allocation failed; native water sampling remains active while resources recover.");}
            return {context,{}};
        }
        failed_=false;
        if(newParameters || scale!=publishedScale_) {
            const std::array<float,4> parameters{1,scale,0,0};
            context->UpdateSubresource(buffers_[0].Get(),0,nullptr,parameters.data(),0,0);publishedScale_=scale;
        }
        for(UINT i=0;i<2;i++) context->CopyResource(buffers_[i+1].Get(),source[i].Get());
        return {context,{views_[0].Get(),views_[1].Get(),views_[2].Get()}};
    }
}
