#include "Features/terrain_variation/TerrainVariationSettings.h"
#include "settings/MasterSettings.h"
namespace csah::terrain_variation
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        Settings settings;
        settings.enabled = values.boolean("TerrainVariation", "Enabled", settings.enabled);
        settings.lodEnabled = values.boolean("TerrainVariation", "EnableLOD", settings.lodEnabled);
        return master_settings::gate(values, settings);
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
