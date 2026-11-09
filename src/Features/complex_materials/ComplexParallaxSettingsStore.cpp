#include "Features/complex_materials/ComplexParallaxSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string>
#include <string_view>

namespace csah::complex_materials
{
    namespace
    {
        constexpr std::string_view kSection = "ComplexMaterials";

        [[nodiscard]] bool readBool(
            const settings_file::Values& values,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(kSection, key, fallback);
        }

        [[nodiscard]] std::int32_t readInt(
            const settings_file::Values& values,
            std::string_view key,
            std::int32_t fallback) noexcept
        {
            return values.integer(kSection, key, fallback);
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
        auto result = defaults;
        result.environmentResponseEnabled = readBool(values,
            "EnableEnvironmentResponse",
            defaults.environmentResponseEnabled);
        result.parallaxEnabled = readBool(values, "EnableParallax", defaults.parallaxEnabled);
        result.parallaxQuality = readInt(values, "ParallaxQuality", defaults.parallaxQuality);
        result.parallaxDepth = readFloat(values, "ParallaxDepth", defaults.parallaxDepth);
        result.grazingClamp = readFloat(values, "ParallaxGrazingClamp", defaults.grazingClamp);
        result.fadeStart = readFloat(values, "ParallaxFadeStart", defaults.fadeStart);
        result.fadeEnd = readFloat(values, "ParallaxFadeEnd", defaults.fadeEnd);
        result = sanitize(result);
        if (!master_settings::enabled(values)) {
            result.environmentResponseEnabled = false;
            result.parallaxEnabled = false;
        }
        return result;
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Complex Materials settings loaded; environment response enabled={}, parallax enabled={}, quality={}.",
            result.environmentResponseEnabled,
            result.parallaxEnabled,
            result.parallaxQuality);
        return result;
    }
}
