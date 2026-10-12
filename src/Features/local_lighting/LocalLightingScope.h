#pragma once
#include "Features/pbr/PbrRuntime.h"
#include "Features/basic_wetness/BasicWetnessRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include <array>

namespace csah::local_lighting
{
    // Immediate-context transaction. The established material runtimes own the
    // constants; the native local PS/CS keeps its own G-buffer slots unchanged.
    class ScopedMaterials final
    {
    public:
        ScopedMaterials(ID3D11DeviceContext* context, bool compute, bool enabled) noexcept :
            context_(enabled ? context : nullptr), compute_(compute),
            wetness_(basic_wetness::Runtime::get().scopeDraw(context_, enabled && basic_wetness::Runtime::get().requested())),
            pbr_(pbr::Runtime::get().scopeDraw(context_, enabled && pbr::Runtime::get().requested()))
        {
            if (!context_ || !wetness_.active() || !pbr_.active()) return;
            auto* surface = surface_classification::Runtime::get().shaderResourceView();
            if (!surface) return;
            std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,2> gbuffer;
            if (compute_) {
                context_->CSGetShaderResources(1,1,&gbuffer[0]);
                context_->CSGetShaderResources(4,1,&gbuffer[1]);
            } else {
                context_->PSGetShaderResources(0,1,&gbuffer[0]);
                context_->PSGetShaderResources(2,1,&gbuffer[1]);
            }
            if (!gbuffer[0] || !gbuffer[1]) return;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> authored;
            context_->PSGetShaderResources(45,1,&authored);
            const std::array<ID3D11ShaderResourceView*,4> inputs{authored.Get(),surface,gbuffer[0].Get(),gbuffer[1].Get()};
            for (std::size_t i=0;i<slots_.size();++i) {
                if (compute_) {
                    context_->CSGetShaderResources(slots_[i],1,&previousInputs_[i]);
                    context_->CSSetShaderResources(slots_[i],1,&inputs[i]);
                } else {
                    context_->PSGetShaderResources(slots_[i],1,&previousInputs_[i]);
                    context_->PSSetShaderResources(slots_[i],1,&inputs[i]);
                }
            }
            if (compute_) for (std::size_t i=0;i<constantSlots_.size();++i) {
                Microsoft::WRL::ComPtr<ID3D11Buffer> current;
                context_->PSGetConstantBuffers(constantSlots_[i],1,&current);
                context_->CSGetConstantBuffers(constantSlots_[i],1,&previousConstants_[i]);
                auto* raw=current.Get();context_->CSSetConstantBuffers(constantSlots_[i],1,&raw);
            }
            active_=true;
        }
        ~ScopedMaterials() noexcept
        {
            if (!active_) return;
            for (std::size_t i=0;i<slots_.size();++i) {
                auto* raw=previousInputs_[i].Get();
                if(compute_) context_->CSSetShaderResources(slots_[i],1,&raw);
                else context_->PSSetShaderResources(slots_[i],1,&raw);
            }
            if(compute_) for(std::size_t i=0;i<constantSlots_.size();++i) {
                auto* raw=previousConstants_[i].Get();context_->CSSetConstantBuffers(constantSlots_[i],1,&raw);
            }
        }
        ScopedMaterials(const ScopedMaterials&)=delete;
        ScopedMaterials& operator=(const ScopedMaterials&)=delete;
        [[nodiscard]] bool active() const noexcept {return active_;}
    private:
        static constexpr std::array<UINT,4> slots_{45,47,81,82};
        static constexpr std::array<UINT,2> constantSlots_{7,9};
        ID3D11DeviceContext* context_{};
        bool compute_{},active_{};
        basic_wetness::ScopedDrawBindings wetness_;
        pbr::ScopedDrawBindings pbr_;
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,4> previousInputs_;
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,2> previousConstants_;
    };
}
