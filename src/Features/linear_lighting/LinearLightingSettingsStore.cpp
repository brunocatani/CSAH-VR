#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <limits>
#include <string_view>

namespace csah::linear_lighting
{
    namespace
    {
        constexpr std::string_view kSection = "LinearLighting";

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view key,
            float fallback) noexcept
        {
            return values.number(kSection, key, fallback);
        }

        [[nodiscard]] bool readBool(
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
        auto result = defaults;
        result.enabled = readBool(values, "Enabled", defaults.enabled);
        result.preserveNativeDarkness = readBool(values,
            "PreserveNativeDarkness",
            defaults.preserveNativeDarkness);
        result.lightGamma = readFloat(values, "LightGamma", defaults.lightGamma);
        result.colorGamma = readFloat(values, "ColorGamma", defaults.colorGamma);
        result.emitColorGamma = readFloat(values, "EmitColorGamma", defaults.emitColorGamma);
        result.glowmapGamma = readFloat(values, "GlowmapGamma", defaults.glowmapGamma);
        result.ambientGamma = readFloat(values, "AmbientGamma", defaults.ambientGamma);
        result.fogGamma = readFloat(values, "FogGamma", defaults.fogGamma);
        result.fogAlphaGamma = readFloat(values, "FogAlphaGamma", defaults.fogAlphaGamma);
        result.effectGamma = readFloat(values, "EffectGamma", defaults.effectGamma);
        result.effectAlphaGamma = readFloat(values, "EffectAlphaGamma", defaults.effectAlphaGamma);
        result.skyGamma = readFloat(values, "SkyGamma", defaults.skyGamma);
        result.waterGamma = readFloat(values, "WaterGamma", defaults.waterGamma);
        result.volumetricLightingGamma = readFloat(values,
            "VolumetricLightingGamma",
            defaults.volumetricLightingGamma);
        result.vanillaDiffuseColorMultiplier = readFloat(values,
            "VanillaDiffuseColorMultiplier",
            defaults.vanillaDiffuseColorMultiplier);
        result.directionalLightMultiplier = readFloat(values,
            "DirectionalLightMultiplier",
            defaults.directionalLightMultiplier);
        result.pointLightMultiplier = readFloat(values,
            "PointLightMultiplier",
            defaults.pointLightMultiplier);
        result.ambientMultiplier = readFloat(values,
            "AmbientMultiplier",
            defaults.ambientMultiplier);
        result.emitColorMultiplier = readFloat(values,
            "EmitColorMultiplier",
            defaults.emitColorMultiplier);
        result.glowmapMultiplier = readFloat(values,
            "GlowmapMultiplier",
            defaults.glowmapMultiplier);
        result.effectLightingMultiplier = readFloat(values,
            "EffectLightingMultiplier",
            defaults.effectLightingMultiplier);
        result.membraneEffectMultiplier = readFloat(values,
            "MembraneEffectMultiplier",
            defaults.membraneEffectMultiplier);
        result.otherEffectMultiplier = readFloat(values,
            "OtherEffectMultiplier",
            defaults.otherEffectMultiplier);

        result = sanitize(result);
        return master_settings::gate(values, result);
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Linear Lighting settings loaded; enabled={}.",
            result.enabled);
        return result;
    }
}
