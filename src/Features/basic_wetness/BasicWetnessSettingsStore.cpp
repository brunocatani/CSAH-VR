#include "Features/basic_wetness/BasicWetnessSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string_view>

namespace csah::basic_wetness
{
    namespace
    {
        constexpr std::string_view kSection = "BasicWetness";

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
            .wetness = readFloat(values, "Wetness", defaults.wetness),
            .diffuseDarkening = readFloat(values, "DiffuseDarkening", defaults.diffuseDarkening),
            .specularMultiplier = readFloat(values, "SpecularMultiplier", defaults.specularMultiplier),
            .roughnessScale = readFloat(values, "RoughnessScale", defaults.roughnessScale),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Basic Wetness settings loaded; enabled={}, wetness={}, darkening={}, specular={}, roughnessScale={}.",
            result.enabled, result.wetness,
            result.diffuseDarkening, result.specularMultiplier,
            result.roughnessScale);
        return result;
    }
}
