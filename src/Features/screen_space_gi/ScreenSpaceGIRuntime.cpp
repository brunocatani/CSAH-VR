#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "render/ComputeStateScope.h"
#include "render/Fo4VrCamera.h"
#include "support/Logger.h"
#include "ScreenSpaceGIShaders.h"
#include <algorithm>
#include <cmath>
namespace csah::screen_space_gi
{
    using Microsoft::WRL::ComPtr;
    namespace
    {
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
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* context,const std::array<ID3D11ShaderResourceView*,8>& views) noexcept:context_(context)
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,8> previous{};
        context_->PSGetShaderResources(57,8,previous.data());
        for(std::size_t i=0;i<8;i++) previous_[i].Attach(previous[i]);
        context_->PSSetShaderResources(57,8,views.data());
    }
    ScopedBindings::~ScopedBindings()
    {
        if(!context_) return;
        std::array<ID3D11ShaderResourceView*,8> previous{};
        for(std::size_t i=0;i<8;i++) previous[i]=previous_[i].Get();
        context_->PSSetShaderResources(57,8,previous.data());
    }
    Runtime& Runtime::get() noexcept {static Runtime runtime;return runtime;}
    void Runtime::applySettings(const Settings& value) noexcept
    {
        const auto s=sanitize(value);
        gi_.store(s.indirectLight);specularEnabled_.store(s.specular);temporal_.store(s.temporal);
        radius_.store(s.radius);thickness_.store(s.thickness);aoPower_.store(s.aoPower);strength_.store(s.strength);
        enabled_.store(s.enabled,std::memory_order_release);resetHistory();
    }
    void Runtime::onDeviceCreated(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
    {
        device_=device;context_=context;shaders_={};constants_.Reset();parameterView_.Reset();previousCamera_.Reset();ambientParameters_.Reset();ambientParameterView_.Reset();
        positions_={};normals_={};diffuse_={};specular_={};rawDiffuse_={};rawSpecular_={};radiance_={};
        frame_=preparedFrame_=radianceFrame_=nextRetry_=0;width_=height_=current_=0;ready_=historyValid_=loggedFailure_=loggedActive_=false;
        resetHistory();
    }
    void Runtime::beginFrame() noexcept
    {
        ++frame_;
        historyValid_=requested() && radianceFrame_+1==frame_ && preparedFrame_+1==frame_;
        if(reset_.exchange(false,std::memory_order_acq_rel)) historyValid_=false;
        current_^=1;ready_=false;
    }
    bool Runtime::createTexture(Texture& value,DXGI_FORMAT format,UINT width,UINT height) noexcept
    {
        value={};D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=1;d.ArraySize=2;
        d.Format=format;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        return SUCCEEDED(device_->CreateTexture2D(&d,nullptr,&value.texture)) &&
            SUCCEEDED(device_->CreateShaderResourceView(value.texture.Get(),nullptr,&value.view)) &&
            SUCCEEDED(device_->CreateUnorderedAccessView(value.texture.Get(),nullptr,&value.output));
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
        if(width==width_ && height==height_ && constants_ && parameterView_ && shaders_[3] && radiance_.output) return true;
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
            for(UINT i=0;i<2;i++) {
                if(!createTexture(positions_[i],DXGI_FORMAT_R32G32B32A32_FLOAT,width,height) ||
                    !createTexture(normals_[i],DXGI_FORMAT_R16G16B16A16_FLOAT,width,height) ||
                    !createTexture(diffuse_[i],DXGI_FORMAT_R16G16B16A16_FLOAT,width,height) ||
                    !createTexture(specular_[i],DXGI_FORMAT_R16G16B16A16_FLOAT,width,height)) return false;
            }
            return createTexture(rawDiffuse_,DXGI_FORMAT_R16G16B16A16_FLOAT,width,height) &&
                createTexture(rawSpecular_,DXGI_FORMAT_R16G16B16A16_FLOAT,width,height) &&
                createTexture(radiance_,DXGI_FORMAT_R16G16B16A16_FLOAT,width,height);
        };
        if(!create()) return failAllocation();
        width_=width;height_=height;nextRetry_=0;
        return true;
    }
    void Runtime::dispatch(ID3D11ComputeShader* shader,const std::array<ID3D11ShaderResourceView*,8>& inputs,
        ID3D11UnorderedAccessView* first,ID3D11UnorderedAccessView* second) noexcept
    {
        const std::array<ID3D11UnorderedAccessView*,2> outputs{first,second},noOutputs{};
        const std::array<ID3D11ShaderResourceView*,8> noInputs{};
        context_->CSSetShaderResources(0,8,inputs.data());context_->CSSetUnorderedAccessViews(0,2,outputs.data(),nullptr);
        context_->CSSetShader(shader,nullptr,0);context_->Dispatch((width_+7)/8,(height_+7)/8,2);
        context_->CSSetUnorderedAccessViews(0,2,noOutputs.data(),nullptr);context_->CSSetShaderResources(0,8,noInputs.data());
    }
    bool Runtime::prepareAmbient(ID3D11DeviceContext* context) noexcept
    {
        if(preparedFrame_==frame_) return ready_;
        if(frame_<nextRetry_) return false;
        std::array<ComPtr<ID3D11ShaderResourceView>,3> inputs;
        for(UINT i=0;i<3;i++) context->PSGetShaderResources(i+1,1,inputs[i].GetAddressOf());
        D3D11_TEXTURE2D_DESC normal{},depth{},material{};
        if(!textureDescription(inputs[0].Get(),normal) || !textureDescription(inputs[1].Get(),material) || !textureDescription(inputs[2].Get(),depth) ||
            normal.Format!=DXGI_FORMAT_R16G16_UNORM || material.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
            normal.Width!=depth.Width || normal.Height!=depth.Height || material.Width!=depth.Width || material.Height!=depth.Height ||
            depth.Width%2 || depth.Width<=depth.Height) return false;
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
        if(count!=1 || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.Width<16 || viewport.Height<8 ||
            viewport.TopLeftX<0 || viewport.TopLeftY<0 || viewport.TopLeftX+viewport.Width>depth.Width || viewport.TopLeftY+viewport.Height>depth.Height) return false;
        const auto width=static_cast<UINT>(std::ceil(viewport.Width*.25f)),height=static_cast<UINT>(std::ceil(viewport.Height*.5f));
        ComPtr<ID3D11Buffer> pass,stereo,camera;
        context->PSGetConstantBuffers(2,1,&pass);context->PSGetConstantBuffers(8,1,&stereo);context->PSGetConstantBuffers(12,1,&camera);
        if(!pass || !stereo || !camera) return false;
        D3D11_BUFFER_DESC d{},p{};camera->GetDesc(&d);pass->GetDesc(&p);
        if(d.ByteWidth<sizeof(render::Fo4VrFrameBuffer)||p.ByteWidth<30*16) return false;
        if(!ensureResources(width,height)) return false;
        if(ambientParameters_) {D3D11_BUFFER_DESC old{};ambientParameters_->GetDesc(&old);if(old.ByteWidth!=p.ByteWidth) {ambientParameters_.Reset();ambientParameterView_.Reset();}}
        if(!ambientParameters_) {
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=p.ByteWidth;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device_->CreateBuffer(&bd,nullptr,&ambientParameters_))) return failAllocation();
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=p.ByteWidth/16;
            if(FAILED(device_->CreateShaderResourceView(ambientParameters_.Get(),&view,&ambientParameterView_))) {ambientParameters_.Reset();return failAllocation();}
        }
        context->CopyResource(ambientParameters_.Get(),pass.Get());
        if(previousCamera_) {D3D11_BUFFER_DESC old{};previousCamera_->GetDesc(&old);if(old.ByteWidth!=d.ByteWidth) {previousCamera_.Reset();historyValid_=false;}}
        if(!previousCamera_) {d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device_->CreateBuffer(&d,nullptr,&previousCamera_))) return failAllocation();
            context->CopyResource(previousCamera_.Get(),camera.Get());historyValid_=false;}
        if(uploaded_.originX!=viewport.TopLeftX || uploaded_.originY!=viewport.TopLeftY ||
            uploaded_.viewportWidth!=viewport.Width || uploaded_.viewportHeight!=viewport.Height) historyValid_=false;
        uploaded_={width,height,static_cast<UINT>(frame_),historyValid_?1u:0u,viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height,
            radius_.load(),thickness_.load(),aoPower_.load(),strength_.load(),gi_.load()?1u:0u,specularEnabled_.load()?1u:0u,temporal_.load()?1u:0u,0};
        context->UpdateSubresource(constants_.Get(),0,nullptr,&uploaded_,0,0);
        // Separate typed buffer for pixel consumers; never bind a CB as an SRV.
        ComPtr<ID3D11Resource> parameters;
        if(parameterView_) parameterView_->GetResource(&parameters);
        if(!parameters) {
            D3D11_BUFFER_DESC pd{};pd.ByteWidth=sizeof(Constants);pd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Buffer> buffer;if(FAILED(device_->CreateBuffer(&pd,nullptr,&buffer))) return failAllocation();
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=4;
            if(FAILED(device_->CreateShaderResourceView(buffer.Get(),&view,&parameterView_))) return failAllocation();parameters=buffer;
        }
        context->UpdateSubresource(parameters.Get(),0,nullptr,&uploaded_,0,0);
        render::ScopedComputeState restore(context,{.firstShaderResource=0,.shaderResourceCount=8,.firstUnorderedAccess=0,.unorderedAccessCount=2,.firstConstantBuffer=2,.constantBufferCount=12});
        if(!restore.captured()) return false;
        ID3D11Buffer* cb=pass.Get();context->CSSetConstantBuffers(2,1,&cb);cb=stereo.Get();context->CSSetConstantBuffers(8,1,&cb);
        cb=previousCamera_.Get();context->CSSetConstantBuffers(10,1,&cb);cb=camera.Get();context->CSSetConstantBuffers(12,1,&cb);
        cb=constants_.Get();context->CSSetConstantBuffers(13,1,&cb);
        dispatch(shaders_[0].Get(),{inputs[0].Get(),inputs[2].Get(),inputs[1].Get()},positions_[current_].output.Get(),normals_[current_].output.Get());
        const auto previous=current_^1;
        dispatch(shaders_[1].Get(),{positions_[current_].view.Get(),normals_[current_].view.Get(),positions_[previous].view.Get(),normals_[previous].view.Get(),radiance_.view.Get()},rawDiffuse_.output.Get(),rawSpecular_.output.Get());
        dispatch(shaders_[2].Get(),{positions_[current_].view.Get(),normals_[current_].view.Get(),positions_[previous].view.Get(),normals_[previous].view.Get(),rawDiffuse_.view.Get(),rawSpecular_.view.Get(),diffuse_[previous].view.Get(),specular_[previous].view.Get()},diffuse_[current_].output.Get(),specular_[current_].output.Get());
        context->CopyResource(previousCamera_.Get(),camera.Get());
        preparedFrame_=frame_;ready_=true;
        if(!loggedActive_) {loggedActive_=true;logging::info("Screen Space GI active: two isolated {}x{} eye slices, AO={}, indirect={}, experimentalSpecular={}, temporal={}; current geometry and prior-frame surface lighting.",width_,height_,uploaded_.aoPower,uploaded_.gi!=0,uploaded_.specular!=0,uploaded_.temporal!=0);}
        return true;
    }
    ScopedBindings Runtime::scopeAmbient(ID3D11DeviceContext* context,bool ambient,ID3D11ShaderResourceView* authored) noexcept
    {
        if(!ambient || context!=context_.Get()) return {nullptr,{}};
        if(!requested() || !prepareAmbient(context)) return {context,{}};
        ComPtr<ID3D11ShaderResourceView> albedo;context->PSGetShaderResources(0,1,&albedo);
        return {context,{diffuse_[current_].view.Get(),specular_[current_].view.Get(),parameterView_.Get(),albedo.Get(),authored,positions_[current_].view.Get(),normals_[current_].view.Get(),ambientParameterView_.Get()}};
    }
    void Runtime::captureRadiance(ID3D11DeviceContext* context,bool tiledActive,ID3D11ShaderResourceView* authored) noexcept
    {
        if(!requested() || context!=context_.Get() || !ready_ || preparedFrame_!=frame_ || radianceFrame_==frame_) return;
        const std::array<UINT,4> slots{0,4,5,11};std::array<ComPtr<ID3D11ShaderResourceView>,4> inputs;
        for(std::size_t i=0;i<inputs.size();i++) context->PSGetShaderResources(slots[i],1,&inputs[i]);
        D3D11_TEXTURE2D_DESC albedo{},emissive{},diffuse{},tiled{};
        if(!textureDescription(inputs[0].Get(),albedo)||!textureDescription(inputs[1].Get(),emissive)||!textureDescription(inputs[2].Get(),diffuse) ||
            albedo.Width!=diffuse.Width || albedo.Height!=diffuse.Height || emissive.Width!=albedo.Width || emissive.Height!=albedo.Height) return;
        if(tiledActive && (!textureDescription(inputs[3].Get(),tiled) || tiled.Width!=albedo.Width || tiled.Height!=albedo.Height)) return;
        uploaded_.tiled=tiledActive?1u:0u;
        ComPtr<ID3D11Buffer> pass;context->PSGetConstantBuffers(2,1,&pass);if(!pass) return;
        render::ScopedComputeState restore(context,{.firstShaderResource=0,.shaderResourceCount=8,.firstUnorderedAccess=0,.unorderedAccessCount=2,.firstConstantBuffer=2,.constantBufferCount=12});
        if(!restore.captured()) return;
        context->UpdateSubresource(constants_.Get(),0,nullptr,&uploaded_,0,0);
        auto* cb=constants_.Get();context->CSSetConstantBuffers(13,1,&cb);cb=pass.Get();context->CSSetConstantBuffers(2,1,&cb);
        dispatch(shaders_[3].Get(),{inputs[0].Get(),inputs[1].Get(),inputs[2].Get(),inputs[3].Get(),positions_[current_].view.Get(),diffuse_[current_].view.Get(),authored},radiance_.output.Get());
        radianceFrame_=frame_;
    }
}
