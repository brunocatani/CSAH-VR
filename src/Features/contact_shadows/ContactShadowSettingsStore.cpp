#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cstdint>
#include <string_view>

namespace csah::contact_shadows
{
    namespace
    {
        constexpr std::string_view kSection = "ContactShadows";

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

        [[nodiscard]] std::uint32_t readUnsigned(
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
            .foveated = readBoolean(values,
                "Foveated",
                defaults.foveated),
            .strength = readFloat(values, "Strength", defaults.strength),
            .maxDistance = readFloat(values,
                "MaxDistance",
                defaults.maxDistance),
            .fadeDistance = readFloat(values,
                "FadeDistance",
                defaults.fadeDistance),
            .thickness = readFloat(values,
                "Thickness",
                defaults.thickness),
            .sampleCount = readUnsigned(values,
                "SampleCount",
                defaults.sampleCount),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Contact Shadows settings loaded; enabled={}, foveated={}, strength={}, ray distance={}, fade distance={}, thickness={}, samples={}.",
            result.enabled,
            result.foveated,
            result.strength,
            result.maxDistance,
            result.fadeDistance,
            result.thickness,
            result.sampleCount);
        return result;
    }
}
