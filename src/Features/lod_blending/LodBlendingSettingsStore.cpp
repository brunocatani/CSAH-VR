#include "Features/lod_blending/LodBlendingSettingsStore.h"
#include "settings/SettingsFile.h"
#include "settings/MasterSettings.h"

namespace csah::lod_blending
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        constexpr std::string_view kSection = "LODBlending";
        Settings value;
        value.enabled = values.boolean(kSection, "Enabled", value.enabled);
        value.terrainBrightness = values.number(kSection, "TerrainBrightness", 1.0f);
        value.terrainGamma = values.number(kSection, "TerrainGamma", 1.0f);
        value.objectBrightness = values.number(kSection, "ObjectBrightness", 1.0f);
        value.objectGamma = values.number(kSection, "ObjectGamma", 1.0f);
        return master_settings::gate(values, sanitize(value));
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
