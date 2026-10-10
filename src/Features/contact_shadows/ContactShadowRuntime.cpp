#include "Features/contact_shadows/ContactShadowRuntime.h"

#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"

#include "ContactShadowDispatchCS.h"
#include "ContactShadowMaskCS.h"
#include "ContactShadowResolveCS.h"
#include "ContactShadowsDFLight.h"

#include <cstring>
#include <limits>
#include <utility>

namespace csah::contact_shadows
{
    namespace
    {
        constexpr std::size_t kInvalidContractIndex =
            std::numeric_limits<std::size_t>::max();
        constexpr UINT kDepthSlot = 3;
        constexpr UINT kMaskSlot = 46;
        constexpr UINT kConstantSlot = 13;
        constexpr UINT kCloudSamplerSlot = 0;
        constexpr UINT kDFLightConstantSlot = 2;
        constexpr UINT kStereoConstantSlot = 8;
        constexpr UINT kCameraConstantSlot = 12;
        constexpr UINT kThreadGroupWidth = 8;
        constexpr UINT kThreadGroupHeight = 8;
        constexpr UINT kDispatchRecordCount = 16;
        constexpr UINT kDispatchRecordStride = 32;
        constexpr UINT kDispatchArgumentStride = sizeof(UINT) * 3;

        struct alignas(16) GpuSettings
        {
            float strength{};
            float maxDistance{};
            float thickness{};
            float sampleCount{};
            float minimumThickness{ 0.35f };
            float selfIntersectionBias{ 0.08f };
            float outerSampleScale{ 0.50f };
            float foveated{};
            float fadeDistance{};
            float reserved0{};
            float reserved1{};
            float reserved2{};
            float cloudEnabled{};
            float cloudOpacity{};
            float cloudHeightUnits{ 10000.0f };
            float planetRadiusUnits{ 446148448.0f };
            float viewport[4]{};
        };
        static_assert(sizeof(GpuSettings) == 80);

        [[nodiscard]] std::size_t matchingContractIndex(
            const void* bytecode,
            SIZE_T bytecodeLength) noexcept
        {
            if (!bytecode || bytecodeLength < 20 ||
                std::memcmp(bytecode, "DXBC", 4) != 0) {
                return kInvalidContractIndex;
            }
            for (std::size_t index = 0;
                 index < csah_contact_shadow_dflight_contracts.size();
                 ++index) {
                const auto& contract =
                    csah_contact_shadow_dflight_contracts[index];
                if (bytecodeLength == contract.originalSize &&
                    std::memcmp(
                        static_cast<const std::byte*>(bytecode) + 4,
                        contract.originalChecksum.data(),
                        contract.originalChecksum.size()) == 0) {
                    return index;
                }
            }
            return kInvalidContractIndex;
        }
    }

    ScopedDrawBindings::ScopedDrawBindings(
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        ID3D11ShaderResourceView* mask,
        render::GpuTimingProfiler* drawTiming,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context),
        drawTiming_(drawTiming ? drawTiming->begin() :
                                 render::GpuTimingProfiler::Scope{}),
        restoreCounter_(restoreCounter)
    {
        if (!context_ || !constants) {
            context_ = nullptr;
            return;
        }
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            previousConstants_.GetAddressOf());
        context_->PSGetShaderResources(
            kMaskSlot,
            1,
            previousMask_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        context_->PSSetShaderResources(kMaskSlot, 1, &mask);
    }

