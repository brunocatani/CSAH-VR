#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>

namespace csah::skylighting
{
    // The private precipitation capture uses conventional depth: clear=1,
    // LESS_EQUAL depth writes. A receiver is visible when it is in front.
    inline constexpr auto kOcclusionComparison = D3D11_COMPARISON_LESS_EQUAL;

    class ScopedDepthRead final
    {
    public:
        ScopedDepthRead(ID3D11DeviceContext* context, ID3D11Resource* source) noexcept : context_(context)
        {
            if (!context_ || !source) return;
            std::array<ID3D11RenderTargetView*,8> views{};
            context_->OMGetRenderTargets(8,views.data(),depth_.GetAddressOf());
            for (UINT i=0;i<8;++i) { targets_[i].Attach(views[i]); if(views[i]) count_=i+1; }
            if (!depth_) return;
            Microsoft::WRL::ComPtr<ID3D11Resource> bound;
            depth_->GetResource(&bound);
            if (bound.Get()!=source) return;
            context_->OMSetRenderTargetsAndUnorderedAccessViews(count_,views.data(),nullptr,
                0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
            detached_=true;
        }
        ~ScopedDepthRead()
        {
            if (!detached_) return;
            std::array<ID3D11RenderTargetView*,8> views{};
            for (UINT i=0;i<8;++i) views[i]=targets_[i].Get();
            context_->OMSetRenderTargetsAndUnorderedAccessViews(count_,views.data(),depth_.Get(),
                0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        }
        ScopedDepthRead(const ScopedDepthRead&)=delete;
        ScopedDepthRead& operator=(const ScopedDepthRead&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,8> targets_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_;
        UINT count_{};
        bool detached_{};
    };
}
