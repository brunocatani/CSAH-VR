#include "Features/ibl/IblReflectionFreeCapture.h"

#include <d3d11_1.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace csah::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr UINT kEnvironmentCubeCount = 42;
        constexpr UINT kEnvironmentFaceCount = kEnvironmentCubeCount * 6;
        constexpr UINT kEnvironmentSlot = 8;
        constexpr UINT kScreenReflectionSlot = 14;

        [[nodiscard]] bool sameDevice(
            ID3D11DeviceChild* child,
            ID3D11Device* expected) noexcept
        {
            if (!child || !expected) {
                return false;
            }
            ComPtr<ID3D11Device> actual;
            child->GetDevice(&actual);
            return actual.Get() == expected;
        }

        [[nodiscard]] bool describeOutput(
            ID3D11RenderTargetView* outputView,
            ID3D11Device* expectedDevice,
            D3D11_TEXTURE2D_DESC& description) noexcept
        {
            if (!outputView || !expectedDevice ||
                !sameDevice(outputView, expectedDevice)) {
                return false;
            }

            D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
            outputView->GetDesc(&viewDescription);
            if (viewDescription.ViewDimension !=
                    D3D11_RTV_DIMENSION_TEXTURE2D ||
                viewDescription.Texture2D.MipSlice != 0 ||
                viewDescription.Format == DXGI_FORMAT_UNKNOWN) {
                return false;
            }

            ComPtr<ID3D11Resource> resource;
            outputView->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture;
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDesc(&description);
            if (description.Width == 0 || description.Height == 0 ||
                description.MipLevels == 0 || description.ArraySize != 1 ||
                description.SampleDesc.Count != 1 ||
                (description.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                return false;
            }
            description.MipLevels = 1;
            description.Format = ReflectionFreeCaptureResources::captureFormat(viewDescription.Format);
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags =
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            description.CPUAccessFlags = 0;
            description.MiscFlags = 0;
            return true;
        }

        [[nodiscard]] bool sameScratchDescription(
            const D3D11_TEXTURE2D_DESC& left,
            const D3D11_TEXTURE2D_DESC& right) noexcept
        {
            return left.Width == right.Width && left.Height == right.Height &&
                left.MipLevels == right.MipLevels &&
                left.ArraySize == right.ArraySize &&
                left.Format == right.Format &&
                left.SampleDesc.Count == right.SampleDesc.Count &&
                left.SampleDesc.Quality == right.SampleDesc.Quality &&
                left.Usage == right.Usage &&
                left.BindFlags == right.BindFlags &&
                left.CPUAccessFlags == right.CPUAccessFlags &&
                left.MiscFlags == right.MiscFlags;
        }
    }

    bool ReflectionFreeCaptureResources::initialize(
        ID3D11Device* device) noexcept
    {
        reset();
        if (!device) {
            return false;
        }

        ComPtr<ID3D11Texture2D> environmentTexture;
        ComPtr<ID3D11ShaderResourceView> environmentSrv;
        D3D11_TEXTURE2D_DESC environmentDescription{};
        environmentDescription.Width = 1;
        environmentDescription.Height = 1;
        environmentDescription.MipLevels = 1;
        environmentDescription.ArraySize = kEnvironmentFaceCount;
        environmentDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        environmentDescription.SampleDesc.Count = 1;
        environmentDescription.Usage = D3D11_USAGE_IMMUTABLE;
        environmentDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        environmentDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        constexpr std::array<float, 4> zero{};
        std::array<D3D11_SUBRESOURCE_DATA, kEnvironmentFaceCount>
            environmentData{};
        for (auto& subresource : environmentData) {
            subresource.pSysMem = zero.data();
            subresource.SysMemPitch = sizeof(zero);
            subresource.SysMemSlicePitch = sizeof(zero);
        }
        if (FAILED(device->CreateTexture2D(
                &environmentDescription,
                environmentData.data(),
                &environmentTexture))) {
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC environmentViewDescription{};
        environmentViewDescription.Format = environmentDescription.Format;
        environmentViewDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        environmentViewDescription.TextureCubeArray.MostDetailedMip = 0;
        environmentViewDescription.TextureCubeArray.MipLevels = 1;
        environmentViewDescription.TextureCubeArray.First2DArrayFace = 0;
        environmentViewDescription.TextureCubeArray.NumCubes =
            kEnvironmentCubeCount;
        if (FAILED(device->CreateShaderResourceView(
                environmentTexture.Get(),
                &environmentViewDescription,
                &environmentSrv))) {
            return false;
        }

        ComPtr<ID3D11Texture2D> screenReflectionTexture;
        ComPtr<ID3D11ShaderResourceView> screenReflectionSrv;
        D3D11_TEXTURE2D_DESC screenReflectionDescription{};
        screenReflectionDescription.Width = 1;
        screenReflectionDescription.Height = 1;
        screenReflectionDescription.MipLevels = 1;
        screenReflectionDescription.ArraySize = 1;
        screenReflectionDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        screenReflectionDescription.SampleDesc.Count = 1;
        screenReflectionDescription.Usage = D3D11_USAGE_IMMUTABLE;
        screenReflectionDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA screenReflectionData{};
        screenReflectionData.pSysMem = zero.data();
        screenReflectionData.SysMemPitch = sizeof(zero);
        screenReflectionData.SysMemSlicePitch = sizeof(zero);
        if (FAILED(device->CreateTexture2D(
                &screenReflectionDescription,
                &screenReflectionData,
                &screenReflectionTexture)) ||
            FAILED(device->CreateShaderResourceView(
                screenReflectionTexture.Get(),
                nullptr,
                &screenReflectionSrv))) {
            return false;
        }

        device_ = device;
        blackEnvironmentTexture_ = std::move(environmentTexture);
        blackEnvironmentSrv_ = std::move(environmentSrv);
        blackScreenReflectionTexture_ =
            std::move(screenReflectionTexture);
        blackScreenReflectionSrv_ = std::move(screenReflectionSrv);
        return true;
    }

    void ReflectionFreeCaptureResources::reset() noexcept
    {
        depthStates_ = {};
        nextDepthState_ = 0;
        scratchShaderResource_.Reset();
        scratchRenderTarget_.Reset();
        scratchTexture_.Reset();
        scratchDescription_ = {};
        sourceFormat_ = DXGI_FORMAT_UNKNOWN;
        blackScreenReflectionSrv_.Reset();
        blackScreenReflectionTexture_.Reset();
        blackEnvironmentSrv_.Reset();
        blackEnvironmentTexture_.Reset();
        device_.Reset();
    }

    ID3D11DepthStencilState* ReflectionFreeCaptureResources::readOnlyDepthState(
        const D3D11_DEPTH_STENCIL_DESC& source) noexcept
    {
        if (!device_) return nullptr;
        auto description = source;
        description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        description.StencilWriteMask = 0;
        const auto sameFace = [](const auto& a, const auto& b) {
            return a.StencilFailOp == b.StencilFailOp &&
                a.StencilDepthFailOp == b.StencilDepthFailOp &&
                a.StencilPassOp == b.StencilPassOp && a.StencilFunc == b.StencilFunc;
        };
        for (const auto& entry : depthStates_) {
            const auto& saved = entry.description;
            if (entry.state && saved.DepthEnable == description.DepthEnable &&
                saved.DepthFunc == description.DepthFunc &&
                saved.StencilEnable == description.StencilEnable &&
                saved.StencilReadMask == description.StencilReadMask &&
                sameFace(saved.FrontFace, description.FrontFace) &&
                sameFace(saved.BackFace, description.BackFace)) return entry.state.Get();
        }
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
        if (FAILED(device_->CreateDepthStencilState(&description, &state))) return nullptr;
        auto& entry = depthStates_[nextDepthState_++ % depthStates_.size()];
        entry.description = description;
        entry.state = std::move(state);
        return entry.state.Get();
    }

    bool ReflectionFreeCaptureResources::scratchMatches(
        ID3D11RenderTargetView* outputView) const noexcept
    {
        D3D11_TEXTURE2D_DESC required{};
        return scratchTexture_ && scratchRenderTarget_ &&
            scratchShaderResource_ &&
            describeOutput(outputView, device_.Get(), required) &&
            sameScratchDescription(required, scratchDescription_);
    }

    bool ReflectionFreeCaptureResources::prepareScratch(
        ID3D11RenderTargetView* outputView) noexcept
    {
        if (!device_ || !blackEnvironmentSrv_ ||
            !blackScreenReflectionSrv_) {
            return false;
        }
        D3D11_TEXTURE2D_DESC required{};
        if (!describeOutput(outputView, device_.Get(), required)) {
            return false;
        }
        D3D11_RENDER_TARGET_VIEW_DESC sourceView{};
        outputView->GetDesc(&sourceView);
        if (scratchTexture_ && scratchRenderTarget_ &&
            scratchShaderResource_ &&
            sameScratchDescription(required, scratchDescription_)) {
            sourceFormat_ = sourceView.Format;
            return true;
        }

        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11ShaderResourceView> shaderResource;
        if (FAILED(device_->CreateTexture2D(
                &required,
                nullptr,
                &texture)) ||
            FAILED(device_->CreateRenderTargetView(
                texture.Get(),
                nullptr,
                &renderTarget)) ||
            FAILED(device_->CreateShaderResourceView(
                texture.Get(),
                nullptr,
                &shaderResource))) {
            return false;
        }

        scratchTexture_ = std::move(texture);
        scratchRenderTarget_ = std::move(renderTarget);
        scratchShaderResource_ = std::move(shaderResource);
        scratchDescription_ = required;
        sourceFormat_ = sourceView.Format;
        return true;
    }

    void ReflectionFreeCaptureResources::clearScratch(ID3D11DeviceContext* context) noexcept
    {
        if (!context || !scratchRenderTarget_) return;
        const float marker = scratchDescription_.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? -1.0F : 0.0F;
        const std::array<float, 4> clear{ marker, marker, marker, 0.0F };
        context->ClearRenderTargetView(scratchRenderTarget_.Get(), clear.data());
    }

    ScopedReflectionFreeCapture::ScopedReflectionFreeCapture(
        ID3D11DeviceContext* context,
        ReflectionFreeCaptureResources& resources,
        render::GpuTimingProfiler* drawTiming) noexcept :
        context_(context)
    {
        if (!context_ || !resources.device() ||
            !resources.scratchRenderTarget() ||
            !resources.blackEnvironment() ||
            !resources.blackScreenReflection()) {
            rejection_ = ReflectionFreeCaptureRejection::invalidResources;
            context_ = nullptr;
            return;
        }
        ComPtr<ID3D11Device> contextDevice;
        context_->GetDevice(&contextDevice);
        if (contextDevice.Get() != resources.device()) {
            rejection_ = ReflectionFreeCaptureRejection::deviceMismatch;
            context_ = nullptr;
            return;
        }

        ComPtr<ID3D11BlendState> blendState;
        UINT sampleMask{};
        context_->OMGetBlendState(&blendState, nullptr, &sampleMask);
        if ((sampleMask & 1U) == 0) {
            rejection_ = ReflectionFreeCaptureRejection::sampleMask;
            context_ = nullptr;
            return;
        }
        if (blendState) {
            D3D11_BLEND_DESC blendDescription{};
            blendState->GetDesc(&blendDescription);
            if (blendDescription.RenderTarget[0].BlendEnable) {
                rejection_ = ReflectionFreeCaptureRejection::blendEnabled;
                context_ = nullptr;
                return;
            }
            constexpr UINT radianceWriteMask =
                D3D11_COLOR_WRITE_ENABLE_RED |
                D3D11_COLOR_WRITE_ENABLE_GREEN |
                D3D11_COLOR_WRITE_ENABLE_BLUE;
            if ((blendDescription.RenderTarget[0].RenderTargetWriteMask &
                    radianceWriteMask) != radianceWriteMask) {
                rejection_ = ReflectionFreeCaptureRejection::
                    renderTargetWriteMask;
                context_ = nullptr;
                return;
            }
            ComPtr<ID3D11BlendState1> blendState1;
            if (SUCCEEDED(blendState.As(&blendState1)) && blendState1) {
                D3D11_BLEND_DESC1 blendDescription1{};
                blendState1->GetDesc1(&blendDescription1);
                if (blendDescription1.RenderTarget[0].LogicOpEnable) {
                    rejection_ = ReflectionFreeCaptureRejection::
                        logicOperation;
                    context_ = nullptr;
                    return;
                }
            }
        }
        std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT>
            rawStreamOutputTargets{};
        context_->SOGetTargets(
            static_cast<UINT>(rawStreamOutputTargets.size()),
            rawStreamOutputTargets.data());
        std::array<ComPtr<ID3D11Buffer>, D3D11_SO_BUFFER_SLOT_COUNT>
            streamOutputTargets{};
        bool hasStreamOutputTarget{};
        for (std::size_t index = 0;
             index < streamOutputTargets.size();
             ++index) {
            streamOutputTargets[index].Attach(rawStreamOutputTargets[index]);
            hasStreamOutputTarget = hasStreamOutputTarget ||
                streamOutputTargets[index] != nullptr;
        }
        if (hasStreamOutputTarget) {
            rejection_ = ReflectionFreeCaptureRejection::streamOutput;
            context_ = nullptr;
            return;
        }

        std::array<ID3D11RenderTargetView*, kRenderTargetCount>
            rawRenderTargets{};
        ID3D11DepthStencilView* rawDepthStencil{};
        context_->OMGetRenderTargets(
            kRenderTargetCount,
            rawRenderTargets.data(),
            &rawDepthStencil);
        for (std::size_t index = 0; index < renderTargets_.size(); ++index) {
            renderTargets_[index].Attach(rawRenderTargets[index]);
            if (rawRenderTargets[index]) {
                renderTargetCount_ = static_cast<UINT>(index + 1);
            }
        }
        depthStencil_.Attach(rawDepthStencil);
        if (renderTargetCount_ != 1 || !renderTargets_[0] ||
            !resources.scratchMatches(renderTargets_[0].Get())) {
            rejection_ = ReflectionFreeCaptureRejection::renderTargetLayout;
            context_ = nullptr;
            renderTargets_ = {};
            depthStencil_.Reset();
            renderTargetCount_ = 0;
            return;
        }

        ID3D11ShaderResourceView* rawEnvironment{};
        ID3D11ShaderResourceView* rawScreenReflection{};
        context_->PSGetShaderResources(
            kEnvironmentSlot,
            1,
            &rawEnvironment);
        context_->PSGetShaderResources(
            kScreenReflectionSlot,
            1,
            &rawScreenReflection);
        environment_.Attach(rawEnvironment);
        screenReflection_.Attach(rawScreenReflection);
        ID3D11DepthStencilState* rawDepthStencilState{};
        context_->OMGetDepthStencilState(
            &rawDepthStencilState,
            &stencilReference_);
        depthStencilState_.Attach(rawDepthStencilState);
        D3D11_DEPTH_STENCIL_DESC depthStencilDescription{};
        if (depthStencilState_) {
            depthStencilState_->GetDesc(&depthStencilDescription);
        } else {
            depthStencilDescription.DepthEnable = TRUE;
            depthStencilDescription.DepthWriteMask =
                D3D11_DEPTH_WRITE_MASK_ALL;
            depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;
            depthStencilDescription.StencilReadMask =
                D3D11_DEFAULT_STENCIL_READ_MASK;
            depthStencilDescription.StencilWriteMask =
                D3D11_DEFAULT_STENCIL_WRITE_MASK;
            depthStencilDescription.FrontFace.StencilFailOp =
                D3D11_STENCIL_OP_KEEP;
            depthStencilDescription.FrontFace.StencilDepthFailOp =
                D3D11_STENCIL_OP_KEEP;
            depthStencilDescription.FrontFace.StencilPassOp =
                D3D11_STENCIL_OP_KEEP;
            depthStencilDescription.FrontFace.StencilFunc =
                D3D11_COMPARISON_ALWAYS;
            depthStencilDescription.BackFace =
                depthStencilDescription.FrontFace;
        }
        captureDepthStencilState_ = resources.readOnlyDepthState(depthStencilDescription);
        if (!captureDepthStencilState_) {
            rejection_ = ReflectionFreeCaptureRejection::
                depthStencilStateCreation;
            context_ = nullptr;
            renderTargets_ = {};
            depthStencil_.Reset();
            depthStencilState_.Reset();
            renderTargetCount_ = 0;
            return;
        }
        stateCaptured_ = true;

        auto* blackEnvironment = resources.blackEnvironment();
        auto* blackScreenReflection = resources.blackScreenReflection();
        auto* scratchRenderTarget = resources.scratchRenderTarget();
        context_->PSSetShaderResources(
            kEnvironmentSlot,
            1,
            &blackEnvironment);
        context_->PSSetShaderResources(
            kScreenReflectionSlot,
            1,
            &blackScreenReflection);
        context_->OMSetRenderTargetsAndUnorderedAccessViews(
            1,
            &scratchRenderTarget,
            depthStencil_.Get(),
            0,
            D3D11_KEEP_UNORDERED_ACCESS_VIEWS,
            nullptr,
            nullptr);
        context_->OMSetDepthStencilState(
            captureDepthStencilState_.Get(),
            stencilReference_);

        active_ = appliedStateMatches(
            scratchRenderTarget,
            depthStencil_.Get(),
            captureDepthStencilState_.Get(),
            stencilReference_,
            blackEnvironment,
            blackScreenReflection);
        if (!active_) {
            rejection_ = ReflectionFreeCaptureRejection::
                appliedStateMismatch;
            (void)restore();
        } else {
            rejection_ = ReflectionFreeCaptureRejection::none;
            if (drawTiming) {
                drawTiming_ = drawTiming->begin();
            }
        }
    }

    ScopedReflectionFreeCapture::~ScopedReflectionFreeCapture()
    {
        (void)restore();
    }

    ScopedReflectionFreeCapture::ScopedReflectionFreeCapture(
        ScopedReflectionFreeCapture&& other) noexcept :
        context_(other.context_),
        renderTargets_(std::move(other.renderTargets_)),
        depthStencil_(std::move(other.depthStencil_)),
        depthStencilState_(std::move(other.depthStencilState_)),
        captureDepthStencilState_(
            std::move(other.captureDepthStencilState_)),
        environment_(std::move(other.environment_)),
        screenReflection_(std::move(other.screenReflection_)),
        drawTiming_(std::move(other.drawTiming_)),
        renderTargetCount_(other.renderTargetCount_),
        stencilReference_(other.stencilReference_),
        stateCaptured_(other.stateCaptured_),
        active_(other.active_),
        rejection_(other.rejection_)
    {
        other.context_ = nullptr;
        other.renderTargetCount_ = 0;
        other.stencilReference_ = 0;
        other.stateCaptured_ = false;
        other.active_ = false;
        other.rejection_ = ReflectionFreeCaptureRejection::notAttempted;
    }

    bool ScopedReflectionFreeCapture::appliedStateMatches(
        ID3D11RenderTargetView* renderTarget,
        ID3D11DepthStencilView* depthStencil,
        ID3D11DepthStencilState* depthStencilState,
        UINT stencilReference,
        ID3D11ShaderResourceView* environment,
        ID3D11ShaderResourceView* screenReflection) const noexcept
    {
        if (!context_) {
            return false;
        }
        ID3D11RenderTargetView* rawRenderTarget{};
        ID3D11DepthStencilView* rawDepthStencil{};
        ID3D11ShaderResourceView* rawEnvironment{};
        ID3D11ShaderResourceView* rawScreenReflection{};
        ID3D11DepthStencilState* rawDepthStencilState{};
        UINT observedStencilReference{};
        context_->OMGetRenderTargets(
            1,
            &rawRenderTarget,
            &rawDepthStencil);
        context_->PSGetShaderResources(
            kEnvironmentSlot,
            1,
            &rawEnvironment);
        context_->PSGetShaderResources(
            kScreenReflectionSlot,
            1,
            &rawScreenReflection);
        context_->OMGetDepthStencilState(
            &rawDepthStencilState,
            &observedStencilReference);
        ComPtr<ID3D11RenderTargetView> retainedRenderTarget;
        ComPtr<ID3D11DepthStencilView> retainedDepthStencil;
        ComPtr<ID3D11ShaderResourceView> retainedEnvironment;
        ComPtr<ID3D11ShaderResourceView> retainedScreenReflection;
        ComPtr<ID3D11DepthStencilState> retainedDepthStencilState;
        retainedRenderTarget.Attach(rawRenderTarget);
        retainedDepthStencil.Attach(rawDepthStencil);
        retainedEnvironment.Attach(rawEnvironment);
        retainedScreenReflection.Attach(rawScreenReflection);
        retainedDepthStencilState.Attach(rawDepthStencilState);
        return retainedRenderTarget.Get() == renderTarget &&
            retainedDepthStencil.Get() == depthStencil &&
            retainedEnvironment.Get() == environment &&
            retainedScreenReflection.Get() == screenReflection &&
            retainedDepthStencilState.Get() == depthStencilState &&
            observedStencilReference == stencilReference;
    }

    bool ScopedReflectionFreeCapture::restore() noexcept
    {
        if (!stateCaptured_ || !context_) {
            active_ = false;
            drawTiming_.finish();
            return true;
        }

        std::array<ID3D11RenderTargetView*, kRenderTargetCount>
            rawRenderTargets{};
        for (std::size_t index = 0; index < renderTargets_.size(); ++index) {
            rawRenderTargets[index] = renderTargets_[index].Get();
        }
        auto* environment = environment_.Get();
        auto* screenReflection = screenReflection_.Get();
        context_->OMSetRenderTargetsAndUnorderedAccessViews(
            renderTargetCount_,
            rawRenderTargets.data(),
            depthStencil_.Get(),
            0,
            D3D11_KEEP_UNORDERED_ACCESS_VIEWS,
            nullptr,
            nullptr);
        context_->PSSetShaderResources(
            kEnvironmentSlot,
            1,
            &environment);
        context_->PSSetShaderResources(
            kScreenReflectionSlot,
            1,
            &screenReflection);

        active_ = false;
        context_->OMSetDepthStencilState(
            depthStencilState_.Get(),
            stencilReference_);

        const auto restored = appliedStateMatches(
            renderTargets_[0].Get(),
            depthStencil_.Get(),
            depthStencilState_.Get(),
            stencilReference_,
            environment_.Get(),
            screenReflection_.Get());
        stateCaptured_ = false;
        drawTiming_.finish();
        return restored;
    }
}
