#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"
#include "SettingsFixture.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Volumetric Lighting settings test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    using csah::volumetric_lighting::Settings;
    using csah::volumetric_lighting::loadSettings;

    SettingsFixture ini("VolumetricLightingSettingsTests");
    ini.write("[VolumetricLighting]\nEnabled = 'invalid'\n");
    require(!loadSettings(ini.values()).enabled,
        "invalid volumetric enable value must fail closed");

    ini.write(
        "[CommunityShaders]\n"
        "Enabled = false\n"
        "[VolumetricLighting]\n"
        "Enabled = true\n");
    require(
        !loadSettings(ini.values()).enabled,
        "visual-suite master gate");

    ini.write(
        "[CommunityShaders]\n"
        "Enabled = true\n"
        "[VolumetricLighting]\n"
        "Enabled = false\n"
        "Quality = 1\n"
        "Intensity = 1.75\n"
        "BaseScattering = 0.12\n"
        "ShaftIntensity = 2.25\n"
        "DensityContribution = 0.7\n"
        "DensityScale = 2.0\n"
        "WindSpeed = 14.0\n"
        "MaxDistance = 8192.0\n"
        "TemporalWeight = 0.8\n");
    const auto loaded = loadSettings(ini.values());
    require(!loaded.enabled, "feature gate");
    require(loaded.quality == 1, "quality");
    require(loaded.intensity == 1.75f, "intensity");
    require(loaded.baseScattering == 0.12f, "base scattering");
    require(loaded.shaftIntensity == 2.25f, "shaft intensity");
    require(loaded.densityContribution == 0.7f, "density contribution");
    require(loaded.densityScale == 2.0f, "density scale");
    require(loaded.windSpeed == 14.0f, "wind speed");
    require(loaded.maxDistance == 8192.0f, "maximum distance");
    require(loaded.temporalWeight == 0.8f, "temporal weight");

    ini.write(
        "[CommunityShaders]\n"
        "Enabled = true\n"
        "[VolumetricLighting]\n"
        "Enabled = true\n"
        "Quality = 99\n"
        "Intensity = 99.0\n"
        "BaseScattering = -1.0\n"
        "ShaftIntensity = 99.0\n"
        "DensityContribution = 99.0\n"
        "DensityScale = 0.0\n"
        "WindSpeed = -1.0\n"
        "MaxDistance = 1.0\n"
        "TemporalWeight = 2.0\n");
    const auto sanitized = loadSettings(ini.values());
    require(sanitized.quality == 2, "quality clamp");
    require(sanitized.intensity == 4.0f, "intensity clamp");
    require(sanitized.baseScattering == 0.0f, "base clamp");
    require(sanitized.shaftIntensity == 4.0f, "shaft clamp");
    require(sanitized.densityContribution == 1.0f, "density mix clamp");
    require(sanitized.densityScale == 0.125f, "density scale clamp");
    require(sanitized.windSpeed == 0.0f, "wind clamp");
    require(sanitized.maxDistance == 256.0f, "distance clamp");
    require(sanitized.temporalWeight == 0.98f, "temporal clamp");

    constexpr std::string_view kHeightFog =
        "[VolumetricLighting]\n"
        "Enabled = true\n"
        "HeightFogEnabled = true\n"
        "HeightFogDensity = 1.5\n"
        "HeightFogHeight = 128.0\n"
        "HeightFogFalloff = 512.0\n"
        "HeightFogStart = 256.0\n"
        "HeightFogOpacity = 0.75\n"
        "HeightFogWeather = 0.5\n"
        "HeightFogVolume = 0.25\n"
        "HeightFogInteriorScale = 0.6\n";
    ini.write(std::string("[CommunityShaders]\nEnabled = true\n").append(kHeightFog));
    const auto fog = loadSettings(ini.values());
    require(fog.enabled && fog.heightFog && fog.heightFogDensity == 1.5f && fog.heightFogHeight == 128.0f &&
            fog.heightFogFalloff == 512.0f && fog.heightFogStart == 256.0f && fog.heightFogOpacity == 0.75f &&
            fog.heightFogWeather == 0.5f && fog.heightFogVolume == 0.25f && fog.heightFogInteriorScale == 0.6f,
        "height fog keys");

    ini.write(std::string("[CommunityShaders]\nEnabled = false\n").append(kHeightFog));
    const auto off = loadSettings(ini.values());
    require(!off.enabled && !off.heightFog, "master gate must disable both fog and shafts");
    std::cout << "Volumetric Lighting settings tests passed.\n";
    return EXIT_SUCCESS;
}
