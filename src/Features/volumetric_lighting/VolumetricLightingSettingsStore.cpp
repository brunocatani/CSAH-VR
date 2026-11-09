#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"
#include "settings/SettingsFile.h"

#include "settings/MasterSettings.h"
#include "support/Logger.h"

#include <cmath>

namespace csah::volumetric_lighting
{
    namespace
    {
        constexpr std::string_view kSection = "VolumetricLighting";

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
        auto result = sanitize({
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .quality = readUnsigned(values, "Quality", defaults.quality),
            .intensity = readFloat(values, "Intensity", defaults.intensity),
            .baseScattering = readFloat(values, "BaseScattering", defaults.baseScattering),
            .shaftIntensity = readFloat(values, "ShaftIntensity", defaults.shaftIntensity),
            .densityContribution = readFloat(values, "DensityContribution", defaults.densityContribution),
            .densityScale = readFloat(values, "DensityScale", defaults.densityScale),
            .windSpeed = readFloat(values, "WindSpeed", defaults.windSpeed),
            .maxDistance = readFloat(values, "MaxDistance", defaults.maxDistance),
            .temporalWeight = readFloat(values, "TemporalWeight", defaults.temporalWeight),
            .heightFog = readBoolean(values,"HeightFogEnabled",defaults.heightFog),
            .heightFogDensity = readFloat(values,"HeightFogDensity",defaults.heightFogDensity),
            .heightFogHeight = readFloat(values,"HeightFogHeight",defaults.heightFogHeight),
            .heightFogFalloff = readFloat(values,"HeightFogFalloff",defaults.heightFogFalloff),
            .heightFogStart = readFloat(values,"HeightFogStart",defaults.heightFogStart),
            .heightFogOpacity = readFloat(values,"HeightFogOpacity",defaults.heightFogOpacity),
            .heightFogWeather = readFloat(values,"HeightFogWeather",defaults.heightFogWeather),
            .heightFogVolume = readFloat(values,"HeightFogVolume",defaults.heightFogVolume),
            .heightFogInteriorScale = readFloat(values,"HeightFogInteriorScale",defaults.heightFogInteriorScale),
        });
        if(!master_settings::enabled(values)) {result.enabled=false;result.heightFog=false;}
        return result;
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Volumetric Lighting settings loaded; enabled={}, quality={}, intensity={}, base={}, shafts={}, densityContribution={}, densityScale={}, windSpeed={}, maxDistance={}, temporalWeight={}.",
            result.enabled,
            result.quality,
            result.intensity,
            result.baseScattering,
            result.shaftIntensity,
            result.densityContribution,
            result.densityScale,
            result.windSpeed,
            result.maxDistance,
            result.temporalWeight);
        return result;
    }
}
