#include "Features/pbr/PbrRuntime.h"

#include "Features/ibl/IblRuntime.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "render/BSDFPrePassShaderHook.h"
#include "render/NativeTextureLoad.h"
#include "support/Logger.h"

#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif
#include <RE/NetImmerse/NiSmartPointer.h>
#include <RE/NetImmerse/NiTexture.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace csah::pbr
{
    namespace
    {
        constexpr UINT kConstantSlot = 7;
        constexpr UINT kPbrMaterialSlot = 45;
        constexpr UINT kSurfaceClassSlot = 47;
        constexpr UINT kAuthoredRmaosSlot = 48;
        constexpr std::size_t kMaximumAuthoredMaterials = 65536;
        constexpr std::size_t kMinimumMaterialLookupCapacity = 64;
        constexpr std::size_t kMaterialLoadQueueCapacity = 4096;
        constexpr std::size_t kMaterialLoadsPerMainThreadTask = 4;

        [[nodiscard]] bool normalizeTexturePath(
            std::string_view path,
            std::array<char, 513>& storage,
            std::string_view& normalized) noexcept
        {
            if (path.empty() || path.size() > storage.size() - 1 ||
                path.find(':') != std::string_view::npos) {
                return false;
            }
            while (path.starts_with(".\\") || path.starts_with("./")) {
                path.remove_prefix(2);
            }
            auto size = std::size_t{};
            for (const auto value : path) {
                const auto character = static_cast<unsigned char>(value);
                storage[size++] = value == '/' ? '\\' :
                    static_cast<char>(std::tolower(character));
            }
            storage[size] = '\0';
            normalized = { storage.data(), size };
            return normalized.starts_with("textures\\") &&
                normalized.ends_with(".dds") &&
                normalized.find("..") == std::string_view::npos &&
                !normalized.starts_with('\\');
        }

        [[nodiscard]] std::optional<std::string> normalizeTexturePath(
            std::string path) noexcept
        {
            try {
                std::array<char, 513> storage{};
                std::string_view normalized;
                if (!normalizeTexturePath(path, storage, normalized)) {
                    return std::nullopt;
                }
                return std::string(normalized);
            } catch (...) {
                return std::nullopt;
            }
        }

        [[nodiscard]] std::filesystem::path manifestRoot() noexcept
        {
            std::array<wchar_t, 32768> executable{};
            const auto length = GetModuleFileNameW(
                nullptr,
                executable.data(),
                static_cast<DWORD>(executable.size()));
            if (length == 0 || length >= executable.size()) {
                return {};
            }
            auto root = std::filesystem::path(
                std::wstring_view(executable.data(), length));
            return root.parent_path() / "Data" / "F4SE" / "Plugins" /
                "CSAH" / "PBRMaterials";
        }

        [[nodiscard]] RE::NiPointer<RE::NiTexture> loadTexture(
            const std::string& path,
            bool sRgb) noexcept
        {
            return render::loadStreamBackedTexture(path.c_str(), sRgb);
        }

        [[nodiscard]] ID3D11ShaderResourceView* shaderResource(
            const RE::NiPointer<RE::NiTexture>& texture) noexcept
        {
            if (!texture || !texture->rendererTexture) {
                return nullptr;
            }
            // Fallout4VR.exe 1.2.72 independently reads NiTexture+0x38 and
            // then the first pointer in BSGraphics::Texture when binding an
            // SRV (for example 0x14287D80A..0x14287D853 and
            // 0x14287F5D3..0x14287F617). Keep the CommonLib renderer type
            // opaque here because its REX D3D declarations cannot coexist
            // with the native d3d11.h ABI in this translation unit.
            ID3D11ShaderResourceView* view{};
            std::memcpy(&view, texture->rendererTexture, sizeof(view));
            return view;
        }

        [[nodiscard]] bool belongsToDevice(
            ID3D11ShaderResourceView* view,
            ID3D11Device* expected) noexcept
        {
            if (!view || !expected) {
                return false;
            }
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            view->GetDevice(device.GetAddressOf());
            return device.Get() == expected;
        }
    }

    struct Runtime::MaterialRegistry final
    {
        enum class LoadState : std::uint8_t
        {
            unloaded,
            queued,
            loaded,
            failed,
        };

        struct Record final
        {
            std::string basePath;
            std::string rmaosPath;
        };

        struct RuntimeRecord final
        {
            std::atomic<LoadState> loadState{ LoadState::unloaded };
            RE::NiPointer<RE::NiTexture> rmaosTexture;
            ID3D11ShaderResourceView* rmaosView{};
            bool resolutionCounted{};
        };

        struct LookupSlot final
        {
            RE::NiTexture* baseTexture{};
            const char* nameData{};
            std::uint16_t nameSize{};
            std::uint32_t recordPlusOne{};
        };

        std::vector<Record> records;
        std::unique_ptr<RuntimeRecord[]> runtimeRecords;
        std::vector<LookupSlot> lookup;
        std::array<std::uint32_t, kMaterialLoadQueueCapacity> loadQueue{};
        std::atomic_uint64_t loadQueueRead{};
        std::atomic_uint64_t loadQueueWrite{};
        std::atomic_bool loadTaskQueued{};
        std::atomic_uint32_t resolvedCount{};
        bool firstResolutionLogged{};

        void initialize()
        {
            std::sort(
                records.begin(),
                records.end(),
                [](const Record& left, const Record& right) {
                    return left.basePath < right.basePath;
                });
            if (records.empty()) {
                return;
            }
            runtimeRecords = std::make_unique<RuntimeRecord[]>(
                records.size());
            auto capacity = kMinimumMaterialLookupCapacity;
            const auto required = records.size() * 2;
            while (capacity < required) {
                capacity <<= 1;
            }
            lookup.resize(capacity);
        }

        [[nodiscard]] std::size_t hash(RE::NiTexture* texture) const noexcept
        {
            auto value = reinterpret_cast<std::uintptr_t>(texture) >> 4;
            value ^= value >> 17;
            value *= static_cast<std::uintptr_t>(0x9E3779B185EBCA87ull);
            return value & (lookup.size() - 1);
        }

        void cache(
            RE::NiTexture* baseTexture,
            std::string_view name,
            std::uint32_t recordIndex) noexcept
        {
            if (!baseTexture || lookup.empty()) {
                return;
            }
            auto slot = hash(baseTexture);
            for (std::size_t probe = 0;
                 probe < lookup.size();
                 ++probe) {
                auto& candidate = lookup[slot];
                if (!candidate.baseTexture ||
                    candidate.baseTexture == baseTexture) {
                    candidate.baseTexture = baseTexture;
                    candidate.nameData = name.data();
                    candidate.nameSize = static_cast<std::uint16_t>(
                        name.size());
                    candidate.recordPlusOne = recordIndex + 1;
                    return;
                }
                slot = (slot + 1) & (lookup.size() - 1);
            }
        }

        [[nodiscard]] std::optional<std::uint32_t> find(
            RE::NiTexture* baseTexture) noexcept
        {
            if (!baseTexture || records.empty() || lookup.empty()) {
                return std::nullopt;
            }
            const auto name = baseTexture->GetName();
            auto slot = hash(baseTexture);
            for (std::size_t probe = 0;
                 probe < lookup.size();
                 ++probe) {
                const auto& candidate = lookup[slot];
                if (!candidate.baseTexture) {
                    break;
                }
                if (candidate.baseTexture == baseTexture) {
                    const auto index = static_cast<std::uint32_t>(
                        candidate.recordPlusOne - 1);
                    if (index < records.size() &&
                        candidate.nameData == name.data() &&
                        candidate.nameSize == name.size()) {
                        return index;
                    }
                    break;
                }
                slot = (slot + 1) & (lookup.size() - 1);
            }

            std::array<char, 513> storage{};
            std::string_view normalized;
            if (!normalizeTexturePath(name, storage, normalized)) {
                return std::nullopt;
            }

            const auto found = std::lower_bound(
                records.begin(),
                records.end(),
                normalized,
                [](const Record& record, std::string_view path) {
                    return record.basePath < path;
                });
            if (found == records.end() || found->basePath != normalized) {
                return std::nullopt;
            }
            const auto index = static_cast<std::uint32_t>(
                std::distance(records.begin(), found));
            cache(baseTexture, name, index);
            return index;
        }

        [[nodiscard]] bool queueLoad(std::uint32_t recordIndex) noexcept
        {
            if (recordIndex >= records.size() || !runtimeRecords) {
                return false;
            }
            auto& runtime = runtimeRecords[recordIndex];
            auto state = runtime.loadState.load(std::memory_order_acquire);
            if (state == LoadState::loaded || state == LoadState::failed) {
                return false;
            }
            if (state == LoadState::queued) {
                return true;
            }
            auto expected = LoadState::unloaded;
            if (!runtime.loadState.compare_exchange_strong(
                    expected,
                    LoadState::queued,
                    std::memory_order_acq_rel)) {
                return expected == LoadState::queued;
            }

            const auto write = loadQueueWrite.load(std::memory_order_relaxed);
            const auto read = loadQueueRead.load(std::memory_order_acquire);
            if (write - read >= loadQueue.size()) {
                runtime.loadState.store(
                    LoadState::unloaded,
                    std::memory_order_release);
                return false;
            }
            loadQueue[write % loadQueue.size()] = recordIndex;
            loadQueueWrite.store(write + 1, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool popLoad(std::uint32_t& recordIndex) noexcept
        {
            const auto read = loadQueueRead.load(std::memory_order_relaxed);
            const auto write = loadQueueWrite.load(std::memory_order_acquire);
            if (read == write) {
                return false;
            }
            recordIndex = loadQueue[read % loadQueue.size()];
            loadQueueRead.store(read + 1, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool hasQueuedLoads() const noexcept
        {
            return loadQueueRead.load(std::memory_order_acquire) !=
                loadQueueWrite.load(std::memory_order_acquire);
        }

        void cancelQueuedLoads() noexcept
        {
            std::uint32_t recordIndex{};
            while (popLoad(recordIndex)) {
                if (recordIndex >= records.size() || !runtimeRecords) {
                    continue;
                }
                auto expected = LoadState::queued;
                runtimeRecords[recordIndex].loadState.compare_exchange_strong(
                    expected,
                    LoadState::unloaded,
                    std::memory_order_acq_rel);
            }
        }

        [[nodiscard]] ID3D11ShaderResourceView* readyView(
            std::uint32_t recordIndex,
            ID3D11Device* device) noexcept
        {
            if (recordIndex >= records.size() || !runtimeRecords || !device) {
                return nullptr;
            }
            auto& runtime = runtimeRecords[recordIndex];
            if (runtime.loadState.load(std::memory_order_acquire) !=
                LoadState::loaded) {
                return nullptr;
            }
            auto* view = shaderResource(runtime.rmaosTexture);
            if (!view || !belongsToDevice(view, device)) {
                return nullptr;
            }
            runtime.rmaosView = view;
            if (!runtime.resolutionCounted) {
                runtime.resolutionCounted = true;
                resolvedCount.fetch_add(1, std::memory_order_relaxed);
                if (!firstResolutionLogged) {
                    firstResolutionLogged = true;
                    logging::info(
                        "Authored PBR resolved its first lazy RMAOS texture ('{}' -> '{}').",
                        records[recordIndex].basePath,
                        records[recordIndex].rmaosPath);
                }
            }
            return runtime.rmaosView;
        }
    };

    ScopedAuthoredMaterialBindings::ScopedAuthoredMaterialBindings(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* rmaos,
        ID3D11PixelShader* previousShader,
        ID3D11PixelShader* authoredShader) noexcept :
        context_(context),
        previousShader_(previousShader),
        authoredShader_(authoredShader)
    {
        if (!context_ || !rmaos || !previousShader_ || !authoredShader_) {
            context_ = nullptr;
            authoredShader_ = nullptr;
            return;
        }
        context_->PSGetShaderResources(
            kAuthoredRmaosSlot,
            1,
            previousRmaos_.GetAddressOf());
        context_->PSSetShaderResources(kAuthoredRmaosSlot, 1, &rmaos);
        ID3D11ShaderResourceView* applied{};
        context_->PSGetShaderResources(kAuthoredRmaosSlot, 1, &applied);
        const auto matched = applied == rmaos;
        if (applied) {
            applied->Release();
        }
        if (!matched) {
            auto* previous = previousRmaos_.Get();
            context_->PSSetShaderResources(
                kAuthoredRmaosSlot, 1, &previous);
            context_ = nullptr;
            authoredShader_ = nullptr;
        }
    }

    ScopedAuthoredMaterialBindings::~ScopedAuthoredMaterialBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previous = previousRmaos_.Get();
        context_->PSSetShaderResources(kAuthoredRmaosSlot, 1, &previous);
    }

    ScopedAuthoredMaterialBindings::ScopedAuthoredMaterialBindings(
        ScopedAuthoredMaterialBindings&& other) noexcept :
        context_(std::exchange(other.context_, nullptr)),
        previousRmaos_(std::move(other.previousRmaos_)),
        previousShader_(std::move(other.previousShader_)),
        authoredShader_(std::exchange(other.authoredShader_, nullptr))
    {}

    ScopedDrawBindings::ScopedDrawBindings(
        ID3D11DeviceContext* context,
        ID3D11Buffer* constants,
        ID3D11ShaderResourceView* pbrMaterial,
        ID3D11ShaderResourceView* surfaceClass,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context), restoreCounter_(restoreCounter)
    {
        if (!context_ || !constants) {
            context_ = nullptr;
            return;
        }
        context_->PSGetConstantBuffers(
            kConstantSlot, 1, previousConstants_.GetAddressOf());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        if (surfaceClass) {
            context_->PSGetShaderResources(
                kPbrMaterialSlot,
                1,
                previousPbrMaterial_.GetAddressOf());
            context_->PSGetShaderResources(
                kSurfaceClassSlot,
                1,
                previousSurfaceClass_.GetAddressOf());
            context_->PSSetShaderResources(
                kPbrMaterialSlot,
                1,
                &pbrMaterial);
            context_->PSSetShaderResources(
                kSurfaceClassSlot, 1, &surfaceClass);
            materialResourcesBound_ = true;
        }
    }

    ScopedDrawBindings::~ScopedDrawBindings() noexcept
    {
        if (!context_) {
            return;
        }
        auto* previousConstants = previousConstants_.Get();
        if (materialResourcesBound_) {
            auto* previousPbrMaterial = previousPbrMaterial_.Get();
            auto* previousSurfaceClass = previousSurfaceClass_.Get();
            context_->PSSetShaderResources(
                kPbrMaterialSlot,
                1,
                &previousPbrMaterial);
            context_->PSSetShaderResources(
                kSurfaceClassSlot,
                1,
                &previousSurfaceClass);
        }
        context_->PSSetConstantBuffers(
            kConstantSlot, 1, &previousConstants);
        if (restoreCounter_) {
            restoreCounter_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    Runtime::~Runtime() = default;

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device;
        context_ = context;
        constants_.Reset();
        uploadedRevision_ = 0;
        uploadedActive_ = false;
        if (!device || !context) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(FrameData);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const auto result = device->CreateBuffer(
            &description, nullptr, constants_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !constants_) {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "PBR settings-buffer creation failed (HRESULT=0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info(
            "PBR direct-light constants ready at private b7; exact FO4VR directional DFLight contracts remain the draw owner.");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        configuredEnabled_.store(safe.enabled, std::memory_order_release);
        legacyMaterials_.store(safe.legacyMaterials, std::memory_order_relaxed);
        directGgx_.store(safe.directGgx, std::memory_order_relaxed);
        grassGgx_.store(safe.grassGgx, std::memory_order_relaxed);
        environmentFresnel_.store(
            safe.environmentFresnel, std::memory_order_relaxed);
        energyConservation_.store(
            safe.energyConservation, std::memory_order_relaxed);
        multiscatterCompensation_.store(
            safe.multiscatterCompensation, std::memory_order_relaxed);
        specularOcclusion_.store(
            safe.specularOcclusion, std::memory_order_relaxed);
        roughnessMultiplier_.store(
            safe.roughnessMultiplier, std::memory_order_relaxed);
        specularRoughnessBlend_.store(
            safe.specularRoughnessBlend, std::memory_order_relaxed);
        baseF0Multiplier_.store(
            safe.baseF0Multiplier, std::memory_order_relaxed);
        minimumF0_.store(safe.minimumF0, std::memory_order_relaxed);
        cubemapToF0Multiplier_.store(
            safe.cubemapToF0Multiplier, std::memory_order_relaxed);
        complexMaterialF0Multiplier_.store(
            safe.complexMaterialF0Multiplier, std::memory_order_relaxed);
        directLightingScale_.store(
            safe.directLightingScale, std::memory_order_relaxed);
        settingsRevision_.fetch_add(1, std::memory_order_release);
        publishEffectiveState(safe);
    }

    void Runtime::onGameDataReady() noexcept
    {
        if (materialRegistryLoaded_.exchange(
                true,
                std::memory_order_acq_rel)) {
            return;
        }
        auto next = std::make_unique<MaterialRegistry>();
        try {
            const auto root = manifestRoot();
            std::error_code error;
            if (root.empty() || !std::filesystem::exists(root, error) ||
                error) {
                surface_classification::Runtime::get()
                    .setPbrMaterialTransportEnabled(false);
                return;
            }

            std::vector<std::filesystem::path> manifests;
            for (std::filesystem::recursive_directory_iterator iterator(
                     root,
                     std::filesystem::directory_options::
                         skip_permission_denied,
                     error),
                 end;
                 iterator != end && !error;
                 iterator.increment(error)) {
                if (iterator->is_regular_file(error) && !error &&
                    iterator->path().extension() == ".json") {
                    manifests.push_back(iterator->path());
                }
            }
            if (error) {
                manifestFailures_.fetch_add(1, std::memory_order_relaxed);
                logging::error(
                    "Authored PBR manifest enumeration failed under '{}': {}.",
                    root.string(),
                    error.message());
                surface_classification::Runtime::get()
                    .setPbrMaterialTransportEnabled(false);
                return;
            }
            std::sort(manifests.begin(), manifests.end());
            std::unordered_set<std::string> basePaths;
            next->records.reserve((std::min)(
                manifests.size() * 8,
                kMaximumAuthoredMaterials));
            for (const auto& manifest : manifests) {
                std::ifstream stream(manifest);
                if (!stream) {
                    manifestFailures_.fetch_add(
                        1, std::memory_order_relaxed);
                    logging::error(
                        "Authored PBR could not open manifest '{}'.",
                        manifest.string());
                    continue;
                }
                nlohmann::json document;
                try {
                    stream >> document;
                } catch (const nlohmann::json::exception& exception) {
                    manifestFailures_.fetch_add(
                        1, std::memory_order_relaxed);
                    logging::error(
                        "Authored PBR could not parse manifest '{}': {}.",
                        manifest.string(),
                        exception.what());
                    continue;
                }
                const auto found = document.find("materials");
                if (!document.is_object() || found == document.end() ||
                    !found->is_array()) {
                    manifestFailures_.fetch_add(
                        1, std::memory_order_relaxed);
                    logging::error(
                        "Authored PBR manifest '{}' must contain a materials array.",
                        manifest.string());
                    continue;
                }
                for (const auto& item : *found) {
                    if (next->records.size() >=
                        kMaximumAuthoredMaterials) {
                        manifestFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        logging::error(
                            "Authored PBR exceeded the validated {} material limit; remaining records are ignored.",
                            kMaximumAuthoredMaterials);
                        break;
                    }
                    if (!item.is_object() || !item.contains("base") ||
                        !item.contains("rmaos") ||
                        !item["base"].is_string() ||
                        !item["rmaos"].is_string()) {
                        manifestFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        logging::error(
                            "Authored PBR manifest '{}' contains a record without string base/rmaos paths.",
                            manifest.string());
                        continue;
                    }
                    const auto base = normalizeTexturePath(
                        item["base"].get<std::string>());
                    const auto rmaos = normalizeTexturePath(
                        item["rmaos"].get<std::string>());
                    if (!base || !rmaos) {
                        manifestFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        logging::error(
                            "Authored PBR manifest '{}' contains a non-Data DDS path.",
                            manifest.string());
                        continue;
                    }
                    if (!basePaths.insert(*base).second) {
                        manifestFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        logging::error(
                            "Authored PBR base texture '{}' is declared more than once; the first record remains authoritative.",
                            *base);
                        continue;
                    }
                    next->records.push_back({
                        .basePath = *base,
                        .rmaosPath = *rmaos,
                    });
                }
            }
            next->initialize();
        } catch (const std::exception& exception) {
            manifestFailures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Authored PBR manifest loading failed closed: {}.",
                exception.what());
            next->records.clear();
            next->runtimeRecords.reset();
            next->lookup.clear();
        } catch (...) {
            manifestFailures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Authored PBR manifest loading failed closed with an unknown error.");
            next->records.clear();
            next->runtimeRecords.reset();
            next->lookup.clear();
        }

        const auto count = next->records.size();
        materialRegistryOwner_ = std::move(next);
        materialRegistry_.store(
            materialRegistryOwner_.get(),
            std::memory_order_release);
        publishEffectiveState(settings());
        if (count != 0) {
            logging::info(
                "Authored PBR indexed {} material records from Data\\F4SE\\Plugins\\CSAH\\PBRMaterials without preloading texture assets; RMAOS textures will stream on first observed use.",
                count);
        }
    }

    void Runtime::setLinearLightingEnabled(bool enabled) noexcept
    {
        linearLightingEnabled_.store(enabled, std::memory_order_release);
        settingsRevision_.fetch_add(1, std::memory_order_release);
        publishEffectiveState(settings());
    }

    void Runtime::publishEffectiveState(const Settings& settings) noexcept
    {
        const auto effective = effectiveEnabled(
            settings,
            linearLightingEnabled_.load(std::memory_order_acquire));
        const auto* registry = materialRegistry_.load(
            std::memory_order_acquire);
        surface_classification::Runtime::get().setConsumerEnabled(
            surface_classification::Consumer::pbr,
            effective);
        const auto authoredTransport =
            effective && registry && !registry->records.empty();
        authoredTransportEnabled_.store(
            authoredTransport,
            std::memory_order_release);
        surface_classification::Runtime::get()
            .setPbrMaterialTransportEnabled(
                authoredTransport);
        render::setDFPrePassAuthoredPbrEnabled(authoredTransport);
        linear_lighting::Runtime::get().setPbrMaterialsEnabled(effective);
        auto effectiveSettings = settings;
        effectiveSettings.enabled = effective;
        ibl::Runtime::get().applyPbrSettings(effectiveSettings);
    }

    bool Runtime::requested() const noexcept
    {
        return configuredEnabled_.load(std::memory_order_acquire) &&
            linearLightingEnabled_.load(std::memory_order_acquire) &&
            resourcesReady_.load(std::memory_order_acquire);
    }

    Settings Runtime::settings() const noexcept
    {
        return sanitize({
            .enabled = configuredEnabled_.load(std::memory_order_acquire),
            .legacyMaterials = legacyMaterials_.load(std::memory_order_relaxed),
            .directGgx = directGgx_.load(std::memory_order_relaxed),
            .grassGgx = grassGgx_.load(std::memory_order_relaxed),
            .environmentFresnel = environmentFresnel_.load(
                std::memory_order_relaxed),
            .energyConservation = energyConservation_.load(
                std::memory_order_relaxed),
            .multiscatterCompensation = multiscatterCompensation_.load(
                std::memory_order_relaxed),
            .specularOcclusion = specularOcclusion_.load(
                std::memory_order_relaxed),
            .roughnessMultiplier = roughnessMultiplier_.load(
                std::memory_order_relaxed),
            .specularRoughnessBlend = specularRoughnessBlend_.load(
                std::memory_order_relaxed),
            .baseF0Multiplier = baseF0Multiplier_.load(
                std::memory_order_relaxed),
            .minimumF0 = minimumF0_.load(std::memory_order_relaxed),
            .cubemapToF0Multiplier = cubemapToF0Multiplier_.load(
                std::memory_order_relaxed),
            .complexMaterialF0Multiplier =
                complexMaterialF0Multiplier_.load(
                    std::memory_order_relaxed),
            .directLightingScale = directLightingScale_.load(
                std::memory_order_relaxed),
        });
    }

    void Runtime::uploadSettings(
        ID3D11DeviceContext* context,
        bool active) noexcept
    {
        const auto revision = settingsRevision_.load(std::memory_order_acquire);
        if (!context || !constants_ ||
            (revision == uploadedRevision_ && active == uploadedActive_)) {
            return;
        }
        const auto data = makeFrameData(settings(), active);
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        uploadedRevision_ = revision;
        uploadedActive_ = active;
        settingsUploads_.fetch_add(1, std::memory_order_relaxed);
    }

    ScopedDrawBindings Runtime::scopeDraw(
        ID3D11DeviceContext* context,
        bool active) noexcept
    {
        if (!context || context != context_.Get() || !constants_ ||
            !resourcesReady_.load(std::memory_order_acquire)) {
            return {};
        }
        auto* surfaceClass = active ?
            surface_classification::Runtime::get().shaderResourceView() :
            nullptr;
        auto* pbrMaterial = active ?
            surface_classification::Runtime::get()
                .pbrMaterialShaderResourceView() : nullptr;
        if (active && !surfaceClass) {
            return {};
        }
        uploadSettings(context, active);
        drawScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedDrawBindings(
            context,
            constants_.Get(),
            pbrMaterial,
            surfaceClass,
            &drawRestores_);
    }

    ScopedAuthoredMaterialBindings Runtime::scopeAuthoredMaterialDraw(
        ID3D11DeviceContext* context,
        const linear_lighting::ReplacementShaderBinding& binding,
        std::uint32_t surfaceClassCode,
        RE::NiTexture* baseTexture) noexcept
    {
        auto* registry = materialRegistry_.load(std::memory_order_acquire);
        if (!registry || registry->records.empty() || !requested() ||
            !context || context != context_.Get() || !device_ ||
            !baseTexture) {
            return {};
        }
        const auto recordIndex = registry->find(baseTexture);
        if (!recordIndex) {
            return {};
        }
        auto* rmaosView = registry->readyView(
            *recordIndex,
            device_.Get());
        if (!rmaosView) {
            if (registry->queueLoad(*recordIndex)) {
                requestMaterialLoadPump();
            }
            return {};
        }

        ID3D11PixelShader* currentRaw{};
        context->PSGetShader(&currentRaw, nullptr, nullptr);
        Microsoft::WRL::ComPtr<ID3D11PixelShader> current;
        current.Attach(currentRaw);
        auto* authored = linear_lighting::Runtime::get()
                             .authoredPbrSurfaceShader(
                                 binding,
                                 surfaceClassCode,
                                 current.Get());
        if (!authored) {
            return {};
        }
        ScopedAuthoredMaterialBindings scope(
            context,
            rmaosView,
            current.Get(),
            authored);
        if (!scope.active()) {
            failures_.fetch_add(1, std::memory_order_relaxed);
        }
        return scope;
    }

    void Runtime::requestMaterialLoadPump() noexcept
    {
        auto* registry = materialRegistry_.load(std::memory_order_acquire);
        if (!registry ||
            !authoredTransportEnabled_.load(std::memory_order_acquire) ||
            !registry->hasQueuedLoads()) {
            return;
        }
        auto expected = false;
        if (!registry->loadTaskQueued.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel)) {
            return;
        }
        const auto* tasks = F4SE::GetTaskInterface();
        if (!tasks) {
            registry->loadTaskQueued.store(false, std::memory_order_release);
            failures_.fetch_add(1, std::memory_order_relaxed);
            if (!materialLoadTaskFailureLogged_.exchange(
                    true,
                    std::memory_order_acq_rel)) {
                logging::error(
                    "Authored PBR lazy texture loading is pending because the F4SE main-thread task interface is unavailable.");
            }
            return;
        }
        try {
            tasks->AddTask([]() noexcept {
                Runtime::get().processMaterialLoadsOnMainThread();
            });
        } catch (const std::exception& exception) {
            registry->loadTaskQueued.store(false, std::memory_order_release);
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Authored PBR lazy texture task could not be queued: {}.",
                exception.what());
        } catch (...) {
            registry->loadTaskQueued.store(false, std::memory_order_release);
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error(
                "Authored PBR lazy texture task could not be queued.");
        }
    }

    void Runtime::processMaterialLoadsOnMainThread() noexcept
    {
        auto* registry = materialRegistry_.load(std::memory_order_acquire);
        if (!registry) {
            return;
        }
        if (!authoredTransportEnabled_.load(std::memory_order_acquire)) {
            registry->cancelQueuedLoads();
            registry->loadTaskQueued.store(false, std::memory_order_release);
            return;
        }
        for (std::size_t load = 0;
             load < kMaterialLoadsPerMainThreadTask;
             ++load) {
            if (!authoredTransportEnabled_.load(
                    std::memory_order_acquire)) {
                break;
            }
            std::uint32_t recordIndex{};
            if (!registry->popLoad(recordIndex)) {
                break;
            }
            if (recordIndex >= registry->records.size() ||
                !registry->runtimeRecords) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            auto& runtime = registry->runtimeRecords[recordIndex];
            if (runtime.loadState.load(std::memory_order_acquire) !=
                MaterialRegistry::LoadState::queued) {
                continue;
            }
            auto texture = loadTexture(
                registry->records[recordIndex].rmaosPath,
                false);
            if (!texture) {
                runtime.loadState.store(
                    MaterialRegistry::LoadState::failed,
                    std::memory_order_release);
                const auto failure = failures_.fetch_add(
                                         1,
                                         std::memory_order_relaxed) +
                    1;
                if ((failure & (failure - 1)) == 0) {
                    logging::error(
                        "Authored PBR could not load lazy RMAOS texture '{}' (failures={}).",
                        registry->records[recordIndex].rmaosPath,
                        failure);
                }
                continue;
            }
            runtime.rmaosTexture = std::move(texture);
            runtime.loadState.store(
                MaterialRegistry::LoadState::loaded,
                std::memory_order_release);
        }

        registry->loadTaskQueued.store(false, std::memory_order_release);
        if (!authoredTransportEnabled_.load(std::memory_order_acquire)) {
            registry->cancelQueuedLoads();
        } else if (registry->hasQueuedLoads()) {
            requestMaterialLoadPump();
        }
    }

    void Runtime::recordDrawFallback() noexcept
    {
        drawFallbacks_.fetch_add(1, std::memory_order_relaxed);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        const auto* registry = materialRegistry_.load(
            std::memory_order_acquire);
        return {
            .settings = settings(),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .drawScopes = drawScopes_.load(std::memory_order_relaxed),
            .drawRestores = drawRestores_.load(std::memory_order_relaxed),
            .drawFallbacks = drawFallbacks_.load(std::memory_order_relaxed),
            .settingsUploads = settingsUploads_.load(
                std::memory_order_relaxed),
            .authoredMaterials = registry ?
                static_cast<std::uint32_t>(registry->records.size()) : 0u,
            .resolvedAuthoredMaterials = registry ?
                registry->resolvedCount.load(std::memory_order_relaxed) : 0u,
            .manifestFailures = manifestFailures_.load(
                std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
