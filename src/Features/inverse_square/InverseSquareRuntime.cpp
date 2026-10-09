#include "Features/inverse_square/InverseSquareRuntime.h"
#include "support/Logger.h"
#include "InverseSquareShaders.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace csah::inverse_square
{
    namespace {constexpr GUID shaderTag{0x73cc4ce9,0x47ba,0x4904,{0xab,0x0e,0xa3,0x18,0x2c,0xd3,0x83,0x65}};constexpr UINT tagValue=0x49534c31;}
    ShaderSelection selectShader(const void* data,std::size_t size,bool compute) noexcept
    {
        if(data) for(const auto& shader:kInverseSquareShaders) if(compute==shader.compute && size==shader.originalSize && std::memcmp(data,shader.original,size)==0) return {shader.replacement,shader.replacementSize,true};
        return {data,size,false};
    }
    bool tagShader(ID3D11DeviceChild* shader) noexcept {return shader && SUCCEEDED(shader->SetPrivateData(shaderTag,sizeof(tagValue),&tagValue));}
    bool taggedShader(ID3D11DeviceChild* shader) noexcept
    {
        UINT value{},size=sizeof(value);return shader && SUCCEEDED(shader->GetPrivateData(shaderTag,&size,&value)) && size==sizeof(value) && value==tagValue;
    }
    ScopedBindings::ScopedBindings(ID3D11DeviceContext* c,ID3D11ShaderResourceView* view,bool compute) noexcept:context_(c),compute_(compute)
    {
        if(!c) return;
        if(compute_) {c->CSGetShaderResources(78,1,&previous_);c->CSSetShaderResources(78,1,&view);}
        else {c->PSGetShaderResources(78,1,&previous_);c->PSSetShaderResources(78,1,&view);}
    }
    ScopedBindings::~ScopedBindings()
    {
        if(!context_) return;auto* previous=previous_.Get();
        if(compute_) context_->CSSetShaderResources(78,1,&previous);else context_->PSSetShaderResources(78,1,&previous);
    }
    Runtime& Runtime::get() noexcept {static Runtime r;return r;}
    void Runtime::applySettings(const Settings& s) noexcept
    {
        size_.store(std::isfinite(s.size)?std::clamp(s.size,.25f,16.0f):1.41421356237f);
        intensity_.store(std::isfinite(s.intensity)?std::clamp(s.intensity,0.0f,8.0f):1);
        enabled_.store(s.enabled,std::memory_order_release);
    }
    void Runtime::onDeviceCreated(ID3D11Device* d,ID3D11DeviceContext* c) noexcept
    {
        device_=d;context_=c;parameters_.Reset();view_.Reset();uploadedValid_=loggedFailure_=false;retry_=0;
    }
    ScopedBindings Runtime::scope(ID3D11DeviceContext* c,bool compute,bool active) noexcept
    {
        if(!active || !c || c!=context_.Get()) return {nullptr,nullptr,compute};
        if(!device_ || !enabled_.load(std::memory_order_acquire) || !dispatchReady_.load(std::memory_order_acquire)) return {c,nullptr,compute};
        if(!view_) {
            if(retry_ && --retry_) return {c,nullptr,compute};
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;vd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;vd.Buffer.NumElements=1;
            if(FAILED(device_->CreateBuffer(&bd,nullptr,parameters_.ReleaseAndGetAddressOf())) || FAILED(device_->CreateShaderResourceView(parameters_.Get(),&vd,view_.ReleaseAndGetAddressOf()))) {
                retry_=60;if(!loggedFailure_) {loggedFailure_=true;logging::error("Inverse-square parameter allocation failed; native local-light falloff retained.");}return {c,nullptr,compute};
            }
            uploadedValid_=false;
        }
        const std::array<float,4> data{1,size_.load(),intensity_.load(),0};
        if(!uploadedValid_ || data!=uploaded_) {c->UpdateSubresource(parameters_.Get(),0,nullptr,data.data(),0,0);uploaded_=data;uploadedValid_=true;}
        return {c,view_.Get(),compute};
    }
}
