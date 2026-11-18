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
        s.blur = values.boolean(kSection, "Blur", s.blur);
        s.resolutionMode = values.integer(kSection, "ResolutionMode", s.resolutionMode);
        s.slices = values.integer(kSection, "NumSlices", s.slices);
        s.steps = values.integer(kSection, "NumSteps", s.steps);
        s.minScreenRadius = values.number(kSection, "MinScreenRadius", s.minScreenRadius);
        s.aoRadius = values.number(kSection, "AORadius", s.aoRadius);
        s.giRadius = values.number(kSection, "GIRadius", s.giRadius);
        s.thickness = values.number(kSection, "Thickness", s.thickness);
        s.fadeStart = values.number(kSection, "DepthFadeStart", s.fadeStart);
        s.fadeEnd = values.number(kSection, "DepthFadeEnd", s.fadeEnd);
        s.saturation = values.number(kSection, "GISaturation", s.saturation);
        s.distanceCompensation = values.number(kSection, "GIDistanceCompensation", s.distanceCompensation);
        s.aoPower = values.number(kSection, "AOPower", s.aoPower);
        s.strength = values.number(kSection, "GIStrength", s.strength);
        s.depthDisocclusion = values.number(kSection, "DepthDisocclusion", s.depthDisocclusion);
        s.maxAccumFrames = values.integer(kSection, "MaxAccumFrames", s.maxAccumFrames);
        s.blurRadius = values.number(kSection, "BlurRadius", s.blurRadius);
        s.distanceNormalisation = values.number(kSection, "DistanceNormalisation", s.distanceNormalisation);
        return master_settings::gate(values, sanitize(s));
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
