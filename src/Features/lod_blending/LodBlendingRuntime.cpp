#include "Features/lod_blending/LodBlendingRuntime.h"
#include "Features/lod_blending/LodClassification.h"
#include "render/BSDFPrePassShaderHook.h"
#include "support/Logger.h"
#include <Windows.h>
#include <cstring>

namespace csah::lod_blending
{
    namespace
    {
        thread_local bool objectLod{};
        template<class T> T read(const void* object,std::size_t offset) noexcept
        {
            T value{};std::memcpy(&value,static_cast<const std::byte*>(object)+offset,sizeof(value));return value;
        }
        bool classify(void* pass,const std::byte* image) noexcept
        {
            if(!pass) return false;
            auto* geometry=read<void*>(pass,0x18);
            if(!geometry) return false;
            auto* property=read<void*>(geometry,0x178);
            if(!property || (read<std::uint64_t>(property,0x30)&(1ull<<33))) return false;
            // VR scene initialization 140D82920 attaches LandAndObjectLOD as
            // LODRoot's first child. 140D82550 independently reads that child.
            if(!image) return false;
            auto* root=read<void*>(image,0x05B042D0);
            if(!root || read<std::uint16_t>(root,0x172)==0) return false;
            auto* children=read<void**>(root,0x168);
            if(!children || !children[0]) return false;
            const auto* landRoot=children[0];
            // NiAVObject::SetParent 141C23D20 and UpdateWorldData 141C23740
            // independently establish the non-owning parent link at +0x28.
            return beneathRoot(read<void*>(geometry,0x28),landRoot,
                [](const void* node) noexcept {return read<void*>(node,0x28);});
        }
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* parameters) noexcept:context_(context)
    {
        if(context_) {
            context_->PSGetShaderResources(55,1,&previous_);
            if(previous_.Get()==parameters) {context_=nullptr;return;}
            context_->PSSetShaderResources(55,1,&parameters);
        }
    }
    ScopedBindings::~ScopedBindings() {if(context_) {auto* prior=previous_.Get();context_->PSSetShaderResources(55,1,&prior);}}
    Runtime& Runtime::get() noexcept {static Runtime value;return value;}
    void Runtime::applySettings(const Settings& value) noexcept
    {
        const auto safe=sanitize(value);
        terrainBrightness_.store(safe.terrainBrightness,std::memory_order_relaxed);terrainGamma_.store(safe.terrainGamma,std::memory_order_relaxed);
        objectBrightness_.store(safe.objectBrightness,std::memory_order_relaxed);objectGamma_.store(safe.objectGamma,std::memory_order_relaxed);
        const bool active=lod_blending::requested(safe);
        objectRequested_.store(safe.objectBrightness!=1.0f || safe.objectGamma!=1.0f,std::memory_order_relaxed);
        requested_.store(active,std::memory_order_release);revision_.fetch_add(1,std::memory_order_release);
        render::setDFPrePassLodBlendingEnabled(active);
    }
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {
        views_={};buffers_={};device_=device;context_=context;published_=0;objectLod=false;
        image_=reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        resourceAttempts_=0;missingResourceDraws_=0;
        createParameters();
    }
    void Runtime::createParameters() noexcept
    {
        if(!device_ || !context_) return;
        ++resourceAttempts_;
        auto* device=device_.Get();
        D3D11_BUFFER_DESC description{};description.ByteWidth=32;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=2;
        for(unsigned i=0;i<2;++i) {
            if(FAILED(device->CreateBuffer(&description,nullptr,&buffers_[i])) || FAILED(device->CreateShaderResourceView(buffers_[i].Get(),&view,&views_[i]))) {
                views_={};buffers_={};if(resourceAttempts_==1) logging::error("LOD matching parameter allocation failed; native colour remains active.");return;
            }
        }
        publish();
    }
    void Runtime::publish() noexcept
    {
        if(!context_ || !buffers_[0] || !buffers_[1]) return;
        const auto revision=revision_.load(std::memory_order_acquire);
        if(revision==published_) return;
        std::array<float,8> data{terrainBrightness_.load(std::memory_order_relaxed),terrainGamma_.load(std::memory_order_relaxed),
            objectBrightness_.load(std::memory_order_relaxed),objectGamma_.load(std::memory_order_relaxed),requested()?1.0f:0.0f,0,0,0};
        for(unsigned i=0;i<2;++i) {data[5]=static_cast<float>(i);context_->UpdateSubresource(buffers_[i].Get(),0,nullptr,data.data(),0,0);}
        published_=revision;
    }
    void Runtime::beginGeometry(void* pass) noexcept {objectLod=requested() && objectRequested_.load(std::memory_order_relaxed) && classify(pass,image_);}
    void Runtime::endGeometry() noexcept {objectLod=false;}
    ScopedBindings Runtime::scopeDraw(ID3D11DeviceContext* context,bool material) noexcept
    {
        if(!material || context!=context_.Get()) return {nullptr,nullptr};
        if(requested() && !views_[0] && (++missingResourceDraws_%60)==1) createParameters();
        publish();return {context,requested()?views_[objectLod?1:0].Get():nullptr};
    }
}
