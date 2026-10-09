#include "Features/ibl/IblEnvironmentUpdater.h"

#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace csah::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr std::uint32_t kMinimumSceneConstantRows = 82;
        constexpr UINT kThreadGroupExtent = 8;
        // Ten float4 rows per face, matching ReduceEnvironmentCS.hlsl.
        constexpr UINT kReductionRows = 10;
        constexpr UINT kReductionBytes = kEnvironmentCubeFaceCount * kReductionRows * 16;
        constexpr float kVisibleDirectionHistoryBlend = 0.75F;
        constexpr float kMaximumCaptureDistance = 2000000.0F;

        [[nodiscard]] bool finiteOrigin(const Float3& origin) noexcept
        {
            constexpr auto limit = kMaximumCaptureDistance * 4.0F;
            return std::isfinite(origin.x) && std::isfinite(origin.y) &&
                std::isfinite(origin.z) && std::abs(origin.x) < limit &&
                std::abs(origin.y) < limit && std::abs(origin.z) < limit;
        }

        [[nodiscard]] bool reconstructCameraOrigin(
            const std::array<float, 4>* sceneRows,
            std::uint32_t eye,
            Float3& origin) noexcept
        {
            const auto& cameraRelativeOrigin = sceneRows[59U + eye];
            const auto& adjustment = sceneRows[80U + eye];
            origin = {
                cameraRelativeOrigin[0] + adjustment[0],
                cameraRelativeOrigin[1] + adjustment[1],
                cameraRelativeOrigin[2] + adjustment[2],
            };
            return finiteOrigin(origin);
        }

        [[nodiscard]] bool sameDevice(
            ID3D11DeviceChild* child,
            ID3D11Device* expected) noexcept
        {
            if (!child || !expected) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            child->GetDevice(&device);
            return device.Get() == expected;
        }

        [[nodiscard]] bool textureFromView(
            ID3D11ShaderResourceView* view,
            ComPtr<ID3D11Texture2D>& texture,
            D3D11_SHADER_RESOURCE_VIEW_DESC& viewDescription,
            D3D11_TEXTURE2D_DESC& textureDescription) noexcept
        {
            if (!view) {
                return false;
            }
            view->GetDesc(&viewDescription);
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource || FAILED(resource.As(&texture)) || !texture) {
                return false;
            }
            texture->GetDesc(&textureDescription);
            return true;
        }

        [[nodiscard]] bool createConstantBuffer(
            ID3D11Device* device,
            UINT byteWidth,
            ComPtr<ID3D11Buffer>& buffer) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = byteWidth;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            return SUCCEEDED(device->CreateBuffer(
                &description,
                nullptr,
                &buffer));
        }

        [[nodiscard]] bool createCapturedCube(
            ID3D11Device* device,
            std::uint32_t extent,
            DXGI_FORMAT format,
            ComPtr<ID3D11Texture2D>& texture,
            ComPtr<ID3D11ShaderResourceView>& shaderResource,
            ComPtr<ID3D11UnorderedAccessView>& unorderedAccess) noexcept
        {
            D3D11_TEXTURE2D_DESC textureDescription{};
            textureDescription.Width = extent;
            textureDescription.Height = extent;
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = kEnvironmentCubeFaceCount;
            textureDescription.Format = format;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            textureDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            if (FAILED(device->CreateTexture2D(
                    &textureDescription,
                    nullptr,
                    &texture))) {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription{};
            shaderResourceDescription.Format = format;
            shaderResourceDescription.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURECUBE;
            shaderResourceDescription.TextureCube.MostDetailedMip = 0;
            shaderResourceDescription.TextureCube.MipLevels = 1;
            if (FAILED(device->CreateShaderResourceView(
                    texture.Get(),
                    &shaderResourceDescription,
                    &shaderResource))) {
                return false;
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC unorderedAccessDescription{};
            unorderedAccessDescription.Format = format;
            unorderedAccessDescription.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            unorderedAccessDescription.Texture2DArray.MipSlice = 0;
            unorderedAccessDescription.Texture2DArray.FirstArraySlice = 0;
            unorderedAccessDescription.Texture2DArray.ArraySize =
                kEnvironmentCubeFaceCount;
            return SUCCEEDED(device->CreateUnorderedAccessView(
                texture.Get(),
                &unorderedAccessDescription,
                &unorderedAccess));
        }

        [[nodiscard]] bool createReductionBuffer(
            ID3D11Device* device, UINT elements,
            ComPtr<ID3D11Buffer>& buffer,
            ComPtr<ID3D11UnorderedAccessView>& output,
            ComPtr<ID3D11ShaderResourceView>* view = nullptr) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = elements * 16;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_UNORDERED_ACCESS |
                (view ? D3D11_BIND_SHADER_RESOURCE : 0);
            description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            description.StructureByteStride = 16;
            if (FAILED(device->CreateBuffer(&description, nullptr, &buffer))) return false;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements = elements;
            if (FAILED(device->CreateUnorderedAccessView(buffer.Get(), &uav, &output))) return false;
            if (view) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                srv.Buffer.NumElements = elements;
                if (FAILED(device->CreateShaderResourceView(buffer.Get(), &srv, view->GetAddressOf()))) return false;
            }
            return true;
        }

    }

    bool EnvironmentUpdater::initialize(
        ID3D11Device* device,
        const void* captureShaderBytecode,
        std::size_t captureShaderBytecodeSize,
        const void* filterShaderBytecode,
        std::size_t filterShaderBytecodeSize,
        const void* reductionShaderBytecode,
        std::size_t reductionShaderBytecodeSize,
        std::uint32_t extent) noexcept
    {
        if (!device || !captureShaderBytecode ||
            captureShaderBytecodeSize < 20 ||
            std::memcmp(captureShaderBytecode, "DXBC", 4) != 0 ||
            !filterShaderBytecode || filterShaderBytecodeSize < 20 ||
            std::memcmp(filterShaderBytecode, "DXBC", 4) != 0 ||
            !reductionShaderBytecode || reductionShaderBytecodeSize < 20 ||
            std::memcmp(reductionShaderBytecode, "DXBC", 4) != 0 ||
            extent < 16 || extent > 512 || !std::has_single_bit(extent)) {
            recordFailure();
            return false;
        }

        Resources candidate{};
        candidate.device = device;
        candidate.extent = extent;
        if (FAILED(device->CreateComputeShader(
                captureShaderBytecode,
                captureShaderBytecodeSize,
                nullptr,
                &candidate.captureShader)) ||
            FAILED(device->CreateComputeShader(
                filterShaderBytecode,
                filterShaderBytecodeSize,
                nullptr,
                &candidate.filterShader)) ||
            FAILED(device->CreateComputeShader(reductionShaderBytecode,
                reductionShaderBytecodeSize, nullptr, &candidate.reductionShader))) {
            recordFailure();
            return false;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(
                &samplerDescription,
                &candidate.sampler)) ||
            !createConstantBuffer(
                device,
                sizeof(UpdateConstants),
                candidate.captureConstants) ||
            !createConstantBuffer(
                device,
                sizeof(FilterConstants),
                candidate.filterConstants) ||
            !createCapturedCube(
                device,
                extent,
                EnvironmentProvider::kFormat,
                candidate.capturedRadiance,
                candidate.capturedRadianceView,
                candidate.capturedRadianceOutput) ||
            !createCapturedCube(
                device,
                extent,
                EnvironmentProvider::kValidityFormat,
                candidate.capturedValidity,
                candidate.capturedValidityView,
                candidate.capturedValidityOutput) ||
            !createConstantBuffer(device, 16, candidate.reductionConstants) ||
            !createReductionBuffer(device, (extent / 8) * (extent / 8) *
                kEnvironmentCubeFaceCount * kReductionRows, candidate.reductionPartials,
                candidate.reductionPartialsOutput, &candidate.reductionPartialsView) ||
            !createReductionBuffer(device, kEnvironmentCubeFaceCount * kReductionRows,
                candidate.reductionResult, candidate.reductionResultOutput)) {
            recordFailure();
            return false;
        }

        D3D11_BUFFER_DESC stagingDescription{};
        stagingDescription.ByteWidth = kReductionBytes;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateBuffer(&stagingDescription, nullptr, &candidate.stagingReduction))) {
            recordFailure();
            return false;
        }

        for (auto& sky : candidate.sky) {
            if (!createCapturedCube(device, extent, DXGI_FORMAT_R32G32B32A32_FLOAT,
                    sky.texture, sky.view, sky.output)) {
                recordFailure();
                return false;
            }
        }
        for (auto* ownership : { &candidate.nearestDepth, &candidate.nearestOwner }) {
            if (!createCapturedCube(device, extent, DXGI_FORMAT_R32_UINT,
                    ownership->texture, ownership->view, ownership->output)) {
                recordFailure();
                return false;
            }
        }
        D3D11_QUERY_DESC queryDescription{ D3D11_QUERY_EVENT, 0 };
        if (FAILED(device->CreateQuery(
                &queryDescription,
                &candidate.completionEvent))) {
            recordFailure();
            return false;
        }

        skyFront_ = 0;
        resources_ = std::move(candidate);
        summary_ = {};
        summary_.initialized = true;
        gpuTiming_.reset();
        gpuTimingInitializationAttempted_ = false;
        validationCpuMilliseconds_ = 0.0;
        validationCpuSamples_ = 0;
        firstValidationTimingLogged_ = false;
        return true;
    }

    void EnvironmentUpdater::reset() noexcept
    {
        skyFront_ = 0;
        resources_ = {};
        summary_ = {};
        gpuTiming_.reset();
        gpuTimingInitializationAttempted_ = false;
        validationCpuMilliseconds_ = 0.0;
        validationCpuSamples_ = 0;
        firstValidationTimingLogged_ = false;
    }

    bool EnvironmentUpdater::validateInputs(
        ID3D11DeviceContext* context,
        const EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants,
        bool usePublishedHistory,
        D3D11_TEXTURE2D_DESC& radianceDescription) const noexcept
    {
        if (!summary_.initialized || summary_.pending || !context ||
            !resources_.device || !sameDevice(context, resources_.device.Get()) ||
            !sameDevice(reflectionFreeRadiance, resources_.device.Get()) ||
            !sameDevice(sceneDepth, resources_.device.Get()) ||
            !sameDevice(sceneConstants, resources_.device.Get())) {
            return false;
        }

        if (usePublishedHistory &&
            (!sameDevice(
                 provider.publishedEnvironment(),
                 resources_.device.Get()) ||
                !sameDevice(
                    provider.publishedValidity(),
                    resources_.device.Get()) ||
                !sameDevice(
                    provider.publishedPosition(),
                    resources_.device.Get()))) {
            return false;
        }

        const auto providerSnapshot = provider.snapshot();
        if (providerSnapshot.state != EnvironmentProviderState::ready ||
            providerSnapshot.extent != resources_.extent ||
            providerSnapshot.mipCount == 0 ||
            providerSnapshot.mipCount > kEnvironmentMaximumMipCount) {
            return false;
        }

        ComPtr<ID3D11Texture2D> radianceTexture;
        D3D11_SHADER_RESOURCE_VIEW_DESC radianceView{};
        if (!textureFromView(
                reflectionFreeRadiance,
                radianceTexture,
                radianceView,
                radianceDescription) ||
            radianceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            (radianceView.Format != DXGI_FORMAT_R11G11B10_FLOAT &&
                radianceView.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
            radianceDescription.Format != radianceView.Format ||
            radianceDescription.ArraySize != 1 ||
            radianceDescription.SampleDesc.Count != 1 ||
            radianceDescription.Width < 2 || radianceDescription.Height == 0 ||
            (radianceDescription.Width & 1U) != 0) {
            return false;
        }

        ComPtr<ID3D11Texture2D> depthTexture;
        D3D11_SHADER_RESOURCE_VIEW_DESC depthView{};
        D3D11_TEXTURE2D_DESC depthDescription{};
        if (!textureFromView(
                sceneDepth,
                depthTexture,
                depthView,
                depthDescription) ||
            depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            depthView.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
            depthDescription.Format != DXGI_FORMAT_R24G8_TYPELESS ||
            depthDescription.Width != radianceDescription.Width ||
            depthDescription.Height != radianceDescription.Height ||
            depthDescription.ArraySize != 1 ||
            depthDescription.SampleDesc.Count != 1) {
            return false;
        }

        D3D11_BUFFER_DESC constantDescription{};
        sceneConstants->GetDesc(&constantDescription);
        return constantDescription.ByteWidth >=
                kMinimumSceneConstantRows * sizeof(std::array<float, 4>) &&
            (constantDescription.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
    }

    bool EnvironmentUpdater::prepareSceneConstantsReadback(
        ID3D11Buffer* sceneConstants) noexcept
    {
        if (!sceneConstants || !resources_.device) {
            return false;
        }
        D3D11_BUFFER_DESC sourceDescription{};
        sceneConstants->GetDesc(&sourceDescription);
        if (resources_.stagingSceneConstants &&
            resources_.sceneConstantsByteWidth ==
                sourceDescription.ByteWidth) {
            return true;
        }

        D3D11_BUFFER_DESC stagingDescription{};
        stagingDescription.ByteWidth = sourceDescription.ByteWidth;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> candidate;
        if (FAILED(resources_.device->CreateBuffer(
                &stagingDescription,
                nullptr,
                &candidate)) ||
            !candidate) {
            return false;
        }
        resources_.stagingSceneConstants = std::move(candidate);
        resources_.sceneConstantsByteWidth = sourceDescription.ByteWidth;
        return true;
    }

    bool EnvironmentUpdater::dispatchUpdate(
        ID3D11DeviceContext* context,
        EnvironmentProvider& provider,
        ID3D11ShaderResourceView* reflectionFreeRadiance,
        ID3D11ShaderResourceView* sceneDepth,
        ID3D11Buffer* sceneConstants,
        bool usePublishedHistory,
        bool fitDiffuse) noexcept
    {
        D3D11_TEXTURE2D_DESC sourceDescription{};
        if (!validateInputs(
                context,
                provider,
                reflectionFreeRadiance,
                sceneDepth,
                sceneConstants,
                usePublishedHistory,
                sourceDescription) ||
            !provider.beginUpdate()) {
            recordFailure();
            return false;
        }
        if (!prepareSceneConstantsReadback(sceneConstants)) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }

        render::ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 6,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 6,
                .firstSampler = 0,
                .samplerCount = 1,
                .firstConstantBuffer = 11,
                .constantBufferCount = 2,
            });
        if (!restore.captured()) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        if (!gpuTimingInitializationAttempted_) {
            gpuTimingInitializationAttempted_ = true;
            if (!gpuTiming_.initialize(
                    resources_.device.Get(),
                    context,
                    "Image Based Lighting",
                    { "capture projection", "GGX filtering",
                        "validation reduction", nullptr },
                    3,
                    5)) {
                logging::warn(
                    "IBL could not allocate image-neutral GPU timing queries; rendering remains active without performance telemetry.");
            }
        }
        auto timing = gpuTiming_.begin();

        std::array<ID3D11ShaderResourceView*, 6> captureSources{
            reflectionFreeRadiance,
            sceneDepth,
            usePublishedHistory ? provider.publishedEnvironment() : nullptr,
            usePublishedHistory ? provider.publishedValidity() : nullptr,
            usePublishedHistory ? provider.publishedPosition() : nullptr,
            usePublishedHistory ? resources_.sky[skyFront_].view.Get() : nullptr,
        };
        std::array<ID3D11UnorderedAccessView*, 6> captureDestinations{
            resources_.capturedRadianceOutput.Get(),
            resources_.capturedValidityOutput.Get(),
            provider.writablePosition(),
            resources_.sky[1 - skyFront_].output.Get(),
            resources_.nearestDepth.output.Get(),
            resources_.nearestOwner.output.Get(),
        };
        if (!captureDestinations[2]) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        std::array<ID3D11Buffer*, 2> captureConstants{
            resources_.captureConstants.Get(),
            sceneConstants,
        };
        const auto previousProbeOrigin = usePublishedHistory ?
            provider.publishedProbeOrigin() : EnvironmentProbeOrigin{};
        UpdateConstants updateConstants{
            sourceDescription.Width,
            sourceDescription.Height,
            resources_.extent,
            usePublishedHistory ? 1U : 0U,
            0U,
            kVisibleDirectionHistoryBlend,
            {},
            previousProbeOrigin.position,
            previousProbeOrigin.valid ? 1U : 0U,
        };
        context->UpdateSubresource(
            resources_.captureConstants.Get(),
            0,
            nullptr,
            &updateConstants,
            0,
            0);
        auto* sampler = resources_.sampler.Get();
        context->CSSetShader(resources_.captureShader.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(captureSources.size()),
            captureSources.data());
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(captureDestinations.size()),
            captureDestinations.data(),
            nullptr);
        context->CSSetSamplers(0, 1, &sampler);
        context->CSSetConstantBuffers(
            11,
            static_cast<UINT>(captureConstants.size()),
            captureConstants.data());
        constexpr std::array<UINT, 4> unowned{ UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
        context->ClearUnorderedAccessViewUint(resources_.nearestDepth.output.Get(), unowned.data());
        context->ClearUnorderedAccessViewUint(resources_.nearestOwner.output.Get(), unowned.data());
        // Separate dispatches establish depth ownership before any colour is written.
        for (std::uint32_t pass = usePublishedHistory ? 0U : 2U; pass < 3U; ++pass) {
            updateConstants.updatePass = pass;
            context->UpdateSubresource(resources_.captureConstants.Get(), 0,
                nullptr, &updateConstants, 0, 0);
            context->Dispatch(
                (resources_.extent + kThreadGroupExtent - 1) / kThreadGroupExtent,
                (resources_.extent + kThreadGroupExtent - 1) / kThreadGroupExtent,
                kEnvironmentCubeFaceCount);
        }

        std::array<ID3D11UnorderedAccessView*, 6> nullDestinations{};
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(nullDestinations.size()),
            nullDestinations.data(),
            nullptr);
        std::array<ID3D11ShaderResourceView*, 6> nullSources{};
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());
        timing.mark();

        std::array<ID3D11ShaderResourceView*, 2> filterSources{
            resources_.capturedRadianceView.Get(),
            resources_.capturedValidityView.Get(),
        };
        context->CSSetShader(resources_.filterShader.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(filterSources.size()),
            filterSources.data());
        auto* filterConstants = resources_.filterConstants.Get();
        context->CSSetConstantBuffers(11, 1, &filterConstants);

        const auto providerSnapshot = provider.snapshot();
        bool completed = true;
        for (std::uint32_t mipLevel = 0;
             mipLevel < providerSnapshot.mipCount;
             ++mipLevel) {
            const auto targetExtent = std::max(
                1U,
                providerSnapshot.extent >> mipLevel);
            const FilterConstants constants{
                targetExtent,
                mipLevel,
                providerSnapshot.mipCount,
                providerSnapshot.mipCount > 1 ?
                    static_cast<float>(mipLevel) /
                        static_cast<float>(providerSnapshot.mipCount - 1) :
                    0.0f,
            };
            context->UpdateSubresource(
                resources_.filterConstants.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);
            std::array<ID3D11UnorderedAccessView*, 2> destinations{
                provider.writableMip(mipLevel),
                provider.writableValidityMip(mipLevel),
            };
            if (!destinations[0] || !destinations[1]) {
                completed = false;
                break;
            }
            context->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(destinations.size()),
                destinations.data(),
                nullptr);
            context->Dispatch(
                (targetExtent + kThreadGroupExtent - 1) /
                    kThreadGroupExtent,
                (targetExtent + kThreadGroupExtent - 1) /
                    kThreadGroupExtent,
                kEnvironmentCubeFaceCount);
            for (std::uint32_t face = 0;
                 face < kEnvironmentCubeFaceCount;
                 ++face) {
                completed = provider.markSubresourceComplete(
                                static_cast<EnvironmentCubeFace>(face),
                                mipLevel) &&
                    completed;
            }
        }

        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(nullDestinations.size()),
            nullDestinations.data(),
            nullptr);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(nullSources.size()),
            nullSources.data());
        timing.mark();
        if (!completed) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }

        std::array<ID3D11ShaderResourceView*, 3> reductionSources{
            provider.writableRadianceArray(), provider.writableValidityArray(), nullptr };
        if (!reductionSources[0] || !reductionSources[1]) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        const auto groupCount = resources_.extent / kThreadGroupExtent;
        std::array<UINT, 4> reductionConstants{resources_.extent, groupCount, 0, fitDiffuse ? 1U : 0U};
        context->UpdateSubresource(resources_.reductionConstants.Get(), 0, nullptr, reductionConstants.data(), 0, 0);
        auto* reductionBuffer = resources_.reductionConstants.Get();
        auto* reductionOutput = resources_.reductionPartialsOutput.Get();
        context->CSSetShader(resources_.reductionShader.Get(), nullptr, 0);
        context->CSSetConstantBuffers(11, 1, &reductionBuffer);
        context->CSSetShaderResources(0, 3, reductionSources.data());
        context->CSSetUnorderedAccessViews(0, 1, &reductionOutput, nullptr);
        context->Dispatch(groupCount, groupCount, kEnvironmentCubeFaceCount);
        context->CSSetUnorderedAccessViews(0, 1, nullDestinations.data(), nullptr);

        reductionSources = {nullptr, nullptr, resources_.reductionPartialsView.Get()};
        reductionConstants[2] = 1;
        context->UpdateSubresource(resources_.reductionConstants.Get(), 0, nullptr, reductionConstants.data(), 0, 0);
        reductionOutput = resources_.reductionResultOutput.Get();
        context->CSSetShaderResources(0, 3, reductionSources.data());
        context->CSSetUnorderedAccessViews(0, 1, &reductionOutput, nullptr);
        context->Dispatch(1, 1, kEnvironmentCubeFaceCount);
        context->CSSetUnorderedAccessViews(0, 1, nullDestinations.data(), nullptr);
        context->CSSetShaderResources(0, 3, nullSources.data());
        if (!restore.restore()) {
            provider.abortUpdate();
            recordFailure();
            return false;
        }
        context->CopyResource(resources_.stagingReduction.Get(), resources_.reductionResult.Get());
        context->CopyResource(
            resources_.stagingSceneConstants.Get(),
            sceneConstants);
        context->End(resources_.completionEvent.Get());

        pendingDiffuseFit_ = fitDiffuse;
        summary_.pending = true;
        summary_.historyUsed = usePublishedHistory;
        summary_.generation = provider.snapshot().activeUpdateGeneration;
        ++summary_.dispatches;
        return true;
    }

    EnvironmentUpdateConsumeResult EnvironmentUpdater::consumeUpdate(
        ID3D11DeviceContext* context,
        EnvironmentProvider& provider) noexcept
    {
        if (!summary_.pending) {
            return EnvironmentUpdateConsumeResult::idle;
        }
        const auto providerSnapshot = provider.snapshot();
        if (!context || !resources_.device ||
            !sameDevice(context, resources_.device.Get()) ||
            providerSnapshot.state != EnvironmentProviderState::updating ||
            providerSnapshot.activeUpdateGeneration != summary_.generation) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }

        BOOL complete{};
        const auto queryResult = context->GetData(
            resources_.completionEvent.Get(),
            &complete,
            sizeof(complete),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (queryResult == S_FALSE || (SUCCEEDED(queryResult) && complete != TRUE)) {
            return EnvironmentUpdateConsumeResult::pending;
        }
        if (FAILED(queryResult)) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }
        const auto validationStart = std::chrono::steady_clock::now();
        summary_.probeOrigin = {};
        if (resources_.stagingSceneConstants &&
            resources_.sceneConstantsByteWidth >=
                kMinimumSceneConstantRows * sizeof(std::array<float, 4>)) {
            D3D11_MAPPED_SUBRESOURCE mappedSceneConstants{};
            const auto mapResult = context->Map(
                    resources_.stagingSceneConstants.Get(),
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mappedSceneConstants);
            if (SUCCEEDED(mapResult)) {
                if (mappedSceneConstants.pData) {
                    const auto* rows =
                        static_cast<const std::array<float, 4>*>(
                            mappedSceneConstants.pData);
                    Float3 leftOrigin{};
                    Float3 rightOrigin{};
                    const auto leftValid = reconstructCameraOrigin(
                        rows,
                        0,
                        leftOrigin);
                    const auto rightValid = reconstructCameraOrigin(
                        rows,
                        1,
                        rightOrigin);
                    if (leftValid || rightValid) {
                        const auto total = leftValid && rightValid ?
                            2.0F : 1.0F;
                        summary_.probeOrigin.position = {
                            ((leftValid ? leftOrigin.x : 0.0F) +
                                (rightValid ? rightOrigin.x : 0.0F)) / total,
                            ((leftValid ? leftOrigin.y : 0.0F) +
                                (rightValid ? rightOrigin.y : 0.0F)) / total,
                            ((leftValid ? leftOrigin.z : 0.0F) +
                                (rightValid ? rightOrigin.z : 0.0F)) / total,
                        };
                        summary_.probeOrigin.valid = true;
                    }
                }
                context->Unmap(
                    resources_.stagingSceneConstants.Get(),
                    0);
            }
        }
        double red{};
        double green{};
        double blue{};
        double validityTotal{};
        std::array<double, kEnvironmentCubeFaceCount> faceLuminance{};
        std::array<double, kEnvironmentCubeFaceCount> faceValidity{};
        std::array<double, kEnvironmentCubeFaceCount> faceSolidAngle{};
        float peak{};
        std::uint32_t nonBlack{};
        std::uint32_t covered{};
        DiffuseSHFit diffuseFit{};
        bool validGeneration = true;
        const auto faceSampleCount = resources_.extent * resources_.extent;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto mapResult = context->Map(resources_.stagingReduction.Get(), 0,
            D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING) {
            return EnvironmentUpdateConsumeResult::pending;
        }
        if (SUCCEEDED(mapResult)) {
            if (mapped.pData) {
                const auto* faces = static_cast<const std::array<float, 40>*>(mapped.pData);
                for (UINT face = 0; face < kEnvironmentCubeFaceCount; ++face) {
                    const auto& values = faces[face];
                    // Counts are exact floats: at most 512*512 samples per face.
                    const bool valid = std::all_of(values.begin(), values.end(),
                        [](float value) { return std::isfinite(value); }) &&
                        values[10] == 0 && values[11] == static_cast<float>(faceSampleCount) &&
                        values[6] > 0 && values[8] >= 0 && values[8] <= values[11] &&
                        values[9] >= 0 && values[9] <= values[11];
                    if (!valid) {
                        validGeneration = false;
                        continue;
                    }
                    red += values[0];
                    green += values[1];
                    blue += values[2];
                    validityTotal += values[3];
                    faceLuminance[face] = values[4];
                    faceValidity[face] = values[5];
                    faceSolidAngle[face] = values[6];
                    peak = std::max(peak, values[7]);
                    nonBlack += static_cast<std::uint32_t>(values[8]);
                    covered += static_cast<std::uint32_t>(values[9]);
                    if (pendingDiffuseFit_) {
                        diffuseFit.observedSolidAngle += values[5];
                        diffuseFit.totalSolidAngle += values[6];
                        for (std::size_t row = 0; row < 4; ++row)
                            for (std::size_t column = 0; column < 4; ++column)
                                diffuseFit.normal[row][column] += values[12 + row * 4 + column];
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            for (std::size_t coefficient = 0; coefficient < 4; ++coefficient)
                                diffuseFit.radiance[channel][coefficient] += values[28 + channel * 4 + coefficient];
                    }
                }
            } else validGeneration = false;
            context->Unmap(resources_.stagingReduction.Get(), 0);
        } else validGeneration = false;

        const auto totalSamples = faceSampleCount * kEnvironmentCubeFaceCount;
        // Empty but valid coverage is meaningful after a teleport or session
        // reset: publish it so consumers use native cubemaps, not the old room.
        validGeneration = validGeneration && summary_.probeOrigin.valid;
        if (!validGeneration ||
            !provider.publishUpdate(summary_.probeOrigin)) {
            provider.abortUpdate();
            summary_.pending = false;
            recordFailure();
            return EnvironmentUpdateConsumeResult::failed;
        }

        const auto inverseTotal = 1.0 / static_cast<double>(totalSamples);
        summary_.average = {
            static_cast<float>(red * inverseTotal),
            static_cast<float>(green * inverseTotal),
            static_cast<float>(blue * inverseTotal),
        };
        for (std::uint32_t face = 0; face < kEnvironmentCubeFaceCount; ++face) {
            summary_.faceAverageLuminance[face] = static_cast<float>(
                faceLuminance[face] / static_cast<double>(faceSampleCount));
            summary_.faceAverageValidity[face] = faceSolidAngle[face] > 0.0 ?
                static_cast<float>(
                    faceValidity[face] / faceSolidAngle[face]) :
                0.0f;
        }
        summary_.peak = peak;
        summary_.averageValidity = static_cast<float>(
            validityTotal * inverseTotal);
        summary_.nonBlackSamples = nonBlack;
        summary_.coveredSamples = covered;
        summary_.sampleCount = totalSamples;
        summary_.diffuseUpdated = pendingDiffuseFit_;
        if (pendingDiffuseFit_) {
            summary_.diffuseSHCoverage = diffuseSHFitCoverage(diffuseFit);
            summary_.diffuseSH = {};
            summary_.diffuseSHState = solveDiffuseSHFit(diffuseFit, summary_.diffuseSH) ?
                classifyDiffuseSH(summary_.diffuseSH) : DiffuseSHState::invalid;
        }
        summary_.validationCpuMilliseconds =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - validationStart)
                .count();
        validationCpuMilliseconds_ += summary_.validationCpuMilliseconds;
        ++validationCpuSamples_;
        if (!firstValidationTimingLogged_ ||
            validationCpuSamples_ >= 300) {
            logging::info(
                "Feature timing Image Based Lighting validation: CPU compact readback and diffuse SH solve={:.3f} ms over {} samples.",
                validationCpuMilliseconds_ /
                    static_cast<double>(validationCpuSamples_),
                validationCpuSamples_);
            firstValidationTimingLogged_ = true;
            validationCpuMilliseconds_ = 0.0;
            validationCpuSamples_ = 0;
        }
        summary_.pending = false;
        ++summary_.completedReadbacks;
        skyFront_ = 1 - skyFront_;
        ++summary_.publishedUpdates;
        return EnvironmentUpdateConsumeResult::completed;
    }

    void EnvironmentUpdater::recordFailure() noexcept
    {
        ++summary_.failedUpdates;
    }
}
