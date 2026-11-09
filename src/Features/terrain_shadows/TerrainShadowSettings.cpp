#include "Features/terrain_shadows/TerrainShadowSettings.h"
#include "settings/MasterSettings.h"
#include <algorithm>
namespace csah::terrain_shadows
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        Settings s;
        s.enabled = values.boolean("TerrainShadows", "Enabled", false);
        s.strength = std::clamp(values.number("TerrainShadows", "Strength", s.strength), 0.0f, 1.0f);
        return master_settings::gate(values, s);
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
