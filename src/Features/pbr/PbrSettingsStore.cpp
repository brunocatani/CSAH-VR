#include "Features/pbr/PbrSettingsStore.h"
#include "settings/SettingsFile.h"

#include "settings/MasterSettings.h"
#include "support/Logger.h"

#include <cmath>
#include <string_view>

namespace csah::pbr
{
    namespace
    {
        constexpr std::string_view kSection = "PBR";

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
            .legacyMaterials = readBoolean(values, "LegacyMaterials", defaults.legacyMaterials),
            .directGgx = readBoolean(values, "DirectGGX", defaults.directGgx),
            .grassGgx = readBoolean(values, "GrassGGX", defaults.grassGgx),
            .environmentFresnel = readBoolean(values,
                "EnvironmentFresnel",
                defaults.environmentFresnel),
            .energyConservation = readBoolean(values,
                "EnergyConservation",
                defaults.energyConservation),
            .multiscatterCompensation = readBoolean(values,
                "MultiscatterCompensation",
                defaults.multiscatterCompensation),
            .specularOcclusion = readBoolean(values,
                "SpecularOcclusion",
                defaults.specularOcclusion),
            .roughnessMultiplier = readFloat(values,
                "RoughnessMultiplier",
                defaults.roughnessMultiplier),
            .specularRoughnessBlend = readFloat(values,
                "SpecularRoughnessBlend",
                defaults.specularRoughnessBlend),
            .baseF0Multiplier = readFloat(values, "BaseF0Multiplier", defaults.baseF0Multiplier),
            .minimumF0 = readFloat(values, "MinimumF0", defaults.minimumF0),
            .cubemapToF0Multiplier = readFloat(values,
                "CubemapToF0Multiplier",
                defaults.cubemapToF0Multiplier),
            .complexMaterialF0Multiplier = readFloat(values,
                "ComplexMaterialF0Multiplier",
                defaults.complexMaterialF0Multiplier),
            .directLightingScale = readFloat(values,
                "DirectLightingScale",
                defaults.directLightingScale),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "PBR settings loaded; enabled={}, legacy={}, directGGX={}, environmentFresnel={}, roughnessMultiplier={}, baseF0Multiplier={}.",
            result.enabled,
            result.legacyMaterials,
            result.directGgx,
            result.environmentFresnel,
            result.roughnessMultiplier,
            result.baseF0Multiplier);
        return result;
    }
}