    ScopedDrawBindings::~ScopedDrawBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previousConstant = previousConstants_.Get();
        auto* previousMask = previousMask_.Get();
        context_->PSSetShaderResources(kMaskSlot, 1, &previousMask);
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previousConstant);
        if (restoreCounter_) {
            restoreCounter_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        gpuTiming_.reset();
        drawGpuTiming_.reset();
        device_ = device;
        context_ = context;
        for (auto& replacement : replacements_) {
            replacement.Reset();
        }
        for (auto& contractDiagnostics : directionalDiagnostics_) {
            for (auto& diagnostic : contractDiagnostics) {
                diagnostic.Reset();
            }
        }
        dispatchCompute_.Reset();
        maskCompute_.Reset();
        resolveCompute_.Reset();
        constants_.Reset();
        dispatchRecords_.Reset();
        dispatchRecordsView_.Reset();
        dispatchRecordsOutput_.Reset();
        dispatchArguments_.Reset();
        dispatchArgumentsOutput_.Reset();
        rawMaskTexture_.Reset();
        rawMaskView_.Reset();
        rawMaskOutput_.Reset();
        maskTexture_.Reset();
        maskView_.Reset();
        maskOutput_.Reset();
        maskWidth_ = 0;
        maskHeight_ = 0;
        for (auto& original : originals_) {
            original.shader.Reset();
            original.contractIndex = 0;
        }
        trackedShaders_.store(0, std::memory_order_relaxed);
        firstMatchLogged_.store(false, std::memory_order_relaxed);
        firstReplacementBindLogged_.store(false, std::memory_order_relaxed);
        firstDiagnosticBindLogged_.store(false, std::memory_order_relaxed);
        firstDispatchLogged_.store(false, std::memory_order_relaxed);
        firstDispatchFailureLogged_.store(false, std::memory_order_relaxed);
        uploadedRevision_ = 0;
        uploadedContactActive_ = false;
        uploadedMaskActive_ = false;
        uploadedCloudActive_ = false;
        uploadedCloudOpacity_ = 0.0f;
        uploadedGpuSettings_.fill(0.0f);
        if (!device || !context || !createPixelShader) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        static_assert(
            csah_contact_shadow_dflight_contracts.size() <=
            kMaximumShaderContracts);
        bool replacementCreationFailed{};
        bool diagnosticCreationFailed{};
        for (std::size_t index = 0;
             index < csah_contact_shadow_dflight_contracts.size();
             ++index) {
            const auto& contract =
                csah_contact_shadow_dflight_contracts[index];
            ID3D11PixelShader* replacement{};
            const auto pixelShaderResult = createPixelShader(
                device,
                contract.replacementBytecode,
                contract.replacementBytecodeLength,
                nullptr,
                &replacement);
            if (FAILED(pixelShaderResult) || !replacement) {
                replacementCreationFailed = true;
                failures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Contact Shadows DFLight contract {} replacement creation failed (HRESULT=0x{:08X}).",
                    index,
                    static_cast<std::uint32_t>(pixelShaderResult));
                continue;
            }
            replacements_[index].Attach(replacement);
            for (std::size_t modeIndex = 0;
                 modeIndex < contract.diagnosticBytecode.size();
                 ++modeIndex) {
                ID3D11PixelShader* diagnostic{};
                const auto diagnosticResult = createPixelShader(
                    device,
                    contract.diagnosticBytecode[modeIndex],
                    contract.diagnosticBytecodeLength[modeIndex],
                    nullptr,
                    &diagnostic);
                if (FAILED(diagnosticResult) || !diagnostic) {
                    diagnosticCreationFailed = true;
                    failures_.fetch_add(1, std::memory_order_relaxed);
                    logging::error(
                        "Directional diagnostic DFLight contract {}/{} creation failed (HRESULT=0x{:08X}).",
                        index,
                        modeIndex + 1,
                        static_cast<std::uint32_t>(diagnosticResult));
                    continue;
                }
                directionalDiagnostics_[index][modeIndex].Attach(diagnostic);
            }
        }
        if (replacementCreationFailed) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            for (auto& contractDiagnostics : directionalDiagnostics_) {
                for (auto& diagnostic : contractDiagnostics) {
                    diagnostic.Reset();
                }
            }
            logging::error(
                "Contact Shadows DFLight family failed closed because one or more structurally verified replacements were rejected.");
            return;
        }
        if (diagnosticCreationFailed) {
            for (auto& contractDiagnostics : directionalDiagnostics_) {
                for (auto& diagnostic : contractDiagnostics) {
                    diagnostic.Reset();
                }
            }
            logging::error(
                "Directional diagnostics failed closed because one or more verified DFLight family variants were rejected; Contact Shadows remain available.");
        } else {
            logging::info(
                "Directional diagnostics armed six color-coded modes across all {} verified DFLight contracts.",
                csah_contact_shadow_dflight_contracts.size());
        }

        const auto dispatchResult = device->CreateComputeShader(
            csah_contact_shadow_dispatch,
            sizeof(csah_contact_shadow_dispatch),
            nullptr,
            dispatchCompute_.ReleaseAndGetAddressOf());
        if (FAILED(dispatchResult) || !dispatchCompute_) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows wavefront-dispatch compute creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(dispatchResult));
            return;
        }

        const auto computeResult = device->CreateComputeShader(
            csah_contact_shadow_mask,
            sizeof(csah_contact_shadow_mask),
            nullptr,
            maskCompute_.ReleaseAndGetAddressOf());
        if (FAILED(computeResult) || !maskCompute_) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            dispatchCompute_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows mask compute creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(computeResult));
            return;
        }
        const auto resolveResult = device->CreateComputeShader(
            csah_contact_shadow_resolve,
            sizeof(csah_contact_shadow_resolve),
            nullptr,
            resolveCompute_.ReleaseAndGetAddressOf());
        if (FAILED(resolveResult) || !resolveCompute_) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            dispatchCompute_.Reset();
            maskCompute_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows directional-resolve compute creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(resolveResult));
            return;
        }

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuSettings);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto bufferResult = device->CreateBuffer(
            &description,
            nullptr,
            constants_.ReleaseAndGetAddressOf());
        if (FAILED(bufferResult) || !constants_) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            dispatchCompute_.Reset();
            maskCompute_.Reset();
            resolveCompute_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(bufferResult));
            return;
        }

        D3D11_BUFFER_DESC recordDescription{};
        recordDescription.ByteWidth =
            kDispatchRecordCount * kDispatchRecordStride;
        recordDescription.Usage = D3D11_USAGE_DEFAULT;
        recordDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        recordDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        recordDescription.StructureByteStride = kDispatchRecordStride;
        const auto recordBufferResult = device->CreateBuffer(
            &recordDescription,
            nullptr,
            dispatchRecords_.ReleaseAndGetAddressOf());
        D3D11_SHADER_RESOURCE_VIEW_DESC recordViewDescription{};
        recordViewDescription.Format = DXGI_FORMAT_UNKNOWN;
        recordViewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        recordViewDescription.Buffer.FirstElement = 0;
        recordViewDescription.Buffer.NumElements = kDispatchRecordCount;
        const auto recordViewResult =
            SUCCEEDED(recordBufferResult) && dispatchRecords_ ?
            device->CreateShaderResourceView(
                dispatchRecords_.Get(),
                &recordViewDescription,
                dispatchRecordsView_.ReleaseAndGetAddressOf()) : E_FAIL;
        D3D11_UNORDERED_ACCESS_VIEW_DESC recordOutputDescription{};
        recordOutputDescription.Format = DXGI_FORMAT_UNKNOWN;
        recordOutputDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        recordOutputDescription.Buffer.FirstElement = 0;
        recordOutputDescription.Buffer.NumElements = kDispatchRecordCount;
        const auto recordOutputResult =
            SUCCEEDED(recordViewResult) ?
            device->CreateUnorderedAccessView(
                dispatchRecords_.Get(),
                &recordOutputDescription,
                dispatchRecordsOutput_.ReleaseAndGetAddressOf()) : E_FAIL;

        D3D11_BUFFER_DESC argumentDescription{};
        argumentDescription.ByteWidth =
            kDispatchRecordCount * kDispatchArgumentStride;
        argumentDescription.Usage = D3D11_USAGE_DEFAULT;
        argumentDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        argumentDescription.MiscFlags =
            D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS |
            D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        const auto argumentBufferResult = device->CreateBuffer(
            &argumentDescription,
            nullptr,
            dispatchArguments_.ReleaseAndGetAddressOf());
        D3D11_UNORDERED_ACCESS_VIEW_DESC argumentOutputDescription{};
        argumentOutputDescription.Format = DXGI_FORMAT_R32_TYPELESS;
        argumentOutputDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        argumentOutputDescription.Buffer.FirstElement = 0;
        argumentOutputDescription.Buffer.NumElements =
            argumentDescription.ByteWidth / sizeof(UINT);
        argumentOutputDescription.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        const auto argumentOutputResult =
            SUCCEEDED(argumentBufferResult) && dispatchArguments_ ?
            device->CreateUnorderedAccessView(
                dispatchArguments_.Get(),
                &argumentOutputDescription,
                dispatchArgumentsOutput_.ReleaseAndGetAddressOf()) : E_FAIL;
        if (FAILED(recordBufferResult) || FAILED(recordViewResult) ||
            FAILED(recordOutputResult) || FAILED(argumentBufferResult) ||
            FAILED(argumentOutputResult) || !dispatchRecords_ ||
            !dispatchRecordsView_ || !dispatchRecordsOutput_ ||
            !dispatchArguments_ || !dispatchArgumentsOutput_) {
            for (auto& replacement : replacements_) {
                replacement.Reset();
            }
            dispatchCompute_.Reset();
            maskCompute_.Reset();
            resolveCompute_.Reset();
            constants_.Reset();
            dispatchRecords_.Reset();
            dispatchRecordsView_.Reset();
            dispatchRecordsOutput_.Reset();
            dispatchArguments_.Reset();
            dispatchArgumentsOutput_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Contact Shadows GPU dispatch infrastructure creation failed (records=0x{:08X}/0x{:08X}/0x{:08X}, arguments=0x{:08X}/0x{:08X}).",
                static_cast<std::uint32_t>(recordBufferResult),
                static_cast<std::uint32_t>(recordViewResult),
                static_cast<std::uint32_t>(recordOutputResult),
                static_cast<std::uint32_t>(argumentBufferResult),
                static_cast<std::uint32_t>(argumentOutputResult));
            return;
        }
        if (!gpuTiming_.initialize(
                device,
                context,
                "Contact Shadows",
                { "wavefront setup", "raymarch", "resolve", nullptr },
                3,
                120,
                10,
                render::GpuTimingProfiler::Group::ContactShadows)) {
            logging::warn(
                "Contact Shadows could not allocate image-neutral GPU timing queries; rendering remains active without performance telemetry.");
        }
        if (!drawGpuTiming_.initialize(
                device,
                context,
                "Contact Shadows draw",
                { "DFLight replacement draw", nullptr, nullptr, nullptr },
                1,
                120,
                31,
                render::GpuTimingProfiler::Group::ContactShadows)) {
            logging::warn(
                "Contact Shadows could not allocate replacement-draw GPU timing queries; rendering remains active without performance telemetry.");
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Contact Shadows Bend wavefront raymarch and GPU indirect-dispatch pipeline ready; {} structurally verified directional DFLight contracts are armed fail-closed.",
            csah_contact_shadow_dflight_contracts.size());
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        const auto contractIndex = matchingContractIndex(
            bytecode,
            bytecodeLength);
        if (!shader || contractIndex == kInvalidContractIndex ||
            contractIndex >= replacements_.size() ||
            !replacements_[contractIndex]) {
            return;
        }
        matchingShaders_.fetch_add(1, std::memory_order_relaxed);
        if (!firstMatchLogged_.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Contact Shadows observed the first live member of its verified FO4VR directional DFLight family (contract {}).",
                contractIndex);
        }
        for (const auto& original : originals_) {
            if (original.shader.Get() == shader) {
                return;
            }
        }
        for (auto& original : originals_) {
            if (!original.shader) {
                original.shader = shader;
                original.contractIndex = static_cast<std::uint16_t>(
                    contractIndex);
                trackedShaders_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        failures_.fetch_add(1, std::memory_order_relaxed);
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11PixelShader* requested,
        bool compositorFeatureActive) noexcept
    {
        if (!requested || !compositorReady(compositorFeatureActive) ||
            !maskCompute_ || !constants_) {
            return { requested, {} };
        }
        for (const auto& original : originals_) {
            if (original.shader.Get() == requested &&
                original.contractIndex < replacements_.size()) {
                auto* replacement =
                    replacements_[original.contractIndex].Get();
                if (!replacement) {
                    return { requested, {} };
                }
                replacementBinds_.fetch_add(1, std::memory_order_relaxed);
                if (!firstReplacementBindLogged_.exchange(
                        true,
                        std::memory_order_relaxed)) {
                    logging::info(
                        "Contact Shadows verified DFLight family selected contract {} for its first live bind.",
                        original.contractIndex);
                }
                return { replacement, { requested, replacement } };
            }
        }
        return { requested, {} };
    }

    PixelShaderSelection Runtime::selectDirectionalDiagnosticPixelShader(
        ID3D11PixelShader* requested,
        const std::uint8_t diagnosticMode) noexcept
    {
        if (!requested || diagnosticMode == 0 || diagnosticMode > 11) {
            return { requested, {} };
        }
        // Coverage and presentation modes use the mode-4 raw-vector shader.
        // Their only difference is the draw/presentation path applied by the
        // diagnostic scopes.
        const auto shaderMode = diagnosticMode <= 6 ? diagnosticMode : 4;
        const auto modeIndex = static_cast<std::size_t>(shaderMode - 1);
        for (const auto& original : originals_) {
            if (original.shader.Get() != requested ||
                original.contractIndex >= directionalDiagnostics_.size()) {
                continue;
            }
            auto* diagnostic =
                directionalDiagnostics_[original.contractIndex][modeIndex].Get();
            if (!diagnostic) {
                return { requested, {} };
            }
            diagnosticBinds_.fetch_add(1, std::memory_order_relaxed);
            if (!firstDiagnosticBindLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Exclusive directional diagnostic selected live DFLight contract {} in color-coded mode {}.",
                    original.contractIndex,
                    diagnosticMode);
            }
            return { diagnostic, { requested, diagnostic } };
        }
        return { requested, {} };
    }

    std::uint32_t Runtime::sunShadowLayout(ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) return 0;
        for (const auto& original : originals_) {
            if (original.shader.Get() == shader &&
                original.contractIndex < csah_contact_shadow_dflight_contracts.size()) {
                return csah_contact_shadow_dflight_contracts[original.contractIndex].sunShadowLayout;
            }
        }
        return 0;
    }

    bool Runtime::tracksOriginal(ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return false;
        }
        for (const auto& original : originals_) {
            if (original.shader.Get() == shader) {
                return true;
            }
        }
        return false;
    }

    bool Runtime::isDirectionalDiagnosticPixelShader(
        ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return false;
        }
        for (const auto& contractDiagnostics : directionalDiagnostics_) {
            for (const auto& diagnostic : contractDiagnostics) {
                if (diagnostic.Get() == shader) {
                    return true;
                }
            }
        }
        return false;
    }

    ID3D11PixelShader*
        Runtime::retainedOriginalDirectionalDiagnosticPixelShader(
            ID3D11PixelShader* diagnosticShader) const noexcept
    {
        if (!diagnosticShader) {
            return nullptr;
        }
        for (const auto& original : originals_) {
            if (!original.shader ||
                original.contractIndex >= directionalDiagnostics_.size()) {
                continue;
            }
            for (const auto& diagnostic :
                 directionalDiagnostics_[original.contractIndex]) {
                if (diagnostic.Get() == diagnosticShader) {
                    return original.shader.Get();
                }
            }
        }
        return nullptr;
    }

    void Runtime::uploadSettings(
        ID3D11DeviceContext* context,
        bool contactShadowsActive,
        bool maskActive,
        bool cloudShadowsActive,
        float cloudOpacity) noexcept
    {
        if (!context || !constants_) return;
        D3D11_VIEWPORT viewport{};UINT count=1;context->RSGetViewports(&count,&viewport);
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (revision == uploadedRevision_ &&
                contactShadowsActive == uploadedContactActive_ &&
                maskActive == uploadedMaskActive_ &&
                cloudShadowsActive == uploadedCloudActive_ &&
                cloudOpacity == uploadedCloudOpacity_ &&
                uploadedGpuSettings_[16]==viewport.TopLeftX && uploadedGpuSettings_[17]==viewport.TopLeftY &&
                uploadedGpuSettings_[18]==viewport.Width && uploadedGpuSettings_[19]==viewport.Height) {
            return;
        }
        const GpuSettings data{
            .strength = strength_.load(std::memory_order_relaxed),
            .maxDistance = maxDistance_.load(std::memory_order_relaxed),
            .thickness = thickness_.load(std::memory_order_relaxed),
            .sampleCount = static_cast<float>(
                sampleCount_.load(std::memory_order_relaxed)),
            .foveated = foveated_.load(std::memory_order_relaxed) ? 1.0f : 0.0f,
            .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
            .reserved0 = maskActive ? 1.0f : 0.0f,
            .reserved1 = contactShadowsActive ? 1.0f : 0.0f,
            .cloudEnabled = cloudShadowsActive ? 1.0f : 0.0f,
            .cloudOpacity = cloudOpacity,
            .viewport = {viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height},
        };
        static_assert(sizeof(data) == sizeof(uploadedGpuSettings_));
        std::memcpy(
            uploadedGpuSettings_.data(),
            &data,
            sizeof(data));
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
        uploadedContactActive_ = contactShadowsActive;
        uploadedMaskActive_ = maskActive;
        uploadedCloudActive_ = cloudShadowsActive;
        uploadedCloudOpacity_ = cloudOpacity;
    }

    bool Runtime::ensureMaskResources(
        ID3D11ShaderResourceView* depth) noexcept
    {
        if (!device_ || !depth) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        depth->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture)) || !texture) {
            return false;
        }
        D3D11_TEXTURE2D_DESC source{};
        texture->GetDesc(&source);
        if (source.Width < 2 || source.Height == 0 || (source.Width & 1u) != 0 ||
            source.ArraySize != 1 || source.SampleDesc.Count != 1) {
            return false;
        }
        if (rawMaskTexture_ && rawMaskView_ && rawMaskOutput_ &&
            maskTexture_ && maskView_ && maskOutput_ &&
            maskWidth_ == source.Width && maskHeight_ == source.Height) {
            return true;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = source.Width;
        description.Height = source.Height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        const auto createTarget = [&](
                                      const char* label,
                                      DXGI_FORMAT format,
                                      Microsoft::WRL::ComPtr<ID3D11Texture2D>&
                                          targetTexture,
                                      Microsoft::WRL::ComPtr<
                                          ID3D11ShaderResourceView>& targetView,
                                      Microsoft::WRL::ComPtr<
                                          ID3D11UnorderedAccessView>&
                                          targetOutput) noexcept {
            description.Format = format;
            const auto textureResult = device_->CreateTexture2D(
                &description,
                nullptr,
                targetTexture.GetAddressOf());
            const auto viewResult = SUCCEEDED(textureResult) ?
                device_->CreateShaderResourceView(
                    targetTexture.Get(),
                    nullptr,
                    targetView.GetAddressOf()) : E_FAIL;
            const auto outputResult = SUCCEEDED(viewResult) ?
                device_->CreateUnorderedAccessView(
                    targetTexture.Get(),
                    nullptr,
                    targetOutput.GetAddressOf()) : E_FAIL;
            if (FAILED(textureResult) || FAILED(viewResult) ||
                FAILED(outputResult) || !targetTexture || !targetView ||
                !targetOutput) {
                logging::error(
                    "Contact Shadows {} allocation failed for {}x{} (texture=0x{:08X}, SRV=0x{:08X}, UAV=0x{:08X}).",
                    label,
                    source.Width,
                    source.Height,
                    static_cast<std::uint32_t>(textureResult),
                    static_cast<std::uint32_t>(viewResult),
                    static_cast<std::uint32_t>(outputResult));
                return false;
            }
            return true;
        };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> nextRawTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextRawView;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextRawOutput;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> nextTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> nextView;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> nextOutput;
        if (!createTarget(
                "raw front/back mask",
                DXGI_FORMAT_R8G8_UNORM,
                nextRawTexture,
                nextRawView,
                nextRawOutput) ||
            !createTarget(
                "resolved direct/transmission mask",
                DXGI_FORMAT_R8G8_UNORM,
                nextTexture,
                nextView,
                nextOutput)) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        rawMaskTexture_ = std::move(nextRawTexture);
        rawMaskView_ = std::move(nextRawView);
        rawMaskOutput_ = std::move(nextRawOutput);
        maskTexture_ = std::move(nextTexture);
        maskView_ = std::move(nextView);
        maskOutput_ = std::move(nextOutput);
        maskWidth_ = source.Width;
        maskHeight_ = source.Height;
        maskRebuilds_.fetch_add(1, std::memory_order_relaxed);
        logging::info(
            "Contact Shadows paired front/back and resolved direct/transmission RG8 stereo masks allocated at {}x{}.",
            maskWidth_,
            maskHeight_);
        return true;
    }

    bool Runtime::dispatchMask(
        ID3D11DeviceContext* context,
        bool contactShadowsActive,
        bool cloudShadowsActive,
        bool& maskActive) noexcept
    {
        maskActive = false;
        if (!context || !dispatchCompute_ || !maskCompute_ ||
            !resolveCompute_ || !constants_ || !dispatchRecords_ ||
            !dispatchRecordsView_ || !dispatchRecordsOutput_ ||
            !dispatchArguments_ || !dispatchArgumentsOutput_) {
            return false;
        }

        ID3D11ShaderResourceView* cloud{};
        ID3D11SamplerState* cloudSampler{};
        float cloudOpacity{};
        const auto cloudReady = cloudShadowsActive &&
            cloud_shadows::Runtime::get().prepareLighting(
                context, cloud, cloudSampler, cloudOpacity);
        maskActive = contactShadowsActive || cloudReady;
        uploadSettings(
            context,
            contactShadowsActive,
            maskActive,
            cloudReady,
            cloudOpacity);
        if (!maskActive) {
            return true;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth, normal;
        Microsoft::WRL::ComPtr<ID3D11Buffer> dflight;
        Microsoft::WRL::ComPtr<ID3D11Buffer> stereo;
        Microsoft::WRL::ComPtr<ID3D11Buffer> camera;
        context->PSGetShaderResources(kDepthSlot, 1, depth.GetAddressOf());
        context->PSGetShaderResources(1, 1, normal.GetAddressOf());
        context->PSGetConstantBuffers(
            kDFLightConstantSlot,
            1,
            dflight.GetAddressOf());
        context->PSGetConstantBuffers(
            kStereoConstantSlot,
            1,
            stereo.GetAddressOf());
        context->PSGetConstantBuffers(
            kCameraConstantSlot,
            1,
            camera.GetAddressOf());
        const auto maskResourcesReady = depth && normal && dflight && stereo && camera &&
            ensureMaskResources(depth.Get());
        if (!maskResourcesReady) {
            if (!firstDispatchFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Contact Shadows first exact DFLight dispatch rejected its live inputs: depth={}, b2={}, b8={}, b12={}, maskResources={}; compositor fell back to vanilla for that draw.",
                    static_cast<bool>(depth),
                    static_cast<bool>(dflight),
                    static_cast<bool>(stereo),
                    static_cast<bool>(camera),
                    rawMaskTexture_ && rawMaskView_ && rawMaskOutput_ &&
                        maskTexture_ && maskView_ && maskOutput_ &&
                        dispatchRecords_ && dispatchRecordsView_ &&
                        dispatchRecordsOutput_ && dispatchArguments_ &&
                        dispatchArgumentsOutput_);
            }
            return false;
        }
        render::ScopedComputeState restore(
            context,
            {
                .firstShaderResource = 0,
                .shaderResourceCount = 5,
                .firstUnorderedAccess = 0,
                .unorderedAccessCount = 2,
                .firstSampler = kCloudSamplerSlot,
                .samplerCount = 1,
                .firstConstantBuffer = kDFLightConstantSlot,
                .constantBufferCount =
                    kConstantSlot - kDFLightConstantSlot + 1,
            });
        if (!restore.captured()) {
            if (!firstDispatchFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Contact Shadows first exact DFLight dispatch could not capture the bounded compute-state footprint; compositor fell back to vanilla for that draw.");
            }
            return false;
        }
        auto timing = gpuTiming_.begin();

        auto* depthView = depth.Get();
        auto* rawOutput = rawMaskOutput_.Get();
        auto* resolvedOutput = maskOutput_.Get();
        auto* dflightConstants = dflight.Get();
        auto* stereoConstants = stereo.Get();
        auto* cameraConstants = camera.Get();
        auto* settingsConstants = constants_.Get();
        context->CSSetConstantBuffers(
            kDFLightConstantSlot,
            1,
            &dflightConstants);
        context->CSSetConstantBuffers(
            kStereoConstantSlot,
            1,
            &stereoConstants);
        context->CSSetConstantBuffers(
            kCameraConstantSlot,
            1,
            &cameraConstants);
        context->CSSetConstantBuffers(
            kConstantSlot,
            1,
            &settingsConstants);
        const auto dispatchWidth =
            (static_cast<UINT>(uploadedGpuSettings_[18]) + kThreadGroupWidth - 1) / kThreadGroupWidth;
        const auto dispatchHeight =
            (static_cast<UINT>(uploadedGpuSettings_[19]) + kThreadGroupHeight - 1) / kThreadGroupHeight;
        constexpr std::array<float, 4> fullyVisible{
            1.0f,
            1.0f,
            1.0f,
            1.0f,
        };
        context->ClearUnorderedAccessViewFloat(
            rawMaskOutput_.Get(),
            fullyVisible.data());
        if (contactShadowsActive) {
            const std::array<ID3D11UnorderedAccessView*, 2> setupOutputs{
                dispatchRecordsOutput_.Get(),
                dispatchArgumentsOutput_.Get(),
            };
            context->CSSetShader(dispatchCompute_.Get(), nullptr, 0);
            context->CSSetShaderResources(0, 1, &depthView);
            context->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(setupOutputs.size()),
                setupOutputs.data(),
                nullptr);
            context->Dispatch(1, 1, 1);
            timing.mark();

            const std::array<ID3D11UnorderedAccessView*, 2> noSetupOutputs{};
            context->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(noSetupOutputs.size()),
                noSetupOutputs.data(),
                nullptr);

            const std::array<ID3D11ShaderResourceView*, 2> raymarchInputs{
                depthView,
                dispatchRecordsView_.Get(),
            };
            context->CSSetShader(maskCompute_.Get(), nullptr, 0);
            context->CSSetShaderResources(
                0,
                static_cast<UINT>(raymarchInputs.size()),
                raymarchInputs.data());
            context->CSSetUnorderedAccessViews(
                0,
                1,
                &rawOutput,
                nullptr);
            auto indexedSettings = uploadedGpuSettings_;
            for (UINT dispatchIndex = 0;
                 dispatchIndex < kDispatchRecordCount;
                 ++dispatchIndex) {
                indexedSettings[11] = static_cast<float>(dispatchIndex);
                context->UpdateSubresource(
                    constants_.Get(),
                    0,
                    nullptr,
                    indexedSettings.data(),
                    0,
                    0);
                context->DispatchIndirect(
                    dispatchArguments_.Get(),
                    dispatchIndex * kDispatchArgumentStride);
            }
            indexedSettings[11] = 0.0f;
            context->UpdateSubresource(
                constants_.Get(),
                0,
                nullptr,
                indexedSettings.data(),
                0,
                0);
            timing.mark();
        } else {
            timing.mark();
            timing.mark();
        }

        const std::array<ID3D11UnorderedAccessView*, 2> noOutputs{};
        context->CSSetUnorderedAccessViews(
            0,
            static_cast<UINT>(noOutputs.size()),
            noOutputs.data(),
            nullptr);
        const std::array<ID3D11ShaderResourceView*, 5> resolveInputs{
            depthView,
            rawMaskView_.Get(),
            cloud,
            normal.Get(),
            cloudReady ? cloud_shadows::Runtime::get().fieldView() : nullptr,
        };
        context->CSSetShader(resolveCompute_.Get(), nullptr, 0);
        context->CSSetShaderResources(
            0,
            static_cast<UINT>(resolveInputs.size()),
            resolveInputs.data());
        context->CSSetUnorderedAccessViews(
            0,
            1,
            &resolvedOutput,
            nullptr);
        context->CSSetSamplers(
            kCloudSamplerSlot,
            1,
            &cloudSampler);
        context->Dispatch(dispatchWidth, dispatchHeight, 1);
        if (!restore.restore()) {
            if (!firstDispatchFailureLogged_.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Contact Shadows exact DFLight dispatch could not restore its bounded compute-state footprint; compositor fell back to vanilla for that draw.");
            }
            return false;
        }
        maskDispatches_.fetch_add(1, std::memory_order_relaxed);
        if (!firstDispatchLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::info(
                "Contact Shadows first exact DFLight stereo-mask dispatch completed and restored all touched compute state.");
        }
        return true;
    }

    ScopedDrawBindings Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        ShaderBinding binding,
        bool contactShadowsActive,
        bool cloudShadowsActive) noexcept
    {
        bool verifiedBinding{};
        for (const auto& original : originals_) {
            if (original.shader.Get() == binding.original &&
                original.contractIndex < replacements_.size() &&
                replacements_[original.contractIndex].Get() ==
                    binding.replacement) {
                verifiedBinding = true;
                break;
            }
        }
        if (!binding || !verifiedBinding || !constants_ ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return {};
        }
        auto maskActive = false;
        if (!dispatchMask(
                context,
                contactShadowsActive,
                cloudShadowsActive,
                maskActive)) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedDrawBindings(
            context,
            constants_.Get(),
            maskActive ? maskView_.Get() : nullptr,
            &drawGpuTiming_,
            &drawRestores_);
    }

    void Runtime::recordDrawFallback() noexcept
    {
        drawFallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::compositorReady(bool wrappedGrassActive) const noexcept
    {
        return resourcesReady_.load(std::memory_order_acquire) &&
            (featureEnabled() || wrappedGrassActive);
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        foveated_.store(safe.foveated, std::memory_order_relaxed);
        strength_.store(safe.strength, std::memory_order_relaxed);
        maxDistance_.store(safe.maxDistance, std::memory_order_relaxed);
        fadeDistance_.store(safe.fadeDistance, std::memory_order_relaxed);
        thickness_.store(safe.thickness, std::memory_order_relaxed);
        sampleCount_.store(safe.sampleCount, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .foveated = foveated_.load(std::memory_order_relaxed),
                .strength = strength_.load(std::memory_order_relaxed),
                .maxDistance = maxDistance_.load(std::memory_order_relaxed),
                .fadeDistance = fadeDistance_.load(std::memory_order_relaxed),
                .thickness = thickness_.load(std::memory_order_relaxed),
                .sampleCount = sampleCount_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .matchingShaders = matchingShaders_.load(std::memory_order_relaxed),
            .trackedShaders = trackedShaders_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .diagnosticBinds = diagnosticBinds_.load(std::memory_order_relaxed),
            .maskDispatches = maskDispatches_.load(std::memory_order_relaxed),
            .maskRebuilds = maskRebuilds_.load(std::memory_order_relaxed),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .drawFallbacks = drawFallbacks_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
