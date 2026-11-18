#include "Features/surface_classification/SurfaceClassificationRuntime.h"

#include "render/BSDFPrePassShaderHook.h"
#include "support/Logger.h"

#include <algorithm>
#include <array>
#include <utility>

namespace csah::surface_classification
{
    namespace
    {
        constexpr std::array<DXGI_FORMAT, 6> kFO4VRGBufferFormats{
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R16G16_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R16G16_FLOAT,
        };

        [[nodiscard]] constexpr std::uint32_t consumerBit(
            Consumer consumer) noexcept
        {
            return static_cast<std::uint32_t>(consumer);
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
        device_ = device;
        context_ = context;
        texture_.Reset();
        renderTargetView_.Reset();
        shaderResourceView_.Reset();
        pbrMaterialTexture_.Reset();
        pbrMaterialRenderTargetView_.Reset();
        pbrMaterialShaderResourceView_.Reset();
        width_ = 0;
        height_ = 0;
        sampleCount_ = 0;
        worldFrameConsumed_ = true;
        firstAcceptedBindingLogged_.store(false, std::memory_order_relaxed);
        firstCandidateRejectionLogged_.store(false, std::memory_order_relaxed);
    }

    void Runtime::setPbrMaterialTransportEnabled(bool enabled) noexcept
    {
        const auto previous = pbrMaterialTransportEnabled_.exchange(
            enabled,
            std::memory_order_acq_rel);
        if (previous != enabled) {
            worldFrameConsumed_ = true;
        }
    }

    void Runtime::setConsumerEnabled(Consumer consumer, bool enabled) noexcept
    {
        const auto bit = consumerBit(consumer);
        std::uint32_t previous{};
        if (enabled) {
            previous = consumerMask_.fetch_or(bit, std::memory_order_acq_rel);
        } else {
            previous = consumerMask_.fetch_and(~bit, std::memory_order_acq_rel);
        }
        const auto next = enabled ? previous | bit : previous & ~bit;
        if ((previous == 0) != (next == 0)) {
            render::setDFPrePassSurfaceClassificationEnabled(next != 0);
            if (next != 0) {
                worldFrameConsumed_ = true;
            }
        }
    }

    bool Runtime::required() const noexcept
    {
        return consumerMask_.load(std::memory_order_acquire) != 0;
    }

    UINT Runtime::requiredRenderTargetCount() const noexcept
    {
        return pbrMaterialTransportEnabled_.load(
                   std::memory_order_acquire) ?
            8u : 7u;
    }

