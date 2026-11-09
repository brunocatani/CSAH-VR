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
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Filmic Tonemapping settings loaded; enabled={}, nativeAutoExposure={}, exposureEV={}, filmicStrength={}, whitePointScale={}.",
            result.enabled,
            result.useNativeAutoExposure,
            result.exposureCompensationEV,
            result.filmicStrength,
            result.whitePointScale);
        return result;
    }
}
