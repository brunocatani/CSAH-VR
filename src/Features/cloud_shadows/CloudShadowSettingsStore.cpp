#include "Features/cloud_shadows/CloudShadowSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string_view>

namespace csah::cloud_shadows
{
    namespace
    {
        constexpr std::string_view kSection = "CloudShadows";

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
            .opacity = readFloat(values, "Opacity", defaults.opacity),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Cloud Shadows settings loaded; enabled={}, opacity={}.",
            result.enabled, result.opacity);
        return result;
    }
}
