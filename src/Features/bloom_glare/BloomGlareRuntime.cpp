#include "Features/bloom_glare/BloomGlareRuntime.h"

#include "BloomGlareShaders.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace csah::bloom_glare
{
    namespace
    {
        constexpr UINT kPrivateResourceSlot = 4;
        constexpr UINT kPrivateResourceCount = 2;
        constexpr UINT kPrivateSamplerSlot = 4;
        constexpr UINT kPrivateConstantSlot = 13;
        constexpr DXGI_FORMAT kColorFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kComplexFormat = DXGI_FORMAT_R32G32_FLOAT;

        [[nodiscard]] constexpr UINT dispatchCount(
            UINT value,
            UINT groupSize) noexcept
        {
            return (value + groupSize - 1u) / groupSize;
        }

        [[nodiscard]] std::uint32_t availableMipCount(
            std::uint32_t width,
            std::uint32_t height) noexcept
        {
            auto eyeWidth = std::max(1u, width / 2u);
            std::uint32_t count = 1;
            while (eyeWidth > 1u && height > 1u && count < 9u &&
                   (eyeWidth & 1u) == 0u) {
                eyeWidth >>= 1u;
                height >>= 1u;
                ++count;
            }
            return count;
        }
    }

    ScopedBindings::ScopedBindings(
        Runtime* owner,
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* bloom,
        ID3D11ShaderResourceView* glare,
        ID3D11SamplerState* sampler,
        ID3D11Buffer* constants) noexcept :
        owner_(owner), context_(context)
    {
        if (!owner_ || !context_ || !constants) {
            owner_ = nullptr;
            context_ = nullptr;
            return;
        }

        std::array<ID3D11ShaderResourceView*, kPrivateResourceCount>
            previousResources{};
        context_->PSGetShaderResources(
            kPrivateResourceSlot,
            kPrivateResourceCount,
            previousResources.data());
        for (std::size_t index = 0; index < previousResources.size(); ++index) {
            previousResources_[index].Attach(previousResources[index]);
        }
        ID3D11SamplerState* previousSampler{};
        context_->PSGetSamplers(
            kPrivateSamplerSlot, 1, &previousSampler);
        previousSampler_.Attach(previousSampler);
        ID3D11Buffer* previousConstants{};
        context_->PSGetConstantBuffers(
            kPrivateConstantSlot, 1, &previousConstants);
        previousConstants_.Attach(previousConstants);

        std::array<ID3D11ShaderResourceView*, kPrivateResourceCount>
            resources{ bloom, glare };
        context_->PSSetShaderResources(
            kPrivateResourceSlot,
            kPrivateResourceCount,
            resources.data());
        context_->PSSetSamplers(kPrivateSamplerSlot, 1, &sampler);
        context_->PSSetConstantBuffers(kPrivateConstantSlot, 1, &constants);
    }

    ScopedBindings::~ScopedBindings() noexcept
    {
        if (!owner_ || !context_) {
            return;
        }
        std::array<ID3D11ShaderResourceView*, kPrivateResourceCount>
            resources{};
        for (std::size_t index = 0; index < resources.size(); ++index) {
            resources[index] = previousResources_[index].Get();
        }
        context_->PSSetShaderResources(
            kPrivateResourceSlot,
            kPrivateResourceCount,
            resources.data());
        auto* sampler = previousSampler_.Get();
        context_->PSSetSamplers(kPrivateSamplerSlot, 1, &sampler);
        auto* constants = previousConstants_.Get();
        context_->PSSetConstantBuffers(kPrivateConstantSlot, 1, &constants);
        owner_->drawRestores_.fetch_add(1, std::memory_order_relaxed);
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
        gpuReady_.store(false, std::memory_order_release);
        releaseSizeResources();
        constants_.Reset();
        linearSampler_.Reset();
        wrapSampler_.Reset();
        bloomThreshold_.Reset();
        bloomDownsample_.Reset();
        bloomUpsample_.Reset();
        glareThreshold_.Reset();
        glareAperture_.Reset();
        for (auto& shader : glarePsf_) {
            shader.Reset();
        }
        glareFftRowForward_.Reset();
        glareFftColumnForward_.Reset();
        glareConvolveRowInverse_.Reset();
        glareFftColumnInverse_.Reset();
        glareComposite_.Reset();
        device_ = device;
        context_ = context;
        firstDispatchLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(GpuConstants);
        constantDescription.Usage = D3D11_USAGE_DEFAULT;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        auto result = device->CreateBuffer(
            &constantDescription,
            nullptr,
            constants_.ReleaseAndGetAddressOf());

        D3D11_SAMPLER_DESC linearDescription{};
        linearDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        linearDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        linearDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        linearDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        linearDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (SUCCEEDED(result)) {
            result = device->CreateSamplerState(
                &linearDescription,
                linearSampler_.ReleaseAndGetAddressOf());
        }
        auto wrapDescription = linearDescription;
        wrapDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        wrapDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        if (SUCCEEDED(result)) {
            result = device->CreateSamplerState(
                &wrapDescription,
                wrapSampler_.ReleaseAndGetAddressOf());
        }

        const auto createShader =
            [device](const auto& bytecode, auto& target) noexcept {
                return device->CreateComputeShader(
                    bytecode.data(),
                    bytecode.size(),
                    nullptr,
                    target.ReleaseAndGetAddressOf());
            };
        if (SUCCEEDED(result)) {
            result = createShader(generated::kBloomThreshold, bloomThreshold_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(
                generated::kBloomDownsample, bloomDownsample_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kBloomUpsample, bloomUpsample_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlareThreshold, glareThreshold_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlareAperture, glareAperture_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlarePsfRed, glarePsf_[0]);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlarePsfGreen, glarePsf_[1]);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlarePsfBlue, glarePsf_[2]);
        }
        if (SUCCEEDED(result)) {
            result = createShader(
                generated::kGlareFftRowForward,
                glareFftRowForward_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(
                generated::kGlareFftColumnForward,
                glareFftColumnForward_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(
                generated::kGlareConvolveRowInverse,
                glareConvolveRowInverse_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(
                generated::kGlareFftColumnInverse,
                glareFftColumnInverse_);
        }
        if (SUCCEEDED(result)) {
            result = createShader(generated::kGlareComposite, glareComposite_);
        }

        if (FAILED(result) || !constants_ || !linearSampler_ || !wrapSampler_ ||
            !bloomThreshold_ || !bloomDownsample_ || !bloomUpsample_ ||
            !glareThreshold_ || !glareAperture_ || !glarePsf_[0] ||
            !glarePsf_[1] || !glarePsf_[2] || !glareFftRowForward_ ||
            !glareFftColumnForward_ || !glareConvolveRowInverse_ ||
            !glareFftColumnInverse_ || !glareComposite_) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Bloom/Glare GPU program creation failed (HRESULT=0x{:08X}); the native HDR composite remains available.",
                static_cast<std::uint32_t>(result));
            return;
        }
        gpuReady_.store(true, std::memory_order_release);
        logging::info(
            "Bloom/Glare GPU programs are ready; stereo size resources remain lazy until the verified HDR composite supplies its scene texture.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        bloomEnabled_.store(safe.bloom.enabled, std::memory_order_release);
        bloomThresholdEV_.store(
            safe.bloom.thresholdEV, std::memory_order_relaxed);
        bloomIntensity_.store(
            safe.bloom.intensity, std::memory_order_relaxed);
        bloomRadius_.store(safe.bloom.radius, std::memory_order_relaxed);
        glareEnabled_.store(safe.glare.enabled, std::memory_order_release);
        glareThresholdEV_.store(
            safe.glare.thresholdEV, std::memory_order_relaxed);
        glareIntensity_.store(
            safe.glare.intensity, std::memory_order_relaxed);
        glareFftResolution_.store(
            safe.glare.fftResolution, std::memory_order_relaxed);
        glarePaddingRatio_.store(
            safe.glare.paddingRatio, std::memory_order_relaxed);
        glareApertureMode_.store(
            safe.glare.apertureMode, std::memory_order_relaxed);
        glareApertureBlades_.store(
            safe.glare.apertureBlades, std::memory_order_relaxed);
        glareApertureRotationDegrees_.store(
            safe.glare.apertureRotationDegrees, std::memory_order_relaxed);
        glareFStop_.store(safe.glare.fStop, std::memory_order_relaxed);
        glareFresnelExponent_.store(
            safe.glare.fresnelExponent, std::memory_order_relaxed);
        glareSphericalAberration_.store(
            safe.glare.sphericalAberration, std::memory_order_relaxed);
        glareChromaticSpread_.store(
            safe.glare.chromaticSpread, std::memory_order_relaxed);
        glareKernelScale_.store(
            safe.glare.kernelScale, std::memory_order_relaxed);
        glarePsfSharpness_.store(
            safe.glare.psfSharpness, std::memory_order_relaxed);
        glarePsfNoiseFloor_.store(
            safe.glare.psfNoiseFloor, std::memory_order_relaxed);
        psfDirty_.store(true, std::memory_order_release);
        filmic_tonemapping::Runtime::get().setOutputFeatureRequested(
            safe.bloom.enabled || safe.glare.enabled);
    }

    bool Runtime::requested() const noexcept
    {
        return bloomEnabled_.load(std::memory_order_acquire) ||
            glareEnabled_.load(std::memory_order_acquire);
    }

    Settings Runtime::currentSettings() const noexcept
    {
        return sanitize({
            .bloom = {
                .enabled = bloomEnabled_.load(std::memory_order_acquire),
                .thresholdEV =
                    bloomThresholdEV_.load(std::memory_order_relaxed),
                .intensity = bloomIntensity_.load(std::memory_order_relaxed),
                .radius = bloomRadius_.load(std::memory_order_relaxed),
            },
            .glare = {
                .enabled = glareEnabled_.load(std::memory_order_acquire),
                .thresholdEV =
                    glareThresholdEV_.load(std::memory_order_relaxed),
                .intensity = glareIntensity_.load(std::memory_order_relaxed),
                .fftResolution =
                    glareFftResolution_.load(std::memory_order_relaxed),
                .paddingRatio =
                    glarePaddingRatio_.load(std::memory_order_relaxed),
                .apertureMode =
                    glareApertureMode_.load(std::memory_order_relaxed),
                .apertureBlades =
                    glareApertureBlades_.load(std::memory_order_relaxed),
                .apertureRotationDegrees =
                    glareApertureRotationDegrees_.load(
                        std::memory_order_relaxed),
                .fStop = glareFStop_.load(std::memory_order_relaxed),
                .fresnelExponent =
                    glareFresnelExponent_.load(std::memory_order_relaxed),
                .sphericalAberration =
                    glareSphericalAberration_.load(
                        std::memory_order_relaxed),
                .chromaticSpread =
                    glareChromaticSpread_.load(std::memory_order_relaxed),
                .kernelScale =
                    glareKernelScale_.load(std::memory_order_relaxed),
                .psfSharpness =
                    glarePsfSharpness_.load(std::memory_order_relaxed),
                .psfNoiseFloor =
                    glarePsfNoiseFloor_.load(std::memory_order_relaxed),
            },
        });
    }

    Runtime::GpuConstants Runtime::constantsFor(
        const Settings& settings,
        std::uint32_t eyeIndex) const noexcept
    {
        return {
            .composite = {},
            .bloom = {
                std::exp2(settings.bloom.thresholdEV - 3.0f),
                settings.bloom.radius,
                1.0f,
                1.0f,
            },
            .glareCore = {
                std::exp2(settings.glare.thresholdEV - 3.0f),
                settings.glare.intensity,
                settings.glare.paddingRatio,
                static_cast<float>(settings.glare.fftResolution),
            },
            .glareScreen = {
                static_cast<float>(width_ / 2u),
                static_cast<float>(height_),
                static_cast<float>(eyeIndex),
                static_cast<float>(settings.glare.apertureMode),
            },
            .glareOptics = {
                static_cast<float>(settings.glare.apertureBlades),
                settings.glare.apertureRotationDegrees *
                    std::numbers::pi_v<float> / 180.0f,
                settings.glare.fresnelExponent,
                1.0f / settings.glare.fStop,
            },
            .glarePsf = {
                settings.glare.sphericalAberration,
                settings.glare.chromaticSpread,
                settings.glare.kernelScale,
                settings.glare.psfSharpness,
            },
            .glareTail = {
                settings.glare.psfNoiseFloor,
                0.0f,
                0.0f,
                0.0f,
            },
        };
    }

    void Runtime::uploadConstants(
        ID3D11DeviceContext* context,
        const GpuConstants& constants) noexcept
    {
        if (context && constants_) {
            context->UpdateSubresource(
                constants_.Get(), 0, nullptr, &constants, 0, 0);
        }
    }

    bool Runtime::ensureSizeResources(
        ID3D11Texture2D* scene,
        const Settings& settings) noexcept
    {
        if (!device_ || !scene) {
            return false;
        }
        D3D11_TEXTURE2D_DESC sceneDescription{};
        scene->GetDesc(&sceneDescription);
        if (sceneDescription.Width < 2u ||
            (sceneDescription.Width & 1u) != 0u ||
            sceneDescription.Height == 0u || sceneDescription.ArraySize != 1u ||
            sceneDescription.SampleDesc.Count != 1u) {
            return false;
        }
        const auto targetFft = settings.glare.fftResolution;
        if (bloomTexture_ && glareOutput_.texture &&
            width_ == sceneDescription.Width &&
            height_ == sceneDescription.Height &&
            fftResolution_ == targetFft) {
            return true;
        }

        releaseSizeResources();
        width_ = sceneDescription.Width;
        height_ = sceneDescription.Height;
        fftResolution_ = targetFft;
        bloomMipCount_ = availableMipCount(width_, height_);

        D3D11_TEXTURE2D_DESC bloomDescription{};
        bloomDescription.Width = width_;
        bloomDescription.Height = height_;
        bloomDescription.MipLevels = bloomMipCount_;
        bloomDescription.ArraySize = 1;
        bloomDescription.Format = kColorFormat;
        bloomDescription.SampleDesc.Count = 1;
        bloomDescription.Usage = D3D11_USAGE_DEFAULT;
        bloomDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        auto result = device_->CreateTexture2D(
            &bloomDescription,
            nullptr,
            bloomTexture_.ReleaseAndGetAddressOf());
        for (std::uint32_t mip = 0;
             SUCCEEDED(result) && mip < bloomMipCount_;
             ++mip) {
            D3D11_SHADER_RESOURCE_VIEW_DESC resourceDescription{};
            resourceDescription.Format = kColorFormat;
            resourceDescription.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;
            resourceDescription.Texture2D.MostDetailedMip = mip;
            resourceDescription.Texture2D.MipLevels = 1;
            result = device_->CreateShaderResourceView(
                bloomTexture_.Get(),
                &resourceDescription,
                bloomResources_[mip].ReleaseAndGetAddressOf());
            if (FAILED(result)) {
                break;
            }
            D3D11_UNORDERED_ACCESS_VIEW_DESC outputDescription{};
            outputDescription.Format = kColorFormat;
            outputDescription.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2D;
            outputDescription.Texture2D.MipSlice = mip;
            result = device_->CreateUnorderedAccessView(
                bloomTexture_.Get(),
                &outputDescription,
                bloomOutputs_[mip].ReleaseAndGetAddressOf());
        }

        const auto createTexture = [this](
                                       UINT width,
                                       UINT height,
                                       DXGI_FORMAT format,
                                       TextureViews& target) noexcept {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            auto createResult = device_->CreateTexture2D(
                &description,
                nullptr,
                target.texture.ReleaseAndGetAddressOf());
            if (SUCCEEDED(createResult)) {
                createResult = device_->CreateShaderResourceView(
                    target.texture.Get(),
                    nullptr,
                    target.resource.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(createResult)) {
                createResult = device_->CreateUnorderedAccessView(
                    target.texture.Get(),
                    nullptr,
                    target.output.ReleaseAndGetAddressOf());
            }
            return createResult;
        };

        for (auto& channel : glareFft_) {
            for (auto& texture : channel) {
                if (SUCCEEDED(result)) {
                    result = createTexture(
                        fftResolution_,
                        fftResolution_,
                        kComplexFormat,
                        texture);
                }
            }
        }
        for (auto& texture : glarePsfFft_) {
            if (SUCCEEDED(result)) {
                result = createTexture(
                    fftResolution_,
                    fftResolution_,
                    kComplexFormat,
                    texture);
            }
        }
        if (SUCCEEDED(result)) {
            result = createTexture(
                width_, height_, kColorFormat, glareOutput_);
        }
        if (FAILED(result) || !bloomResources_[0] || !glareOutput_.resource) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Bloom/Glare stereo resource creation failed for {}x{}, FFT={} (HRESULT=0x{:08X}); private output remains disabled.",
                width_,
                height_,
                fftResolution_,
                static_cast<std::uint32_t>(result));
            releaseSizeResources();
            return false;
        }
        psfDirty_.store(true, std::memory_order_release);
        logging::info(
            "Bloom/Glare allocated a {}x{} packed-stereo bloom chain with {} mips and one shared {}x{} physical-glare FFT workspace processed independently per eye.",
            width_,
            height_,
            bloomMipCount_,
            fftResolution_,
            fftResolution_);
        return true;
    }

    void Runtime::releaseSizeResources() noexcept
    {
        bloomTexture_.Reset();
        for (auto& resource : bloomResources_) {
            resource.Reset();
        }
        for (auto& output : bloomOutputs_) {
            output.Reset();
        }
        for (auto& channel : glareFft_) {
            for (auto& texture : channel) {
                texture = {};
            }
        }
        for (auto& texture : glarePsfFft_) {
            texture = {};
        }
        glareOutput_ = {};
        width_ = 0;
        height_ = 0;
        bloomMipCount_ = 0;
        fftResolution_ = 0;
        psfDirty_.store(true, std::memory_order_release);
    }

    void Runtime::clearComputeViews(ID3D11DeviceContext* context) noexcept
    {
        if (!context) {
            return;
        }
        constexpr std::array<ID3D11ShaderResourceView*, 4> resources{};
        constexpr std::array<ID3D11UnorderedAccessView*, 3> outputs{};
        context->CSSetShaderResources(
            0, static_cast<UINT>(resources.size()), resources.data());
        context->CSSetUnorderedAccessViews(
            0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
    }

    bool Runtime::dispatchBloom(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* scene,
        const Settings& settings) noexcept
    {
        if (!context || !scene || !bloomTexture_ || bloomMipCount_ < 2u) {
            return false;
        }
        auto constants = constantsFor(settings);
        uploadConstants(context, constants);
        auto* constantBuffer = constants_.Get();
        auto* sampler = linearSampler_.Get();
        context->CSSetConstantBuffers(kPrivateConstantSlot, 1, &constantBuffer);
        context->CSSetSamplers(0, 1, &sampler);

        ID3D11ShaderResourceView* source = scene;
        auto* output = bloomOutputs_[0].Get();
        context->CSSetShaderResources(0, 1, &source);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->CSSetShader(bloomThreshold_.Get(), nullptr, 0);
        context->Dispatch(
            dispatchCount(width_, 8), dispatchCount(height_, 8), 1);

        for (std::uint32_t mip = 0; mip + 1u < bloomMipCount_; ++mip) {
            clearComputeViews(context);
            source = bloomResources_[mip].Get();
            output = bloomOutputs_[mip + 1u].Get();
            context->CSSetShaderResources(0, 1, &source);
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->CSSetShader(bloomDownsample_.Get(), nullptr, 0);
            context->Dispatch(
                dispatchCount(std::max(1u, width_ >> (mip + 1u)), 8),
                dispatchCount(std::max(1u, height_ >> (mip + 1u)), 8),
                1);
        }

        for (int mip = static_cast<int>(bloomMipCount_) - 2;
             mip >= 0;
             --mip) {
            clearComputeViews(context);
            constants.bloom[2] = mip == 0 ? settings.bloom.intensity : 1.0f;
            constants.bloom[3] = mip == 0 ? 0.0f : 1.0f;
            uploadConstants(context, constants);
            source = bloomResources_[static_cast<std::size_t>(mip) + 1u].Get();
            output = bloomOutputs_[static_cast<std::size_t>(mip)].Get();
            context->CSSetShaderResources(0, 1, &source);
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->CSSetShader(bloomUpsample_.Get(), nullptr, 0);
            context->Dispatch(
                dispatchCount(std::max(1u, width_ >> mip), 8),
                dispatchCount(std::max(1u, height_ >> mip), 8),
                1);
        }
        clearComputeViews(context);
        bloomDispatches_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Runtime::dispatchFft(
        ID3D11DeviceContext* context,
        ID3D11ComputeShader* shader,
        const TextureViews& input,
        const TextureViews& output,
        const TextureViews* psf) noexcept
    {
        clearComputeViews(context);
        const std::array<ID3D11ShaderResourceView*, 2> resources{
            input.resource.Get(), psf ? psf->resource.Get() : nullptr };
        auto* target = output.output.Get();
        context->CSSetShaderResources(0, psf ? 2u : 1u, resources.data());
        context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
        context->CSSetShader(shader, nullptr, 0);
        context->Dispatch(fftResolution_, 1, 1);
    }

    bool Runtime::generatePsf(
        ID3D11DeviceContext* context,
        const Settings& settings) noexcept
    {
        if (!context || !glareAperture_ || !glareFft_[0][0].output) {
            return false;
        }
        auto constants = constantsFor(settings);
        uploadConstants(context, constants);
        auto* constantBuffer = constants_.Get();
        context->CSSetConstantBuffers(kPrivateConstantSlot, 1, &constantBuffer);

        clearComputeViews(context);
        auto* output = glareFft_[0][0].output.Get();
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->CSSetShader(glareAperture_.Get(), nullptr, 0);
        context->Dispatch(
            dispatchCount(fftResolution_, 8),
            dispatchCount(fftResolution_, 8),
            1);

        dispatchFft(
            context,
            glareFftRowForward_.Get(),
            glareFft_[0][0],
            glareFft_[0][1]);
        dispatchFft(
            context,
            glareFftColumnForward_.Get(),
            glareFft_[0][1],
            glareFft_[0][0]);

        auto* sampler = wrapSampler_.Get();
        context->CSSetSamplers(0, 1, &sampler);
        for (std::size_t channel = 0; channel < kColorChannels; ++channel) {
            clearComputeViews(context);
            auto* source = glareFft_[0][0].resource.Get();
            output = glareFft_[channel][1].output.Get();
            context->CSSetShaderResources(0, 1, &source);
            context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            context->CSSetShader(glarePsf_[channel].Get(), nullptr, 0);
            context->Dispatch(
                dispatchCount(fftResolution_, 8),
                dispatchCount(fftResolution_, 8),
                1);
        }

        for (std::size_t channel = 0; channel < kColorChannels; ++channel) {
            dispatchFft(
                context,
                glareFftRowForward_.Get(),
                glareFft_[channel][1],
                glareFft_[channel][0]);
            dispatchFft(
                context,
                glareFftColumnForward_.Get(),
                glareFft_[channel][0],
                glarePsfFft_[channel]);
        }
        clearComputeViews(context);
        psfDirty_.store(false, std::memory_order_release);
        psfGenerations_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::dispatchGlare(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* scene,
        const Settings& settings) noexcept
    {
        if (!context || !scene || !glareOutput_.output ||
            (psfDirty_.load(std::memory_order_acquire) &&
                !generatePsf(context, settings))) {
            return false;
        }

        for (std::uint32_t eye = 0; eye < 2u; ++eye) {
            auto constants = constantsFor(settings, eye);
            uploadConstants(context, constants);
            auto* constantBuffer = constants_.Get();
            context->CSSetConstantBuffers(
                kPrivateConstantSlot, 1, &constantBuffer);

            clearComputeViews(context);
            std::array<ID3D11UnorderedAccessView*, kColorChannels> outputs{
                glareFft_[0][0].output.Get(),
                glareFft_[1][0].output.Get(),
                glareFft_[2][0].output.Get(),
            };
            auto* source = scene;
            context->CSSetShaderResources(0, 1, &source);
            context->CSSetUnorderedAccessViews(
                0, static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
            context->CSSetShader(glareThreshold_.Get(), nullptr, 0);
            context->Dispatch(
                dispatchCount(fftResolution_, 8),
                dispatchCount(fftResolution_, 8),
                1);

            for (std::size_t channel = 0;
                 channel < kColorChannels;
                 ++channel) {
                dispatchFft(
                    context,
                    glareFftRowForward_.Get(),
                    glareFft_[channel][0],
                    glareFft_[channel][1]);
                dispatchFft(
                    context,
                    glareFftColumnForward_.Get(),
                    glareFft_[channel][1],
                    glareFft_[channel][0]);
            }

            for (std::size_t channel = 0; channel < kColorChannels; ++channel) {
                dispatchFft(context, glareConvolveRowInverse_.Get(),
                    glareFft_[channel][0], glareFft_[channel][1], &glarePsfFft_[channel]);
                dispatchFft(context, glareFftColumnInverse_.Get(),
                    glareFft_[channel][1], glareFft_[channel][0]);
            }

            clearComputeViews(context);
            std::array<ID3D11ShaderResourceView*, 4> resources{
                scene,
                glareFft_[0][0].resource.Get(),
                glareFft_[1][0].resource.Get(),
                glareFft_[2][0].resource.Get(),
            };
            auto* target = glareOutput_.output.Get();
            auto* sampler = linearSampler_.Get();
            context->CSSetShaderResources(
                0,
                static_cast<UINT>(resources.size()),
                resources.data());
            context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
            context->CSSetSamplers(0, 1, &sampler);
            context->CSSetShader(glareComposite_.Get(), nullptr, 0);
            context->Dispatch(
                dispatchCount(width_ / 2u, 8),
                dispatchCount(height_, 8),
                1);
        }
        clearComputeViews(context);
        glareDispatches_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    ScopedBindings Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        bool outputCompositeActive) noexcept
    {
        if (!outputCompositeActive || !context || context != context_.Get() ||
            !constants_) {
            return {};
        }

        const auto settings = currentSettings();
        bool bloomReady{};
        bool glareReady{};
        if ((settings.bloom.enabled || settings.glare.enabled) &&
            gpuReady_.load(std::memory_order_acquire)) {
            ID3D11ShaderResourceView* rawScene{};
            context->PSGetShaderResources(1, 1, &rawScene);
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scene;
            scene.Attach(rawScene);
            Microsoft::WRL::ComPtr<ID3D11Resource> sceneResource;
            if (scene) {
                scene->GetResource(sceneResource.ReleaseAndGetAddressOf());
            }
            Microsoft::WRL::ComPtr<ID3D11Texture2D> sceneTexture;
            if (sceneResource) {
                (void)sceneResource.As(&sceneTexture);
            }
            if (sceneTexture && ensureSizeResources(sceneTexture.Get(), settings)) {
                render::ScopedComputeState computeState(
                    context,
                    {
                        .firstShaderResource = 0,
                        .shaderResourceCount = 4,
                        .firstUnorderedAccess = 0,
                        .unorderedAccessCount = 3,
                        .firstSampler = 0,
                        .samplerCount = 1,
                        .firstConstantBuffer = kPrivateConstantSlot,
                        .constantBufferCount = 1,
                    });
                if (computeState.captured()) {
                    bloomReady = settings.bloom.enabled &&
                        dispatchBloom(context, scene.Get(), settings);
                    glareReady = settings.glare.enabled &&
                        dispatchGlare(context, scene.Get(), settings);
                    clearComputeViews(context);
                    (void)computeState.restore();
                }
            }
        }

        auto finalConstants = constantsFor(settings);
        finalConstants.composite[0] = bloomReady ? 1.0f : 0.0f;
        finalConstants.composite[1] = glareReady ? 1.0f : 0.0f;
        uploadConstants(context, finalConstants);
        if ((bloomReady || glareReady) &&
            !firstDispatchLogged_.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Bloom/Glare completed its first verified HDR transaction; bloom={}, physicalGlare={}, packedStereo={}x{}, FFT={} per eye.",
                bloomReady,
                glareReady,
                width_,
                height_,
                fftResolution_);
        }
        return ScopedBindings(
            this,
            context,
            bloomReady ? bloomResources_[0].Get() : nullptr,
            glareReady ? glareOutput_.resource.Get() : nullptr,
            linearSampler_.Get(),
            constants_.Get());
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = currentSettings(),
            .gpuReady = gpuReady_.load(std::memory_order_acquire),
            .sizeResourcesReady = bloomTexture_ && glareOutput_.texture,
            .width = width_,
            .height = height_,
            .fftResolution = fftResolution_,
            .bloomDispatches =
                bloomDispatches_.load(std::memory_order_relaxed),
            .glareDispatches =
                glareDispatches_.load(std::memory_order_relaxed),
            .psfGenerations =
                psfGenerations_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
