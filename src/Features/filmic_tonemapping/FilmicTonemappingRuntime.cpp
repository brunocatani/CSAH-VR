#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"

#include "Features/volumetric_lighting/VolumetricEngineData.h"
#include "FilmicTonemappingPS.h"
#include "support/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace csah::filmic_tonemapping
{
    namespace
    {
        enum class NativeTonemapContract : std::uint8_t
        {
            none,
            base,
            fade,
        };

        constexpr SIZE_T kNativeBaseBytecodeSize = 1772;
        constexpr SIZE_T kNativeFadeBytecodeSize = 1848;
        constexpr UINT kConstantSlot = 12;
        constexpr UINT kGradeTableSlot = 6;
        constexpr UINT kGradeTableSlots = 2;
        constexpr UINT kGradeSamplerSlot = 5;
        // One table per file, named as ENB presets name theirs, so a preset's
        // set drops in unchanged. Indexed by Runtime::GradeTable.
        constexpr std::array<const char*, 3> kGradeTableFiles{
            "Data/F4SE/Plugins/CSAH/ColorGrading/lut_day.png",
            "Data/F4SE/Plugins/CSAH/ColorGrading/lut_night.png",
            "Data/F4SE/Plugins/CSAH/ColorGrading/lut_interior.png",
        };
        // The day weight moves slowly; a smaller step than this is not worth
        // a constant upload.
        constexpr float kGradeWeightStep = 1.0f / 512.0f;
        constexpr std::array<std::uint8_t, 16> kNativeBaseDxbcChecksum{
            0x83, 0xB5, 0x14, 0xDD, 0x63, 0xEE, 0xA6, 0x0D,
            0x84, 0x76, 0x93, 0x43, 0xD5, 0x76, 0xD4, 0xFA,
        };
        constexpr std::array<std::uint8_t, 16> kNativeFadeDxbcChecksum{
            0xF1, 0xBF, 0xA0, 0x42, 0xD5, 0x20, 0x62, 0xC4,
            0xAE, 0xDF, 0xD6, 0x12, 0xBB, 0xF4, 0x79, 0x9D,
        };

        [[nodiscard]] NativeTonemapContract classifyNativeTonemap(
            const void* bytecode,
            SIZE_T bytecodeLength) noexcept
        {
            if (!bytecode || bytecodeLength < 20) {
                return NativeTonemapContract::none;
            }
            const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
            if (std::memcmp(bytes, "DXBC", 4) != 0) {
                return NativeTonemapContract::none;
            }
            if (bytecodeLength == kNativeBaseBytecodeSize &&
                std::memcmp(
                    bytes + 4,
                    kNativeBaseDxbcChecksum.data(),
                    kNativeBaseDxbcChecksum.size()) == 0) {
                return NativeTonemapContract::base;
            }
            if (bytecodeLength == kNativeFadeBytecodeSize &&
                std::memcmp(
                    bytes + 4,
                    kNativeFadeDxbcChecksum.data(),
                    kNativeFadeDxbcChecksum.size()) == 0) {
                return NativeTonemapContract::fade;
            }
            return NativeTonemapContract::none;
        }
    }

    ScopedConstants::ScopedConstants(
        Runtime* owner,
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        const GradeBindings& grade) noexcept :
        owner_(owner), context_(context)
    {
        if (!owner_ || !context_ || !constants) {
            owner_ = nullptr;
            context_ = nullptr;
            return;
        }
        ID3D11Buffer* previous{};
        context_->PSGetConstantBuffers(kConstantSlot, 1, &previous);
        previous_.Attach(previous);
        std::array<ID3D11ShaderResourceView*, kGradeTableSlots>
            previousTables{};
        context_->PSGetShaderResources(
            kGradeTableSlot, kGradeTableSlots, previousTables.data());
        for (std::size_t index = 0; index < previousTables.size(); ++index) {
            previousTables_[index].Attach(previousTables[index]);
        }
        ID3D11SamplerState* previousSampler{};
        context_->PSGetSamplers(kGradeSamplerSlot, 1, &previousSampler);
        previousSampler_.Attach(previousSampler);

        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        context_->PSSetShaderResources(
            kGradeTableSlot, kGradeTableSlots, grade.tables.data());
        context_->PSSetSamplers(kGradeSamplerSlot, 1, &grade.sampler);
    }

    ScopedConstants::~ScopedConstants() noexcept
    {
        if (!owner_ || !context_) {
            return;
        }
        auto* previous = previous_.Get();
        context_->PSSetConstantBuffers(kConstantSlot, 1, &previous);
        std::array<ID3D11ShaderResourceView*, kGradeTableSlots> tables{};
        for (std::size_t index = 0; index < tables.size(); ++index) {
            tables[index] = previousTables_[index].Get();
        }
        context_->PSSetShaderResources(
            kGradeTableSlot, kGradeTableSlots, tables.data());
        auto* sampler = previousSampler_.Get();
        context_->PSSetSamplers(kGradeSamplerSlot, 1, &sampler);
        owner_->drawRestores_.fetch_add(1, std::memory_order_relaxed);
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        CreatePixelShaderFunction createPixelShader) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        trackedShaders_.store(0, std::memory_order_relaxed);
        for (auto& original : originals_) {
            original.Reset();
        }
        originalFade_.fill(false);
        baseReplacement_.Reset();
        fadeReplacement_.Reset();
        constants_.Reset();
        gradeSampler_.Reset();
        for (auto& table : gradeTables_) {
            table = {};
        }
        gradingTablesLoaded_.store(false, std::memory_order_release);
        uploadedSelection_ = {};
        device_ = device;
        context_ = context;
        uploadedRevision_ = 0;
        baseMatchLogged_.store(false, std::memory_order_relaxed);
        fadeMatchLogged_.store(false, std::memory_order_relaxed);
        firstBindLogged_.store(false, std::memory_order_relaxed);
        if (!device || !context || !createPixelShader) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Colour tables. A file that is absent or rejected becomes the
        // identity table, so the shader always has two valid tables to blend.
        bool tablesLoaded = false;
        bool tablesReady = true;
        for (std::size_t index = 0; index < gradeTables_.size(); ++index) {
            gradeTables_[index] = color_grading::loadTable(
                device, std::filesystem::path(kGradeTableFiles[index]));
            if (gradeTables_[index]) {
                tablesLoaded = true;
            } else {
                gradeTables_[index] =
                    color_grading::createIdentityTable(device);
            }
            tablesReady = tablesReady &&
                static_cast<bool>(gradeTables_[index]);
        }
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(
                &samplerDescription,
                gradeSampler_.ReleaseAndGetAddressOf())) ||
            !tablesReady) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Filmic Tonemapping could not create its colour grading tables; native HDR tonemapping remains bound.");
            return;
        }
        gradingTablesLoaded_.store(tablesLoaded, std::memory_order_release);
        logging::info(
            "Colour Grading colour tables from Data\\F4SE\\Plugins\\CSAH\\ColorGrading: day={}, night={}, interior={}. A missing table leaves its colours unchanged.",
            gradeTables_[kDayTable].fromFile,
            gradeTables_[kNightTable].fromFile,
            gradeTables_[kInteriorTable].fromFile);

        // The first draw uploads the real constants: uploadedRevision_ is 0.
        const auto initial = gpuConstants({ .enabled = false }, {});
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(GpuConstants);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = &initial;
        auto result = device->CreateBuffer(
            &description, &data, constants_.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result)) {
            result = createPixelShader(
                device,
                generated::kBasePixelShader.data(),
                generated::kBasePixelShader.size(),
                nullptr,
                baseReplacement_.ReleaseAndGetAddressOf());
        }
        if (SUCCEEDED(result)) {
            result = createPixelShader(
                device,
                generated::kFadePixelShader.data(),
                generated::kFadePixelShader.size(),
                nullptr,
                fadeReplacement_.ReleaseAndGetAddressOf());
        }
        if (FAILED(result) || !constants_ || !baseReplacement_ ||
            !fadeReplacement_) {
            constants_.Reset();
            baseReplacement_.Reset();
            fadeReplacement_.Reset();
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Filmic Tonemapping GPU resource creation failed (HRESULT=0x{:08X}); native HDR tonemapping remains bound.",
                static_cast<std::uint32_t>(result));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "Filmic Tonemapping replacements are GPU-ready for the exact FO4VR ImageSpace[026] base and ImageSpace[027] fade HDR output family; CB12 remains scoped and optional Bloom/Glare t4/t5/b13 inputs fail closed.");
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        const auto contract = classifyNativeTonemap(bytecode, bytecodeLength);
        if (!shader || contract == NativeTonemapContract::none) {
            return;
        }
        matchingShaders_.fetch_add(1, std::memory_order_relaxed);
        const auto count = trackedShaders_.load(std::memory_order_acquire);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (originals_[index].Get() == shader) {
                return;
            }
        }
        if (count >= originals_.size()) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Filmic Tonemapping exceeded its fixed original-shader capacity; the untracked shader remains native.");
            return;
        }
        originals_[count] = shader;
        originalFade_[count] = contract == NativeTonemapContract::fade;
        trackedShaders_.store(count + 1, std::memory_order_release);
        auto& matchLogged = contract == NativeTonemapContract::fade ?
            fadeMatchLogged_ : baseMatchLogged_;
        if (!matchLogged.exchange(true, std::memory_order_relaxed)) {
            logging::info(
                "Filmic Tonemapping matched FO4VR {} HDR contract ({} bytes, checksum={}).",
                contract == NativeTonemapContract::fade ?
                    "ImageSpace[027] fade" : "ImageSpace[026] base",
                bytecodeLength,
                contract == NativeTonemapContract::fade ?
                    "f1bfa042d52062c4aedfd612bbf4799d" :
                    "83b514dd63eea60d84769343d576d4fa");
        }
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        if (!requested || !featureEnabled() || context != context_.Get()) {
            return { requested, {} };
        }
        const auto count = trackedShaders_.load(std::memory_order_acquire);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (originals_[index].Get() != requested) {
                continue;
            }
            auto* replacement = originalFade_[index] ?
                fadeReplacement_.Get() : baseReplacement_.Get();
            if (!replacement) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                return { requested, {} };
            }
            replacementBinds_.fetch_add(1, std::memory_order_relaxed);
            if (!firstBindLogged_.exchange(true, std::memory_order_relaxed)) {
                logging::info(
                    "Filmic Tonemapping replaced its first executing native HDR output bind; base/fade semantics, exposure, native bloom, cinematic state, and bypass-mask inputs remain engine-owned while optional Bloom/Glare inputs are privately scoped.");
            }
            return {
                replacement,
                { requested, replacement },
            };
        }
        return { requested, {} };
    }

    ScopedConstants Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        ShaderBinding binding) noexcept
    {
        if (!bindingActive(binding) || !context || context != context_.Get() ||
            !constants_) {
            return {};
        }
        uploadConstants(context);
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedConstants(
            this,
            context,
            constants_.Get(),
            {
                .tables = {
                    gradeTables_[uploadedSelection_.first].view.Get(),
                    gradeTables_[uploadedSelection_.second].view.Get(),
                },
                .sampler = gradeSampler_.Get(),
            });
    }

    bool Runtime::tracksOriginal(ID3D11PixelShader* shader) const noexcept
    {
        if (!shader) {
            return false;
        }
        const auto count = trackedShaders_.load(std::memory_order_acquire);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (originals_[index].Get() == shader) {
                return true;
            }
        }
        return false;
    }

    bool Runtime::featureEnabled() const noexcept
    {
        return (enabled_.load(std::memory_order_acquire) ||
                   outputFeatureRequested_.load(std::memory_order_acquire) ||
                   gradingActive()) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    bool Runtime::gradingActive() const noexcept
    {
        return gradingEnabled_.load(std::memory_order_acquire) &&
            (gradingTablesLoaded_.load(std::memory_order_acquire) ||
                !gradingControlsNeutral_.load(std::memory_order_acquire));
    }

    Runtime::GradeSelection Runtime::selectGradeTables() const noexcept
    {
        GradeSelection selection;
        if (!gradingTablesLoaded_.load(std::memory_order_acquire)) {
            return selection;
        }
        const auto frame = volumetric_lighting::captureDayNightFrame();
        if (!frame.valid) {
            return selection;
        }
        selection.tablesUsable = true;
        if (frame.interior) {
            selection.first = kInteriorTable;
            selection.second = kInteriorTable;
            return selection;
        }
        selection.secondWeight = std::clamp(frame.day, 0.0f, 1.0f);
        return selection;
    }

    void Runtime::setOutputFeatureRequested(bool requested) noexcept
    {
        outputFeatureRequested_.store(requested, std::memory_order_release);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    bool Runtime::bindingActive(ShaderBinding binding) const noexcept
    {
        return binding && featureEnabled() &&
            (binding.replacement == baseReplacement_.Get() ||
                binding.replacement == fadeReplacement_.Get());
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        enabled_.store(safe.enabled, std::memory_order_release);
        nativeAutoExposure_.store(
            safe.useNativeAutoExposure, std::memory_order_relaxed);
        exposureCompensationEV_.store(
            safe.exposureCompensationEV, std::memory_order_relaxed);
        filmicStrength_.store(safe.filmicStrength, std::memory_order_relaxed);
        whitePointScale_.store(safe.whitePointScale, std::memory_order_relaxed);
        {
            const std::scoped_lock lock(gradingMutex_);
            grading_ = safe.grading;
        }
        gradingControlsNeutral_.store(
            color_grading::controlsNeutral(safe.grading),
            std::memory_order_release);
        gradingEnabled_.store(
            safe.grading.enabled, std::memory_order_release);
        settingsRevision_.fetch_add(1, std::memory_order_release);
    }

    Runtime::GpuConstants Runtime::gpuConstants(
        const color_grading::Settings& grading,
        const GradeSelection& selection) const noexcept
    {
        GpuConstants constants{
            .exposureMultiplier = 1.0f,
            .nativeAdaptationWeight = 1.0f,
            .filmicStrength = 0.0f,
            .whitePointScale = 1.0f,
        };
        if (enabled_.load(std::memory_order_acquire)) {
            constants.exposureMultiplier = std::exp2(
                exposureCompensationEV_.load(std::memory_order_relaxed));
            constants.nativeAdaptationWeight = nativeAutoExposure_.load(
                std::memory_order_relaxed) ? 1.0f : 0.0f;
            constants.filmicStrength =
                filmicStrength_.load(std::memory_order_relaxed);
            constants.whitePointScale =
                whitePointScale_.load(std::memory_order_relaxed);
        }

        const auto tableStrength = selection.tablesUsable ?
            grading.tableStrength : 0.0f;
        if (!grading.enabled ||
            (tableStrength <= 0.0f && color_grading::controlsNeutral(grading))) {
            return constants;
        }
        // A table of N steps is sampled at its texel centres: 0.5 / N at
        // colour 0 and 1 - 0.5 / N at colour 1.
        const auto tableScale = [this](std::size_t index) {
            const auto size = static_cast<float>(gradeTables_[index].size);
            return size >= 2.0f ? (size - 1.0f) / size : 0.0f;
        };
        const auto tableOffset = [this](std::size_t index) {
            const auto size = static_cast<float>(gradeTables_[index].size);
            return size >= 2.0f ? 0.5f / size : 0.0f;
        };
        constants.gradeControl = {
            1.0f, tableStrength, selection.secondWeight, grading.gamma
        };
        constants.gradeTables = {
            tableScale(selection.first), tableOffset(selection.first),
            tableScale(selection.second), tableOffset(selection.second),
        };
        constants.gradeLevels = {
            grading.blackPoint,
            1.0f / std::max(grading.whitePoint - grading.blackPoint, 1.0e-4f),
            grading.brightness,
            grading.contrast,
        };
        constants.gradeColor = {
            grading.saturation, grading.desaturateShadows, 0.0f, 0.0f
        };
        constants.gradeShadowTint = {
            grading.shadowTint[0], grading.shadowTint[1],
            grading.shadowTint[2], 0.0f
        };
        constants.gradeHighlightTint = {
            grading.highlightTint[0], grading.highlightTint[1],
            grading.highlightTint[2], 0.0f
        };
        return constants;
    }

    void Runtime::uploadConstants(ID3D11DeviceContext* context) noexcept
    {
        if (!context || !constants_) {
            return;
        }
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        auto selection = gradingActive() ? selectGradeTables() : GradeSelection{};
        if (uploadedRevision_ == revision &&
            selection.first == uploadedSelection_.first &&
            selection.second == uploadedSelection_.second &&
            selection.tablesUsable == uploadedSelection_.tablesUsable &&
            std::abs(selection.secondWeight -
                uploadedSelection_.secondWeight) < kGradeWeightStep) {
            return;
        }
        color_grading::Settings grading;
        {
            const std::scoped_lock lock(gradingMutex_);
            grading = grading_;
        }
        const auto data = gpuConstants(grading, selection);
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
        uploadedSelection_ = selection;
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        color_grading::Settings grading;
        {
            const std::scoped_lock lock(gradingMutex_);
            grading = grading_;
        }
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .useNativeAutoExposure = nativeAutoExposure_.load(
                    std::memory_order_relaxed),
                .exposureCompensationEV = exposureCompensationEV_.load(
                    std::memory_order_relaxed),
                .filmicStrength =
                    filmicStrength_.load(std::memory_order_relaxed),
                .whitePointScale =
                    whitePointScale_.load(std::memory_order_relaxed),
                .grading = grading,
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .matchingShaders =
                matchingShaders_.load(std::memory_order_relaxed),
            .trackedShaders =
                trackedShaders_.load(std::memory_order_acquire),
            .replacementBinds =
                replacementBinds_.load(std::memory_order_relaxed),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
