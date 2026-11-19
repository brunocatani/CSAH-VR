#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"

#include <cstdint>
#include <string_view>

namespace csah::vanilla_fixes
{
    namespace
    {
        constexpr std::string_view kVanillaFixesSection = "VanillaFixes";

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view key,
            const bool fallback) noexcept
        {
            return values.boolean(kVanillaFixesSection, key, fallback);
        }

        [[nodiscard]] DirectionalLightDiagnosticMode readDiagnosticMode(
            const settings_file::Values& values,
            const DirectionalLightDiagnosticMode fallback) noexcept
        {
            const auto raw = values.unsignedInteger("Diagnostics", "DirectionalLightingMode",
                static_cast<std::uint32_t>(fallback));
            if (raw > static_cast<std::uint32_t>(
                    DirectionalLightDiagnosticMode::cubemapLookupOnly)) {
                return fallback;
            }
            return static_cast<DirectionalLightDiagnosticMode>(raw);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        return Settings{
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .precipitationOcclusion = readBoolean(values,
                "PrecipitationOcclusion",
                defaults.precipitationOcclusion),
            .imageSpaceModifiers = readBoolean(values,
                "AllowImageSpaceModifiers",
                defaults.imageSpaceModifiers),
            .sao = !screen_space_gi::loadSettings(values).enabled,
            .screenSpaceReflections = readBoolean(values,
                "VrAllowScreenSpaceReflections",
                defaults.screenSpaceReflections),
            .nativeScreenSpaceMaterialPipeline = readBoolean(values,
                "NativeScreenSpaceMaterialPipeline",
                defaults.nativeScreenSpaceMaterialPipeline),
            .lensFlare = readBoolean(values,
                "LensFlareVr",
                defaults.lensFlare),
            .focusShadows = readBoolean(values,
                "VrAllowFocusShadows",
                defaults.focusShadows),
            .stereoSunOcclusion = readBoolean(values,
                "VrSunOcclusion",
                defaults.stereoSunOcclusion),
            .directionalLightDiagnosticMode = readDiagnosticMode(values,
                defaults.directionalLightDiagnosticMode),
        };
    }

    Settings loadSettings() noexcept
    {
        const auto settings = loadSettings(settings_file::File::get().current());
        logging::info(
            "Vanilla Fixes settings loaded; enabled={}, precipitation={}, imageModifiers={}, SAO={}, SSR={}, nativeScreenSpaceMaterialPipeline={}, lensFlare={}, focusShadows={}, stereoSunOcclusion={}, exclusiveDirectionalDiagnostic={}.",
            settings.enabled,
            settings.precipitationOcclusion,
            settings.imageSpaceModifiers,
            settings.sao,
            settings.screenSpaceReflections,
            settings.nativeScreenSpaceMaterialPipeline,
            settings.lensFlare,
            settings.focusShadows,
            settings.stereoSunOcclusion,
            static_cast<unsigned>(settings.directionalLightDiagnosticMode));
        return settings;
    }
}
