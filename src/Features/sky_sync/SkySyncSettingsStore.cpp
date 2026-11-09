#include "Features/sky_sync/SkySyncSettingsStore.h"
#include "settings/SettingsFile.h"

#include "settings/MasterSettings.h"
#include "support/Logger.h"

#include <string_view>

namespace csah::sky_sync
{
    namespace
    {
        constexpr std::string_view kSection = "SkySync";

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(kSection, key, fallback);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        return master_settings::gate(values, Settings{
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Sky Sync settings loaded; enabled={}.",
            result.enabled);
        return result;
    }
}
