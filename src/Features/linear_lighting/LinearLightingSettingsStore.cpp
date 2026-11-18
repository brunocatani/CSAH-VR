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
        result.lightGamma = readFloat(values, "LightResponse", defaults.lightGamma);
        result.colorGamma = readFloat(values, "ColorResponse", defaults.colorGamma);
        result.emitColorGamma = readFloat(values, "EmitColorResponse", defaults.emitColorGamma);
        result.glowmapGamma = readFloat(values, "GlowmapResponse", defaults.glowmapGamma);
        result.ambientGamma = readFloat(values, "AmbientResponse", defaults.ambientGamma);
        result.fogGamma = readFloat(values, "FogResponse", defaults.fogGamma);
        result.fogAlphaGamma = readFloat(values, "FogAlphaResponse", defaults.fogAlphaGamma);
        result.effectGamma = readFloat(values, "EffectResponse", defaults.effectGamma);
        result.effectAlphaGamma = readFloat(values, "EffectAlphaResponse", defaults.effectAlphaGamma);
        result.skyGamma = readFloat(values, "SkyResponse", defaults.skyGamma);
        result.waterGamma = readFloat(values, "WaterResponse", defaults.waterGamma);
        result.volumetricLightingGamma = readFloat(values,
            "VolumetricLightingResponse",
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
            "GlowmapScale",
            defaults.glowmapMultiplier);
        result.effectLightingMultiplier = readFloat(values,
            "EffectLightingScale",
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
