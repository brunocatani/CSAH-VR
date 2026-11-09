#include "Features/inverse_square/InverseSquareSettings.h"
#include "settings/MasterSettings.h"
#include <algorithm>
namespace csah::inverse_square
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        constexpr std::string_view kSection = "InverseSquareLighting";
        Settings s;
        s.enabled = values.boolean(kSection, "Enabled", false);
        s.size = std::clamp(values.number(kSection, "SourceSize", s.size), .25f, 16.0f);
        s.intensity = std::clamp(values.number(kSection, "Intensity", s.intensity), 0.0f, 8.0f);
        return master_settings::gate(values, s);
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