    bool Runtime::matchesGBuffer(
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        D3D11_TEXTURE2D_DESC& sourceDescription) const noexcept
    {
        if (renderTargetCount < kFO4VRGBufferFormats.size() ||
            renderTargetCount > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT ||
            !renderTargets) {
            return false;
        }

        // FO4VR may bind all eight OM slots while leaving slots 6 and 7 null.
        // Those trailing nulls are semantically the same six-target G-buffer
        // and leave slot 6 available for our private classification target.
        for (UINT index = static_cast<UINT>(kFO4VRGBufferFormats.size());
             index < renderTargetCount;
             ++index) {
            if (renderTargets[index]) {
                return false;
            }
        }

        D3D11_TEXTURE2D_DESC common{};
        for (UINT index = 0;
             index < static_cast<UINT>(kFO4VRGBufferFormats.size());
             ++index) {
            auto* view = renderTargets[index];
            if (!view) {
                return false;
            }
            D3D11_RENDER_TARGET_VIEW_DESC viewDescription{};
            view->GetDesc(&viewDescription);
            if (viewDescription.Format != kFO4VRGBufferFormats[index] ||
                (viewDescription.ViewDimension !=
                        D3D11_RTV_DIMENSION_TEXTURE2D &&
                    viewDescription.ViewDimension !=
                        D3D11_RTV_DIMENSION_TEXTURE2DMS)) {
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            view->GetResource(resource.GetAddressOf());
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            // The FO4VR world G-buffer is a side-by-side stereo surface. The
            // other capture passes can reuse the same six formats at square
            // resolutions, but they are independent render domains and must
            // never replace or clear the world classification target.
            if (description.Width < 2 || description.Height == 0 ||
                description.Width <= description.Height ||
                (description.Width & 1u) != 0 ||
                description.ArraySize != 1 || description.MipLevels == 0 ||
                description.Format != kFO4VRGBufferFormats[index] ||
                description.SampleDesc.Count != 1) {
                return false;
            }
            if (index == 0) {
                common = description;
            } else if (
                description.Width != common.Width ||
                description.Height != common.Height ||
                description.ArraySize != common.ArraySize ||
                description.SampleDesc.Count != common.SampleDesc.Count ||
                description.SampleDesc.Quality !=
                    common.SampleDesc.Quality) {
                return false;
            }
        }
        sourceDescription = common;
        return true;
    }

    void Runtime::logCandidateRejectionOnce(
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets) noexcept
    {
        if (!renderTargets ||
            renderTargetCount < kFO4VRGBufferFormats.size() ||
            firstCandidateRejectionLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            return;
        }

        std::array<UINT, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> formats{};
        std::array<UINT, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> dimensions{};
        std::uint32_t nonNullMask{};
        const auto inspected = std::min(
            renderTargetCount,
            static_cast<UINT>(formats.size()));
        for (UINT index = 0; index < inspected; ++index) {
            if (!renderTargets[index]) {
                continue;
            }
            nonNullMask |= 1u << index;
            D3D11_RENDER_TARGET_VIEW_DESC description{};
            renderTargets[index]->GetDesc(&description);
            formats[index] = static_cast<UINT>(description.Format);
            dimensions[index] = static_cast<UINT>(
                description.ViewDimension);
        }
        logging::warn(
            "Surface classification rejected its first six-or-more-target candidate: count={}, nonNullMask=0x{:02X}, formats=[{},{},{},{},{},{},{},{}], dimensions=[{},{},{},{},{},{},{},{}].",
            renderTargetCount,
            nonNullMask,
            formats[0], formats[1], formats[2], formats[3],
            formats[4], formats[5], formats[6], formats[7],
            dimensions[0], dimensions[1], dimensions[2], dimensions[3],
            dimensions[4], dimensions[5], dimensions[6], dimensions[7]);
    }

    bool Runtime::ensureTarget(
        const D3D11_TEXTURE2D_DESC& sourceDescription) noexcept
    {
        const auto pbrRequired = pbrMaterialTransportEnabled_.load(
            std::memory_order_acquire);
        if (texture_ && renderTargetView_ && shaderResourceView_ &&
            width_ == sourceDescription.Width &&
            height_ == sourceDescription.Height &&
            sampleCount_ == sourceDescription.SampleDesc.Count &&
            (!pbrRequired ||
                (pbrMaterialTexture_ && pbrMaterialRenderTargetView_ &&
                    pbrMaterialShaderResourceView_))) {
            return true;
        }
        if (!device_) {
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = sourceDescription.Width;
        description.Height = sourceDescription.Height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8_UNORM;
        description.SampleDesc = sourceDescription.SampleDesc;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> nextTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> nextRenderTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextShaderResource;
        const auto textureResult = device_->CreateTexture2D(
            &description,
            nullptr,
            nextTexture.GetAddressOf());
        const auto renderTargetResult = SUCCEEDED(textureResult) ?
            device_->CreateRenderTargetView(
                nextTexture.Get(),
                nullptr,
                nextRenderTarget.GetAddressOf()) : E_FAIL;
        const auto shaderResourceResult = SUCCEEDED(renderTargetResult) ?
            device_->CreateShaderResourceView(
                nextTexture.Get(),
                nullptr,
                nextShaderResource.GetAddressOf()) : E_FAIL;
        if (FAILED(textureResult) || FAILED(renderTargetResult) ||
            FAILED(shaderResourceResult) || !nextTexture ||
            !nextRenderTarget || !nextShaderResource) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Surface classification target allocation failed for {}x{}x{} (texture=0x{:08X}, RTV=0x{:08X}, SRV=0x{:08X}).",
                sourceDescription.Width,
                sourceDescription.Height,
                sourceDescription.SampleDesc.Count,
                static_cast<std::uint32_t>(textureResult),
                static_cast<std::uint32_t>(renderTargetResult),
                static_cast<std::uint32_t>(shaderResourceResult));
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> nextPbrMaterialTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>
            nextPbrMaterialRenderTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            nextPbrMaterialShaderResource;
        if (pbrRequired) {
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            const auto pbrTextureResult = device_->CreateTexture2D(
                &description,
                nullptr,
                nextPbrMaterialTexture.GetAddressOf());
            const auto pbrRenderTargetResult =
                SUCCEEDED(pbrTextureResult) ?
                device_->CreateRenderTargetView(
                    nextPbrMaterialTexture.Get(),
                    nullptr,
                    nextPbrMaterialRenderTarget.GetAddressOf()) : E_FAIL;
            const auto pbrShaderResourceResult =
                SUCCEEDED(pbrRenderTargetResult) ?
                device_->CreateShaderResourceView(
                    nextPbrMaterialTexture.Get(),
                    nullptr,
                    nextPbrMaterialShaderResource.GetAddressOf()) : E_FAIL;
            if (FAILED(pbrTextureResult) ||
                FAILED(pbrRenderTargetResult) ||
                FAILED(pbrShaderResourceResult) ||
                !nextPbrMaterialTexture || !nextPbrMaterialRenderTarget ||
                !nextPbrMaterialShaderResource) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Authored PBR material target allocation failed for {}x{} (texture=0x{:08X}, RTV=0x{:08X}, SRV=0x{:08X}).",
                    sourceDescription.Width,
                    sourceDescription.Height,
                    static_cast<std::uint32_t>(pbrTextureResult),
                    static_cast<std::uint32_t>(pbrRenderTargetResult),
                    static_cast<std::uint32_t>(pbrShaderResourceResult));
                return false;
            }
        }

