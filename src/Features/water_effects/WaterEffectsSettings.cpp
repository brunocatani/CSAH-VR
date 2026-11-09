#include "Features/water_effects/WaterEffectsSettings.h"
#include "settings/MasterSettings.h"
namespace csah::water_effects
{
    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        constexpr std::string_view kSection = "WaterEffects";
        Settings settings;
        settings.enabled = values.boolean(kSection, "Enabled", settings.enabled);
        settings.parallax = values.boolean(kSection, "EnableParallax", settings.parallax);
        settings.caustics = values.boolean(kSection, "EnableCaustics", settings.caustics);
        settings.reflections = values.boolean(kSection, "EnableReflections", settings.reflections);
        settings.parallaxScale = values.number(kSection, "ParallaxScale", settings.parallaxScale);
        settings.causticStrength = values.number(kSection, "CausticStrength", settings.causticStrength);
        settings.reflectionStrength = values.number(kSection, "ReflectionStrength", settings.reflectionStrength);
        settings.reflectionDistance = values.number(kSection, "ReflectionDistance", settings.reflectionDistance);
        settings.screenReflections = values.boolean(kSection, "EnableScreenSpaceReflections", settings.screenReflections);
        settings.reflectionFlatness = values.number(kSection, "ReflectionFlatness", settings.reflectionFlatness);
        settings.reflectionRoughness = values.number(kSection, "ReflectionRoughness", settings.reflectionRoughness);
        return master_settings::gate(values, sanitize(settings));
    }
    Settings loadSettings() noexcept { return loadSettings(settings_file::File::get().current()); }
}
