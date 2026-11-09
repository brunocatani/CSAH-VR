#include "Features/skylighting/SkylightingSettingsStore.h"
#include "settings/SettingsFile.h"

#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string_view>

namespace csah::skylighting
{
    namespace
    {
        constexpr std::string_view kSection = "Skylighting";

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

        [[nodiscard]] std::uint32_t readInteger(
            const settings_file::Values& values,
            std::string_view key,
            std::uint32_t fallback) noexcept
        {
            return values.unsignedInteger(kSection, key, fallback);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};

        return master_settings::gate(values, sanitize({
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .quality = sanitizeQuality(readInteger(values,
                "Quality",
                static_cast<std::uint32_t>(defaults.quality))),
            .minimumDiffuseVisibility = readFloat(values,
                "MinDiffuseVisibility",
                defaults.minimumDiffuseVisibility),
            .minimumSpecularVisibility = readFloat(values,
                "MinSpecularVisibility",
                defaults.minimumSpecularVisibility),
            .maximumZenithDegrees = readFloat(values,
                "MaxZenithDegrees",
                defaults.maximumZenithDegrees),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Skylighting settings loaded; enabled={}, quality={}, minimum diffuse visibility={}, minimum specular visibility={}, maximum zenith={} degrees.",
            result.enabled,
            qualityName(result.quality),
            result.minimumDiffuseVisibility,
            result.minimumSpecularVisibility,
            result.maximumZenithDegrees);
        return result;
    }
}
