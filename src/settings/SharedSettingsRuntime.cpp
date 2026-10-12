#include "diagnostics/stereo_measurement/StereoMeasurement.h"
#include "Features/inverse_square/InverseSquareRuntime.h"
#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "Features/water_effects/WaterEffectsRuntime.h"
#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "settings/SharedSettingsRuntime.h"

#include "Features/basic_wetness/BasicWetnessRuntime.h"
#include "Features/lod_blending/LodBlendingRuntime.h"
#include "Features/terrain_variation/TerrainVariationRuntime.h"
#include "Features/lod_blending/LodBlendingSettingsStore.h"
#include "Features/basic_wetness/BasicWetnessSettingsStore.h"
#include "Features/bloom_glare/BloomGlareRuntime.h"
#include "Features/bloom_glare/BloomGlareSettingsStore.h"
#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/cloud_shadows/CloudShadowSettingsStore.h"
#include "Features/complex_materials/ComplexParallaxSettingsStore.h"
#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/DlaaSettingsStore.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/ibl/IblSettingsStore.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "Features/pbr/PbrRuntime.h"
#include "Features/pbr/PbrSettingsStore.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/skylighting/SkylightingSettingsStore.h"
#include "Features/sky_sync/SkySyncRuntime.h"
#include "Features/sky_sync/SkySyncSettingsStore.h"
#include "Features/vanilla_fixes/VanillaFixesRuntime.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/volumetric_lighting/VolumetricLightingRuntime.h"
#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"
#include "render/GpuTimingProfiler.h"
#include "diagnostics/RenderDocCapture.h"
#include "settings/DiagnosticsSettings.h"
#include "settings/MasterSettings.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace csah::shared_settings
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr auto kPollInterval = 200ms;
        constexpr std::uintmax_t kMaximumIniBytes = 1024u * 1024u;
        constexpr std::uint32_t kRequiredStableObservations = 2;

        struct FileSignature final
        {
            std::filesystem::file_time_type writeTime{};
            std::uintmax_t size{};

            [[nodiscard]] bool operator==(
                const FileSignature&) const noexcept = default;
        };

        [[nodiscard]] std::optional<FileSignature> readSignature(
            const std::filesystem::path& path) noexcept
        {
            std::error_code regularError;
            if (path.empty() ||
                !std::filesystem::is_regular_file(path, regularError) ||
                regularError) {
                return std::nullopt;
            }

            std::error_code timeError;
            std::error_code sizeError;
            const auto writeTime =
                std::filesystem::last_write_time(path, timeError);
            const auto size = std::filesystem::file_size(path, sizeError);
            if (timeError || sizeError || size > kMaximumIniBytes) {
                return std::nullopt;
            }
            return FileSignature{ .writeTime = writeTime, .size = size };
        }

        [[nodiscard]] Snapshot loadSnapshot(
            const std::filesystem::path& path) noexcept
        {
            return {
                .masterEnabled = master_settings::enabled(path),
                .diagnostics = diagnostics_settings::load(path),
                .linearLighting = linear_lighting::loadSettings(path),
                .dlaa = dlaa::loadSettings(path),
                .filmicTonemapping =
                    filmic_tonemapping::loadSettings(path),
                .bloomGlare = bloom_glare::loadSettings(path),
                .ibl = ibl::loadSettings(path),
                .complexMaterials =
                    complex_materials::loadSettings(path),
                .contactShadows = contact_shadows::loadSettings(path),
                .basicWetness = basic_wetness::loadSettings(path),
                .lodBlending = lod_blending::loadSettings(path),
                .terrainVariation = terrain_variation::loadSettings(path),
                .screenSpaceGI = screen_space_gi::loadSettings(path),
                .waterEffects = water_effects::loadSettings(path),
                .terrainShadows = terrain_shadows::loadSettings(path),
                .inverseSquare = inverse_square::loadSettings(path),
                .cloudShadows = cloud_shadows::loadSettings(path),
                .volumetricLighting =
                    volumetric_lighting::loadSettings(path),
                .vanillaFixes = vanilla_fixes::loadSettings(path),
                .nativeShadows = native_shadows::loadSettings(path),
                .pbr = pbr::loadSettings(path),
                .skylighting = skylighting::loadSettings(path),
                .skySync = sky_sync::loadSettings(path),
            };
        }

        void applyLiveChanges(
            const ChangeSet& changes,
            const Snapshot& next) noexcept
        {
            if (changes.diagnostics) {
                render::GpuTimingProfiler::setOnDemandGroups(
                    next.diagnostics.gpuProfilingGroups | (next.diagnostics.measureStereoSharing ? 8u : 0u));
                diagnostics::stereo_measurement::Runtime::get().setEnabled(next.diagnostics.measureStereoSharing);
            }
            if (changes.linearLighting) {
                linear_lighting::Runtime::get().queueSettings(
                    next.linearLighting);
                pbr::Runtime::get().setLinearLightingEnabled(
                    next.linearLighting.enabled);
            }
            if (changes.dlaa) {
                dlaa::Runtime::get().applySettings(next.dlaa);
            }
            if (changes.filmicTonemapping) {
                filmic_tonemapping::Runtime::get().applySettings(
                    next.filmicTonemapping);
            }
            if (changes.bloomGlare) {
                bloom_glare::Runtime::get().applySettings(next.bloomGlare);
            }
            if (changes.ibl) {
                ibl::Runtime::get().applySettings(next.ibl);
            }
            if (changes.complexMaterials) {
                linear_lighting::Runtime::get().queueComplexParallaxSettings(
                    next.complexMaterials);
            }
            if (changes.contactShadows) {
                contact_shadows::Runtime::get().applySettings(
                    next.contactShadows);
            }
            if(changes.inverseSquare) inverse_square::Runtime::get().applySettings(next.inverseSquare);
            if(changes.terrainShadows) terrain_shadows::Runtime::get().applySettings(next.terrainShadows);
            if(changes.waterEffects) water_effects::Runtime::get().applySettings(next.waterEffects);
            if(changes.screenSpaceGI) screen_space_gi::Runtime::get().applySettings(next.screenSpaceGI);
            if(changes.terrainVariation) terrain_variation::Runtime::get().applySettings(next.terrainVariation);
            if(changes.lodBlending) lod_blending::Runtime::get().applySettings(next.lodBlending);
            if (changes.basicWetness) {
                basic_wetness::Runtime::get().applySettings(
                    next.basicWetness);
            }
            if (changes.cloudShadows) {
                cloud_shadows::Runtime::get().applySettings(
                    next.cloudShadows);
            }
            if (changes.volumetricLighting) {
                volumetric_lighting::Runtime::get().applySettings(
                    next.volumetricLighting);
            }
            if (changes.vanillaFixes) {
                vanilla_fixes::applySettings(next.vanillaFixes);
                volumetric_lighting::Runtime::get().setDiagnosticSuppressed(
                    next.vanillaFixes.enabled &&
                    next.vanillaFixes.directionalLightDiagnosticMode !=
                        vanilla_fixes::DirectionalLightDiagnosticMode::off);
            }
            if (changes.pbr) {
                pbr::Runtime::get().applySettings(next.pbr);
            }
            if (changes.skylighting) {
                skylighting::Runtime::get().applySettings(next.skylighting);
            }
            if (changes.skySync) {
                sky_sync::Runtime::get().applySettings(next.skySync);
            }
        }

        class Controller final
        {
        public:
            [[nodiscard]] static Controller& get() noexcept
            {
                static auto* controller = new Controller();
                return *controller;
            }

            [[nodiscard]] bool start() noexcept
            {
                auto expected = false;
                if (!started_.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    return true;
                }

                configPath_ = settings_path::resolveIniPath();
                if (configPath_.empty()) {
                    started_.store(false, std::memory_order_release);
                    return false;
                }
                active_ = loadSnapshot(configPath_);
                render::GpuTimingProfiler::setOnDemandGroups(
                    active_.diagnostics.gpuProfilingGroups | (active_.diagnostics.measureStereoSharing ? 8u : 0u));
                diagnostics::stereo_measurement::Runtime::get().setEnabled(active_.diagnostics.measureStereoSharing);
                accepted_ = readSignature(configPath_);

                try {
                    monitor_ = std::jthread(
                        [this](const std::stop_token stopToken) noexcept {
                            monitorMain(stopToken);
                        });
                } catch (const std::exception& error) {
                    logging::warn(
                        "Shared settings monitor could not start: {}.",
                        error.what());
                    started_.store(false, std::memory_order_release);
                    return false;
                } catch (...) {
                    logging::warn(
                        "Shared settings monitor could not start.");
                    started_.store(false, std::memory_order_release);
                    return false;
                }

                logging::info(
                    "Shared CSAH INI monitor started for '{}'; the visual suite, DLAA/DLSS, Vanilla Fixes, and Native Shadows retain independent master gates, with Native Shadows/Vanilla Fixes master changes restart-only.",
                    configPath_.string());
                return true;
            }

        private:
            Controller() = default;

            void monitorMain(const std::stop_token stopToken) noexcept
            {
                std::unique_lock lock(wakeMutex_);
                while (!stopToken.stop_requested()) {
                    wake_.wait_for(
                        lock,
                        stopToken,
                        kPollInterval,
                        []() noexcept { return false; });
                    if (stopToken.stop_requested()) {
                        break;
                    }
                    lock.unlock();
                    reloadIfStable();
                    diagnostics::serviceDeveloperCapture(configPath_.c_str());
                    lock.lock();
                }
            }

            void reloadIfStable() noexcept
            {
                try {
                    const auto observed = readSignature(configPath_);
                    if (!observed) {
                        pending_.reset();
                        pendingObservations_ = 0;
                        return;
                    }
                    if (accepted_ && *accepted_ == *observed) {
                        pending_.reset();
                        pendingObservations_ = 0;
                        return;
                    }
                    if (!pending_ || *pending_ != *observed) {
                        pending_ = observed;
                        pendingObservations_ = 1;
                        return;
                    }
                    ++pendingObservations_;
                    if (pendingObservations_ < kRequiredStableObservations) {
                        return;
                    }

                    auto next = loadSnapshot(configPath_);
                    const auto stable = readSignature(configPath_);
                    if (!stable || *stable != *observed) {
                        pending_ = stable;
                        pendingObservations_ = stable ? 1u : 0u;
                        return;
                    }

                    accepted_ = stable;
                    pending_.reset();
                    pendingObservations_ = 0;
                    const auto changes = diff(active_, next);
                    if (!changes.any()) {
                        return;
                    }

                    const bool activeVanillaFixesGate =
                        active_.vanillaFixes.enabled;
                    applyLiveChanges(changes, next);
                    active_ = next;
                    if (changes.vanillaFixesGate) {
                        // Keep the process-effective startup gate as the
                        // comparison authority until the next launch.
                        active_.vanillaFixes.enabled =
                            activeVanillaFixesGate;
                    }
                    const auto reloadNumber =
                        acceptedReloads_.fetch_add(
                            1,
                            std::memory_order_relaxed) +
                        1;
                    logging::info(
                        "Shared CSAH INI reload #{} accepted: live feature groups={}, startup-native restart pending={}.",
                        reloadNumber,
                        changes.liveFeatureCount(),
                        changes.nativeShadows || changes.vanillaFixesGate);
                } catch (const std::exception& error) {
                    logging::warn(
                        "Shared settings monitor rejected an INI reload: {}.",
                        error.what());
                } catch (...) {
                    logging::warn(
                        "Shared settings monitor rejected an INI reload.");
                }
            }

            std::atomic_bool started_{};
            std::filesystem::path configPath_{};
            Snapshot active_{};
            std::optional<FileSignature> accepted_{};
            std::optional<FileSignature> pending_{};
            std::uint32_t pendingObservations_{};
            std::atomic_uint64_t acceptedReloads_{};
            std::mutex wakeMutex_{};
            std::condition_variable_any wake_{};
            std::jthread monitor_{};
        };
    }

    bool startMonitor() noexcept
    {
        return Controller::get().start();
    }
}
