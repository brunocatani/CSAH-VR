#include "Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string_view>

namespace csah::filmic_tonemapping
{
    namespace
    {
        constexpr std::string_view kSection = "FilmicTonemapping";

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(kSection, key, fallback);
        }

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view key,
            float fallback) noexcept
        {
            return values.number(kSection, key, fallback);
        }

        [[nodiscard]] color_grading::Settings loadGrading(
            const settings_file::Values& values) noexcept
        {
            constexpr std::string_view section = "ColorGrading";
            const color_grading::Settings defaults{};
            const auto number = [&](std::string_view key, float fallback) {
                return values.number(section, key, fallback);
            };
            return {
                .enabled = values.boolean(section, "Enabled", defaults.enabled) &&
                    master_settings::enabled(values),
                .tableStrength = number("TableStrength", defaults.tableStrength),
                .brightness = number("Brightness", defaults.brightness),
                .contrast = number("Contrast", defaults.contrast),
                .saturation = number("Saturation", defaults.saturation),
                .gamma = number("Gamma", defaults.gamma),
                .blackPoint = number("BlackPoint", defaults.blackPoint),
                .whitePoint = number("WhitePoint", defaults.whitePoint),
                .desaturateShadows =
                    number("DesaturateShadows", defaults.desaturateShadows),
                .shadowTint = {
                    number("ShadowsRed", 0.0f),
                    number("ShadowsGreen", 0.0f),
                    number("ShadowsBlue", 0.0f),
                },
                .highlightTint = {
                    number("HighlightsRed", 0.0f),
                    number("HighlightsGreen", 0.0f),
                    number("HighlightsBlue", 0.0f),
                },
            };
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        return master_settings::gate(values, sanitize({
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .useNativeAutoExposure = readBoolean(values,
                "UseNativeAutoExposure",
                defaults.useNativeAutoExposure),
            .exposureCompensationEV = readFloat(values,
                "ExposureCompensationEV",
                defaults.exposureCompensationEV),
            .filmicStrength = readFloat(values, "FilmicStrength", defaults.filmicStrength),
            .whitePointScale = readFloat(values, "WhitePointScale", defaults.whitePointScale),
            .grading = loadGrading(values),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Filmic Tonemapping settings loaded; enabled={}, nativeAutoExposure={}, exposureEV={}, filmicStrength={}, whitePointScale={}; colour grading enabled={}, tableStrength={}, controlsNeutral={}.",
            result.enabled,
            result.useNativeAutoExposure,
            result.exposureCompensationEV,
            result.filmicStrength,
            result.whitePointScale,
            result.grading.enabled,
            result.grading.tableStrength,
            color_grading::controlsNeutral(result.grading));
        return result;
    }
}
