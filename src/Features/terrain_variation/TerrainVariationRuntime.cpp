#include "Features/terrain_variation/TerrainVariationRuntime.h"
#include "render/BSDFPrePassShaderHook.h"
#include "support/Logger.h"
#include <array>
namespace csah::terrain_variation
{
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* parameters) noexcept:context_(context)
    {
        if(context_) {
            context_->PSGetShaderResources(56,1,&previous_);
            if(previous_.Get()==parameters) {context_=nullptr;return;}
            context_->PSSetShaderResources(56,1,&parameters);
        }
    }
    ScopedBindings::~ScopedBindings() {if(context_) {auto* previous=previous_.Get();context_->PSSetShaderResources(56,1,&previous);}}
    Runtime& Runtime::get() noexcept {static Runtime runtime;return runtime;}
    void Runtime::applySettings(const Settings& settings) noexcept
    {
        lodEnabled_.store(settings.lodEnabled,std::memory_order_relaxed);
        enabled_.store(settings.enabled,std::memory_order_release);
        revision_.fetch_add(1,std::memory_order_release);
        render::setDFPrePassTerrainVariationEnabled(settings.enabled);
    }
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {
        view_.Reset();parameters_.Reset();device_=device;context_=context;
        published_=0;resourceAttempts_=0;missingResourceDraws_=0;
        if(requested()) createParameters();
    }
    void Runtime::createParameters() noexcept
    {
        if(!device_ || !context_) return;
        ++resourceAttempts_;
        D3D11_BUFFER_DESC description{};description.ByteWidth=16;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=1;
        if(FAILED(device_->CreateBuffer(&description,nullptr,&parameters_)) || FAILED(device_->CreateShaderResourceView(parameters_.Get(),&view,&view_))) {
            view_.Reset();parameters_.Reset();if(resourceAttempts_==1) logging::error("Terrain variation parameter allocation failed; native sampling remains active.");return;
        }
        published_=0;publish();
    }
    void Runtime::publish() noexcept
    {
        if(!context_ || !parameters_) return;
        const auto revision=revision_.load(std::memory_order_acquire);
        if(revision==published_) return;
        const std::array<float,4> data{requested()?1.0f:0.0f,lodEnabled_.load(std::memory_order_relaxed)?1.0f:0.0f,0,0};
        context_->UpdateSubresource(parameters_.Get(),0,nullptr,data.data(),0,0);published_=revision;
    }
    ScopedBindings Runtime::scopeDraw(ID3D11DeviceContext* context,bool material) noexcept
    {
        if(!material || context!=context_.Get()) return {nullptr,nullptr};
        if(requested() && !view_ && (++missingResourceDraws_%60)==1) createParameters();
        publish();return {context,requested()?view_.Get():nullptr};
    }
}
