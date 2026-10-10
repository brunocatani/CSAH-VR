#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"

#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include "SubsurfaceScatteringCS.h"

#include <array>
#include <utility>

namespace csah::subsurface_scattering
{
    namespace
    {
        constexpr UINT kAlbedoSlot = 0;
        constexpr UINT kDepthSlot = 3;
        constexpr UINT kThreadGroupWidth = 8;
        constexpr UINT kThreadGroupHeight = 8;

        struct alignas(16) GpuSettings
        {
            float width{};
            float height{};
            float directionX{};
            float directionY{};
            float radiusPixels{};
            float strength{};
            float depthRejection{};
            float skinCode{ 3.0f / 255.0f };
            float classTolerance{ 0.5f / 255.0f };
            float reserved0{};
            float reserved1{};
            float reserved2{};
            float viewport[4]{};
        };
        static_assert(sizeof(GpuSettings) == 64);

        [[nodiscard]] bool supportedFormat(DXGI_FORMAT format) noexcept
        {
            return format == DXGI_FORMAT_R11G11B10_FLOAT ||
                format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                format == DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        gpuTiming_.reset();
        device_ = device;
        context_ = context;
        shader_.Reset();
        constants_.Reset();
        scratchA_.Reset();
        scratchB_.Reset();
        scratchAView_.Reset();
        scratchBView_.Reset();
        scratchAOutput_.Reset();
        scratchBOutput_.Reset();
        width_ = 0;
        height_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        firstExecutionLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto shaderResult = device->CreateComputeShader(
            csah_subsurface_scattering,
            sizeof(csah_subsurface_scattering),
            nullptr,
            shader_.ReleaseAndGetAddressOf());
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto bufferResult = SUCCEEDED(shaderResult) ?
            device->CreateBuffer(
                &description, nullptr, constants_.ReleaseAndGetAddressOf()) :
            E_FAIL;
        if (FAILED(shaderResult) || FAILED(bufferResult) || !shader_ ||
            !constants_) {
            shader_.Reset();
            constants_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Subsurface Scattering GPU resource creation failed (shader=0x{:08X}, constants=0x{:08X}).",
                static_cast<std::uint32_t>(shaderResult),
                static_cast<std::uint32_t>(bufferResult));
            return;
        }
        if (!gpuTiming_.initialize(
                device,
                context,
                "Subsurface Scattering",
                { "copy in", "horizontal", "vertical", "copy back" },
                4)) {
            logging::warn(
                "Subsurface Scattering could not allocate image-neutral GPU timing queries; rendering remains active without performance telemetry.");
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Subsurface Scattering stereo compute pipeline is ready; execution is class-mask-gated after exact directional DFLight and producer coverage is reported separately.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        strength_.store(safe.strength, std::memory_order_relaxed);
        radiusPixels_.store(safe.radiusPixels, std::memory_order_relaxed);
        depthRejection_.store(safe.depthRejection, std::memory_order_relaxed);
        surface_classification::Runtime::get().setConsumerEnabled(
            surface_classification::Consumer::subsurfaceScattering,
            safe.enabled);
    }

    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::ensureScratch(
        ID3D11RenderTargetView* target,
        ID3D11Texture2D** source,
        UINT* sourceSubresource) noexcept
    {
        if (!device_ || !target || !source || !sourceSubresource) {
            return false;
        }
        *source = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        target->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture)) || !texture) {
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDescription{};
        D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
        texture->GetDesc(&sourceDescription);
        target->GetDesc(&viewDescription);
        UINT mipSlice{};
        UINT arraySlice{};
        if (viewDescription.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) {
            mipSlice = viewDescription.Texture2D.MipSlice;
        } else if (
            viewDescription.ViewDimension ==
                D3D11_RTV_DIMENSION_TEXTURE2DARRAY &&
            viewDescription.Texture2DArray.ArraySize == 1) {
            mipSlice = viewDescription.Texture2DArray.MipSlice;
            arraySlice = viewDescription.Texture2DArray.FirstArraySlice;
        } else {
            return false;
        }
        const auto width = sourceDescription.Width >> mipSlice;
        const auto height = sourceDescription.Height >> mipSlice;
        const auto format = viewDescription.Format == DXGI_FORMAT_UNKNOWN ?
            sourceDescription.Format : viewDescription.Format;
        if (width < 2 || height == 0 || (width & 1u) != 0 ||
            sourceDescription.SampleDesc.Count != 1 ||
            !supportedFormat(format)) {
            return false;
        }
        UINT support{};
        if (FAILED(device_->CheckFormatSupport(format, &support)) ||
            (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) == 0 ||
            (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0) {
            return false;
        }
        if (!scratchA_ || !scratchB_ || !scratchAView_ || !scratchBView_ ||
            !scratchAOutput_ || !scratchBOutput_ || width_ != width ||
            height_ != height || format_ != format) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> nextA;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> nextB;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextAView;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextBView;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextAOutput;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextBOutput;
            auto result = device_->CreateTexture2D(
                &description, nullptr, nextA.GetAddressOf());
            result = SUCCEEDED(result) ? device_->CreateTexture2D(
                &description, nullptr, nextB.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateShaderResourceView(
                nextA.Get(), nullptr, nextAView.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateShaderResourceView(
                nextB.Get(), nullptr, nextBView.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateUnorderedAccessView(
                nextA.Get(), nullptr, nextAOutput.GetAddressOf()) : result;
            result = SUCCEEDED(result) ? device_->CreateUnorderedAccessView(
                nextB.Get(), nullptr, nextBOutput.GetAddressOf()) : result;
            if (FAILED(result) || !nextA || !nextB || !nextAView ||
                !nextBView || !nextAOutput || !nextBOutput) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Subsurface Scattering scratch allocation failed for {}x{} format {} (HRESULT=0x{:08X}).",
                    width, height, static_cast<unsigned>(format),
                    static_cast<std::uint32_t>(result));
                return false;
            }
            scratchA_ = std::move(nextA);
            scratchB_ = std::move(nextB);
            scratchAView_ = std::move(nextAView);
            scratchBView_ = std::move(nextBView);
            scratchAOutput_ = std::move(nextAOutput);
            scratchBOutput_ = std::move(nextBOutput);
            width_ = width;
            height_ = height;
            format_ = format;
            resourceRebuilds_.fetch_add(1, std::memory_order_relaxed);
            logging::info(
                "Subsurface Scattering stereo scratch pair allocated at {}x{} format {}.",
                width_, height_, static_cast<unsigned>(format_));
        }
        *sourceSubresource = D3D11CalcSubresource(
            mipSlice, arraySlice, sourceDescription.MipLevels);
        *source = texture.Detach();
        return true;
    }

    bool Runtime::updateConstants(
        ID3D11DeviceContext* context,
        float directionX,
        float directionY) noexcept
    {
        if (!context || !constants_ || width_ == 0 || height_ == 0) {
            return false;
        }
        D3D11_VIEWPORT viewport{};UINT count=1;context->RSGetViewports(&count,&viewport);
        const GpuSettings settings{
            .width = static_cast<float>(width_),
            .height = static_cast<float>(height_),
            .directionX = directionX,
            .directionY = directionY,
            .radiusPixels = radiusPixels_.load(std::memory_order_relaxed),
            .strength = strength_.load(std::memory_order_relaxed),
            .depthRejection = depthRejection_.load(std::memory_order_relaxed),
            .viewport = {viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height},
        };
        context->UpdateSubresource(
            constants_.Get(), 0, nullptr, &settings, 0, 0);
        return true;
    }

    bool Runtime::executeAfterDirectionalLight(
        ID3D11DeviceContext* context) noexcept
    {
        if (!requested() || !context || context != context_.Get() ||
            !shader_ || !constants_) {
            return false;
        }
        auto* surfaceClass =
            surface_classification::Runtime::get().shaderResourceView();
        if (!surfaceClass) {
            return false;
        }
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
        std::array<ID3D11RenderTargetView*,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencil;
        context->OMGetRenderTargets(
            static_cast<UINT>(rawTargets.size()), rawTargets.data(),
            depthStencil.GetAddressOf());
        UINT targetCount{};
        for (std::size_t index = 0; index < rawTargets.size(); ++index) {
            targets[index].Attach(rawTargets[index]);
            if (rawTargets[index]) {
                targetCount = static_cast<UINT>(index + 1);
            }
        }
        if (!targets[0]) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        UINT sourceSubresource{};
        ID3D11Texture2D* sourceRaw{};
        if (!ensureScratch(
                targets[0].Get(), &sourceRaw, &sourceSubresource) ||
            !sourceRaw) {
            return false;
        }
        source.Attach(sourceRaw);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> albedo;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth;
        context->PSGetShaderResources(kAlbedoSlot, 1, albedo.GetAddressOf());
        context->PSGetShaderResources(kDepthSlot, 1, depth.GetAddressOf());
        if (!albedo || !depth) {
            return false;
        }
        auto timing = gpuTiming_.begin();
        D3D11_BOX sourceBox{ 0, 0, 0, width_, height_, 1 };
        context->CopySubresourceRegion(
            scratchA_.Get(), 0, 0, 0, 0, source.Get(), sourceSubresource,
            &sourceBox);
        timing.mark();

        render::ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 4,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 1,
                .firstConstantBuffer = 0,
                .constantBufferCount = 1,
            });
        if (!restore.captured()) {
            return false;
        }
        auto dispatch = [this, context, albedo = albedo.Get(),
                            depth = depth.Get(), surfaceClass](
                            ID3D11ShaderResourceView* input,
                            ID3D11UnorderedAccessView* output,
                            float directionX,
                            float directionY) noexcept {
            if (!updateConstants(context, directionX, directionY)) {
                return false;
            }
            std::array<ID3D11ShaderResourceView*, 4> inputs{
                input, depth, surfaceClass, albedo,
            };
            auto* constants = constants_.Get();
            context->CSSetShader(shader_.Get(), nullptr, 0);
            context->CSSetShaderResources(
                0, static_cast<UINT>(inputs.size()), inputs.data());
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->CSSetConstantBuffers(0, 1, &constants);
            context->Dispatch(
                (width_ + kThreadGroupWidth - 1) / kThreadGroupWidth,
                (height_ + kThreadGroupHeight - 1) / kThreadGroupHeight,
                1);
            std::array<ID3D11ShaderResourceView*, 4> nullInputs{};
            ID3D11UnorderedAccessView* nullOutput{};
            context->CSSetShaderResources(
                0, static_cast<UINT>(nullInputs.size()), nullInputs.data());
            context->CSSetUnorderedAccessViews(
                0, 1, &nullOutput, nullptr);
            dispatches_.fetch_add(1, std::memory_order_relaxed);
            return true;
        };
        const auto horizontal = dispatch(
            scratchAView_.Get(), scratchBOutput_.Get(), 1.0f, 0.0f);
        timing.mark();
        const auto vertical = horizontal && dispatch(
            scratchBView_.Get(), scratchAOutput_.Get(), 0.0f, 1.0f);
        timing.mark();
        const auto restored = restore.restore();
        if (!vertical || !restored) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopySubresourceRegion(
            source.Get(), sourceSubresource, 0, 0, 0, scratchA_.Get(), 0,
            nullptr);
        context->OMSetRenderTargets(
            targetCount, rawTargets.data(), depthStencil.Get());
        executions_.fetch_add(1, std::memory_order_relaxed);
        if (!firstExecutionLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::info(
                "Subsurface Scattering completed its first class-mask-gated, albedo-preserving stereo pass (class=3, strength={:.3f}, reference radius={:.3f}); producer coverage is reported separately.",
                strength_.load(std::memory_order_relaxed),
                radiusPixels_.load(std::memory_order_relaxed));
        }
        return true;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .strength = strength_.load(std::memory_order_relaxed),
                .radiusPixels = radiusPixels_.load(std::memory_order_relaxed),
                .depthRejection = depthRejection_.load(
                    std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .executions = executions_.load(std::memory_order_relaxed),
            .dispatches = dispatches_.load(std::memory_order_relaxed),
            .resourceRebuilds = resourceRebuilds_.load(
                std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