        texture_ = std::move(nextTexture);
        renderTargetView_ = std::move(nextRenderTarget);
        shaderResourceView_ = std::move(nextShaderResource);
        pbrMaterialTexture_ = std::move(nextPbrMaterialTexture);
        pbrMaterialRenderTargetView_ =
            std::move(nextPbrMaterialRenderTarget);
        pbrMaterialShaderResourceView_ =
            std::move(nextPbrMaterialShaderResource);
        width_ = sourceDescription.Width;
        height_ = sourceDescription.Height;
        sampleCount_ = sourceDescription.SampleDesc.Count;
        worldFrameConsumed_ = true;
        targetRebuilds_.fetch_add(1, std::memory_order_relaxed);
        logging::info(
            "Surface classification target allocated at {}x{} R8_UNORM (samples={}, authoredPbr={}).",
            width_,
            height_,
            sampleCount_,
            pbrRequired);
        return true;
    }

    GBufferBinding Runtime::prepareGBufferBinding(
        ID3D11DeviceContext* context,
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets) noexcept
    {
        if (!required() || !context || context != context_.Get()) {
            return {};
        }

        D3D11_TEXTURE2D_DESC sourceDescription{};
        if (!matchesGBuffer(
                renderTargetCount,
                renderTargets,
                sourceDescription)) {
            logCandidateRejectionOnce(renderTargetCount, renderTargets);
            rejectedGBufferBinds_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        if (!ensureTarget(sourceDescription)) {
            rejectedGBufferBinds_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        if (worldFrameConsumed_) {
            constexpr float clear[4]{};
            context->ClearRenderTargetView(renderTargetView_.Get(), clear);
            if (pbrMaterialRenderTargetView_) {
                context->ClearRenderTargetView(
                    pbrMaterialRenderTargetView_.Get(), clear);
            }
            targetClears_.fetch_add(1, std::memory_order_relaxed);
            worldFrameConsumed_ = false;
        }

        GBufferBinding binding{};
        std::copy_n(
            renderTargets,
            kFO4VRGBufferFormats.size(),
            binding.renderTargets.begin());
        binding.renderTargets[6] = renderTargetView_.Get();
        if (pbrMaterialTransportEnabled_.load(
                std::memory_order_acquire)) {
            binding.renderTargets[7] = pbrMaterialRenderTargetView_.Get();
        }
        binding.renderTargetCount = requiredRenderTargetCount();
        acceptedGBufferBinds_.fetch_add(1, std::memory_order_relaxed);
        if (!firstAcceptedBindingLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Surface classification accepted the exact double-wide stereo six-target FO4VR G-buffer and appended private MRT6{}.",
                binding.renderTargetCount == 8 ? "/MRT7" : "");
        }
        return binding;
    }

    void Runtime::markWorldFrameConsumed() noexcept
    {
        worldFrameConsumed_ = true;
    }

    ID3D11ShaderResourceView* Runtime::shaderResourceView() const noexcept
    {
        return shaderResourceView_.Get();
    }

    ID3D11ShaderResourceView*
        Runtime::pbrMaterialShaderResourceView() const noexcept
    {
        return pbrMaterialShaderResourceView_.Get();
    }

    void Runtime::recordProducerSelection(
        std::uint32_t classCode,
        std::uint32_t contractIndex,
        ProducerEvidence evidence,
        std::uint32_t descriptor) noexcept
    {
        if (classCode >= producerSelections_.size()) {
            descriptorContractMisses_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto index = static_cast<std::size_t>(classCode);
        producerSelections_[index].fetch_add(1, std::memory_order_relaxed);
        if (!firstProducerSelectionLogged_[index].exchange(
                true, std::memory_order_relaxed)) {
            if (evidence == ProducerEvidence::descriptor) {
                logging::info(
                    "Surface classification selected producer class {} from exact descriptor 0x{:08X} and material contract {}.",
                    classCode,
                    descriptor,
                    contractIndex);
            } else if (evidence == ProducerEvidence::materialIdentity) {
                logging::info(
                    "Surface classification selected producer class {} from unambiguous material shader contract {}.",
                    classCode,
                    contractIndex);
            } else {
                logging::info(
                    "Surface classification selected producer class {} from an exact grass-only vertex shader and material contract {}.",
                    classCode,
                    contractIndex);
            }
        }
    }

    void Runtime::recordDescriptorObservation(
        std::uint32_t classCode,
        std::uint32_t descriptor) noexcept
    {
        if (classCode >= descriptorObservations_.size()) {
            recordDescriptorObservationMiss(descriptor);
            return;
        }
        const auto index = static_cast<std::size_t>(classCode);
        descriptorObservations_[index].fetch_add(
            1,
            std::memory_order_relaxed);
        if (!firstDescriptorObservationLogged_[index].exchange(
                true, std::memory_order_relaxed)) {
            logging::info(
                "Surface classification observed exact DFPrePass descriptor class {} at the verified technique boundary (descriptor=0x{:08X}).",
                classCode,
                descriptor);
        }
    }

    void Runtime::recordDescriptorObservationMiss(
        std::uint32_t descriptor) noexcept
    {
        descriptorObservationMisses_.fetch_add(
            1,
            std::memory_order_relaxed);
        for (auto& slot : recentMissDescriptors_) {
            auto value = slot.load(std::memory_order_relaxed);
            if (value == descriptor) {
                break;
            }
            if (value == 0 &&
                slot.compare_exchange_strong(
                    value, descriptor, std::memory_order_relaxed)) {
                break;
            }
            if (value == descriptor) {
                break;
            }
        }
        if (!firstDescriptorObservationMissLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::warn(
                "Surface classification observed an exact DFPrePass descriptor outside the generated FXP inventory at the verified technique boundary (descriptor=0x{:08X}).",
                descriptor);
        }
    }

    void Runtime::recordDescriptorScopeMiss() noexcept
    {
        descriptorScopeMisses_.fetch_add(1, std::memory_order_relaxed);
        if (!firstDescriptorScopeMissLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::warn(
                "Surface classification selected a material replacement without an active DFPrepass descriptor scope.");
        }
    }

    void Runtime::recordDescriptorContractMiss(std::uint32_t descriptor) noexcept
    {
        descriptorContractMisses_.fetch_add(1, std::memory_order_relaxed);
        if (!firstDescriptorContractMissLogged_.exchange(
                true, std::memory_order_relaxed)) {
            logging::warn(
                "Surface classification has no exact producer contract for descriptor 0x{:08X}.",
                descriptor);
        }
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        std::array<std::uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            producerSelections{};
        std::array<std::uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            descriptorObservations{};
        for (std::size_t index = 0; index < producerSelections.size(); ++index) {
            producerSelections[index] =
                producerSelections_[index].load(std::memory_order_relaxed);
            descriptorObservations[index] =
                descriptorObservations_[index].load(
                    std::memory_order_relaxed);
        }
        return {
            .consumerMask = consumerMask_.load(std::memory_order_acquire),
            .targetReady = texture_ && renderTargetView_ && shaderResourceView_,
            .width = width_,
            .height = height_,
            .acceptedGBufferBinds = acceptedGBufferBinds_.load(
                std::memory_order_relaxed),
            .rejectedGBufferBinds = rejectedGBufferBinds_.load(
                std::memory_order_relaxed),
            .targetRebuilds = targetRebuilds_.load(
                std::memory_order_relaxed),
            .targetClears = targetClears_.load(std::memory_order_relaxed),
            .producerSelections = producerSelections,
            .descriptorObservations = descriptorObservations,
            .descriptorObservationMisses =
                descriptorObservationMisses_.load(
                    std::memory_order_relaxed),
            .descriptorScopeMisses =
                descriptorScopeMisses_.load(std::memory_order_relaxed),
            .descriptorContractMisses =
                descriptorContractMisses_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }

    std::array<std::uint32_t, Runtime::kRecentMissCapacity>
        Runtime::takeRecentMissDescriptors() noexcept
    {
        std::array<std::uint32_t, kRecentMissCapacity> result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = recentMissDescriptors_[index].exchange(
                0, std::memory_order_relaxed);
        }
        return result;
    }
}
