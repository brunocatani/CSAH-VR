#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"
#include "support/ReadableMemory.h"
#include "TerrainShadowUpdate.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace csah::terrain_shadows
{
    namespace
    {
        template<class T> bool read(std::uintptr_t address,T& result) noexcept
        {
            MEMORY_BASIC_INFORMATION m{};
            if(!address || !VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
            const auto start=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
            if(address<start || sizeof(T)>m.RegionSize || address-start>m.RegionSize-sizeof(T)) return false;
            std::memcpy(&result,reinterpret_cast<void*>(address),sizeof(T));return true;
        }
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* c,ID3D11ShaderResourceView* s,ID3D11ShaderResourceView* p) noexcept:context_(c)
    {
        if(!c) return;
        ID3D11ShaderResourceView* old[2]{};c->PSGetShaderResources(76,2,old);
        for(unsigned i=0;i<2;i++) previous_[i].Attach(old[i]);
        ID3D11ShaderResourceView* next[]{s,p};c->PSSetShaderResources(76,2,next);
    }
    ScopedBindings::~ScopedBindings()
    {
        if(context_) {ID3D11ShaderResourceView* old[]{previous_[0].Get(),previous_[1].Get()};context_->PSSetShaderResources(76,2,old);}
    }
    Runtime& Runtime::get() noexcept {static Runtime r;return r;}
    void Runtime::applySettings(const Settings& s) noexcept
    {
        strength_.store(std::isfinite(s.strength)?std::clamp(s.strength,0.0f,1.0f):1);
        enabled_.store(s.enabled,std::memory_order_release);if(!s.enabled) resetWorld();
    }
    void Runtime::loadMaps(const std::filesystem::path& directory) noexcept
    {
        try {
            if(!std::filesystem::exists(directory)) {logging::info("Terrain shadows: no heightmap directory at '{}'.",directory.string());return;}
            for(const auto& entry:std::filesystem::directory_iterator(directory)) {
                if(entry.path().extension()!=L".cshf") continue;
                try {
                    struct Header {char magic[8];std::uint32_t width,height;std::int32_t west,south;std::uint32_t samples,reserved;};
                    static_assert(sizeof(Header)==32);
                    std::ifstream file(entry.path(),std::ios::binary);Header h{};file.read(reinterpret_cast<char*>(&h),sizeof(h));
                    const auto count=std::uint64_t(h.width)*h.height;
                    if(!file || std::memcmp(h.magic,"CSAHHF01",8) || h.width<2 || h.height<2 || h.width>16384 || h.height>16384 ||
                        (h.samples!=8 && h.samples!=16 && h.samples!=32) || h.reserved ||
                        std::filesystem::file_size(entry.path())!=sizeof(h)+count*sizeof(float)) throw std::runtime_error("invalid heightmap header/length");
                    Map map;map.name=entry.path().stem().string();map.width=h.width;map.height=h.height;map.west=h.west;map.south=h.south;map.samples=h.samples;
                    if(map.name.empty() || map.name.size()>=256 || std::any_of(map.name.begin(),map.name.end(),[](unsigned char c){return !(c>='0'&&c<='9') && !(c>='a'&&c<='z') && !(c>='A'&&c<='Z') && c!='_' && c!='-';})) throw std::runtime_error("invalid worldspace editor ID");
                    map.pixels.resize(static_cast<std::size_t>(count));file.read(reinterpret_cast<char*>(map.pixels.data()),static_cast<std::streamsize>(count*sizeof(float)));
                    if(!file || std::any_of(map.pixels.begin(),map.pixels.end(),[](float f){return !std::isfinite(f);})) throw std::runtime_error("invalid height samples");
                    if(std::any_of(maps_.begin(),maps_.end(),[&](const Map& m){return _stricmp(m.name.c_str(),map.name.c_str())==0;})) throw std::runtime_error("duplicate worldspace");
                    maps_.push_back(std::move(map));
                } catch(const std::exception& e) {logging::error("Terrain heightmap '{}' rejected: {}",entry.path().string(),e.what());}
            }
            logging::info("Terrain shadows loaded {} worldspace heightmaps.",maps_.size());
        } catch(const std::exception& e) {logging::error("Terrain heightmap discovery failed: {}",e.what());}
    }
    void Runtime::captureWorld(bool ready) noexcept
    {
        const auto epoch=epoch_.load(std::memory_order_acquire);
        const auto publish=[&](std::uint32_t index){selected_.store((std::uint64_t(epoch)<<32)|index,std::memory_order_release);};
        if(!ready || !requested()) {publish(0);return;}
        static const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uintptr_t tes{},interior{},world{};
        // TES +58/+D8: 1400F5360 and 1400F8A10. Parent +188/flags +8A:
        // 14047C6E0 and 14047C540. Land inheritance is bit 0, not an ordinal mask.
        if(!read(base+0x05B042C0,tes) || !tes || !read(tes+0x58,interior) || interior || !read(tes+0xd8,world)) {publish(0);return;}
        for(unsigned depth=0;world && depth<64;depth++) {
            std::uintptr_t parent{};std::uint8_t flags{};
            if(!read(world+0x188,parent) || !read(world+0x8a,flags)) break;
            if(parent && (flags&1)) {world=parent;continue;}
            // WRLD vtable 142CBFC98 slot 3A -> 140479BD0 returns +208 string;
            // native terrain ctor 1407AF59F consumes that same slot as map name.
            std::uintptr_t name{};std::array<char,256> buffer{};
            if(!read(world+0x208,name) || !name) break;
            if(!support::readCString(name,buffer.data(),buffer.size())) break;
            for(std::size_t i=0;i<maps_.size();i++) if(_stricmp(buffer.data(),maps_[i].name.c_str())==0) {publish(static_cast<std::uint32_t>(i+1));return;}
            break;
        }
        publish(0);
    }
    void Runtime::onDeviceCreated(ID3D11Device* d,ID3D11DeviceContext* c) noexcept
    {
        device_=d;context_=c;parametersUploaded_=false;loadedSelection_=0;updatedFrame_=~0ull;retry_=0;loggedFailure_=false;
        height_.Reset();shadow_.Reset();heightView_.Reset();shadowView_.Reset();shadowWrite_.Reset();parameters_.Reset();parameterView_.Reset();updateParameters_.Reset();compute_.Reset();
    }
    bool Runtime::createResources(const Map& m) noexcept
    {
        heightView_.Reset();shadowView_.Reset();shadowWrite_.Reset();height_.Reset();shadow_.Reset();
        if(!compute_ && FAILED(device_->CreateComputeShader(kTerrainShadowUpdate,sizeof(kTerrainShadowUpdate),nullptr,&compute_))) return false;
        D3D11_TEXTURE2D_DESC td{};td.Width=m.width;td.Height=m.height;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{m.pixels.data(),m.width*static_cast<UINT>(sizeof(float)),0};
        if(FAILED(device_->CreateTexture2D(&td,&data,&height_)) || FAILED(device_->CreateShaderResourceView(height_.Get(),nullptr,&heightView_))) return false;
        td.Format=DXGI_FORMAT_R32G32_FLOAT;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device_->CreateTexture2D(&td,nullptr,&shadow_)) || FAILED(device_->CreateShaderResourceView(shadow_.Get(),nullptr,&shadowView_)) || FAILED(device_->CreateUnorderedAccessView(shadow_.Get(),nullptr,&shadowWrite_))) return false;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(!parameters_ && FAILED(device_->CreateBuffer(&bd,nullptr,&parameters_))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;vd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;vd.Buffer.NumElements=2;
        if(!parameterView_ && FAILED(device_->CreateShaderResourceView(parameters_.Get(),&vd,&parameterView_))) return false;
        bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(!updateParameters_ && FAILED(device_->CreateBuffer(&bd,nullptr,&updateParameters_))) return false;
        updatedFrame_=~0ull;return true;
    }
    bool Runtime::update(const Map& m) noexcept
    {
        Ptr<ID3D11Buffer> light,camera;context_->PSGetConstantBuffers(2,1,&light);context_->PSGetConstantBuffers(12,1,&camera);
        if(!light || !camera) return false;
        D3D11_BUFFER_DESC bd{};light->GetDesc(&bd);if(bd.ByteWidth<32) return false;camera->GetDesc(&bd);if(bd.ByteWidth<23*16) return false;
        render::ScopedComputeState state(context_.Get(),{0,1,0,1,0,0,0,13});if(!state.captured()) return false;
        context_->CSSetShader(compute_.Get(),nullptr,0);auto* h=heightView_.Get();context_->CSSetShaderResources(0,1,&h);
        auto* u=shadowWrite_.Get();context_->CSSetUnorderedAccessViews(0,1,&u,nullptr);
        auto* b=updateParameters_.Get();context_->CSSetConstantBuffers(0,1,&b);b=light.Get();context_->CSSetConstantBuffers(2,1,&b);b=camera.Get();context_->CSSetConstantBuffers(12,1,&b);
        const auto dimension=std::max(m.width,m.height);
        for(std::uint32_t slice=0;slice<(dimension+127)/128;slice++) {
            struct {std::uint32_t slice;float spacing;float padding[2];} p{slice,4096.0f/m.samples,{}};
            context_->UpdateSubresource(updateParameters_.Get(),0,nullptr,&p,0,0);context_->Dispatch(dimension,1,1);
        }
        return state.restore();
    }
    ScopedBindings Runtime::scopeDraw(ID3D11DeviceContext* c) noexcept
    {
        if(!c || c!=context_.Get()) return {nullptr,nullptr,nullptr};
        const auto selection=selected_.load(std::memory_order_acquire);const auto index=static_cast<std::uint32_t>(selection);
        if(!requested() || !device_ || selection>>32!=epoch_.load(std::memory_order_acquire) || !index || index>maps_.size()) return {c,nullptr,nullptr};
        const auto& map=maps_[index-1];
        if(loadedSelection_!=selection) {
            if(retry_ && --retry_) return {c,nullptr,nullptr};
            if(!createResources(map)) {retry_=60;if(!loggedFailure_) {loggedFailure_=true;logging::error("Terrain shadow GPU allocation failed; native sunlight remains active.");}return {c,nullptr,nullptr};}
            loadedSelection_=selection;logging::info("Terrain shadows selected '{}' ({} x {}).",map.name,map.width,map.height);
        }
        // Each completed scan uses the same native light/camera. No partial or
        // stale map is published across a world transition, eye, or frame.
        if(updatedFrame_!=frame_) {
            bool updated=false;
            {ScopedBindings unbind(c,nullptr,nullptr);updated=update(map);}
            if(!updated) return {c,nullptr,nullptr};
            updatedFrame_=frame_;
        }
        const std::array<float,8> parameters{float(map.west)*4096,float(map.south)*4096,4096.0f/map.samples,256,
            float(map.width),float(map.height),strength_.load(),1};
        if(!parametersUploaded_ || uploadedParameters_!=parameters) {
            c->UpdateSubresource(parameters_.Get(),0,nullptr,parameters.data(),0,0);
            uploadedParameters_=parameters;parametersUploaded_=true;
        }
        return {c,shadowView_.Get(),parameterView_.Get()};
    }
}
