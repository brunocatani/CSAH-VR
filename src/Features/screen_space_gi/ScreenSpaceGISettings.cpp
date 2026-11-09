#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "settings/MasterSettings.h"
namespace csah::screen_space_gi
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        constexpr std::string_view kSection = "ScreenSpaceGI";
        Settings s;
        s.enabled = values.boolean(kSection, "Enabled", s.enabled);
        s.indirectLight = values.boolean(kSection, "IndirectLight", s.indirectLight);
        s.specular = values.boolean(kSection, "ExperimentalSpecular", s.specular);
        s.temporal = values.boolean(kSection, "Temporal", s.temporal);
        s.radius = values.number(kSection, "Radius", s.radius);
        s.thickness = values.number(kSection, "Thickness", s.thickness);
        s.aoPower = values.number(kSection, "AOPower", s.aoPower);
        s.strength = values.number(kSection, "GIStrength", s.strength);
        return master_settings::gate(values, sanitize(s));
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
