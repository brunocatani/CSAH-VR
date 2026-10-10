#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"

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

    class TemporaryIni final
    {
    public:
        TemporaryIni() :
            path_(std::filesystem::temp_directory_path() /
                ("CSAH-VolumetricLighting-" +
                    std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count()) +
                    ".ini"))
        {}

        ~TemporaryIni()
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        void write(const char* source) const
        {
            std::ofstream output(path_, std::ios::binary | std::ios::trunc);
            output << source;
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };
}

int main()
{
    using csah::volumetric_lighting::Settings;
    using csah::volumetric_lighting::loadSettings;
    using csah::volumetric_lighting::saveSettings;

    TemporaryIni ini;
    const auto defaults = loadSettings(ini.path());
    require(defaults == Settings{}, "missing-file defaults");
    require(!defaults.enabled, "missing INI must keep volumetric lighting off");
    require(defaults.baseScattering == 0.0f, "safe base default");
    require(defaults.densityContribution == 0.5f, "qualified density default");
    require(defaults.windSpeed == 0.0f, "stable wind default");
    require(defaults.maxDistance == 3000.0f, "qualified distance default");

    ini.write("[CommunityShaders]\nbEnabled=1\n");
    require(!loadSettings(ini.path()).enabled,
        "missing volumetric section must default to off");
    ini.write("[VolumetricLighting]\niQuality=1\n");
    require(!loadSettings(ini.path()).enabled,
        "missing volumetric enable key must default to off");
    ini.write("[VolumetricLighting]\nbEnabled=invalid\n");
    require(!loadSettings(ini.path()).enabled,
        "invalid volumetric enable value must fail closed");

    ini.write(
        "[CommunityShaders]\n"
        "bEnabled=0\n"
        "[VolumetricLighting]\n"
        "bEnabled=1\n");
    require(
        !loadSettings(ini.path()).enabled,
        "visual-suite master gate");

    ini.write(
        "[CommunityShaders]\n"
        "bEnabled=1\n"
        "[VolumetricLighting]\n"
        "bEnabled=off\n"
        "iQuality=1\n"
        "fIntensity=1.75\n"
        "fBaseScattering=0.12\n"
        "fShaftIntensity=2.25\n"
        "fDensityContribution=0.7\n"
        "fDensityScale=2\n"
        "fWindSpeed=14\n"
        "fMaxDistance=8192\n"
        "fTemporalWeight=0.8\n");
    const auto loaded = loadSettings(ini.path());
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

    const Settings outOfRange{
        .enabled = true,
        .quality = 99,
        .intensity = 99.0f,
        .baseScattering = -1.0f,
        .shaftIntensity = 99.0f,
        .densityContribution = 99.0f,
        .densityScale = 0.0f,
        .windSpeed = -1.0f,
        .maxDistance = 1.0f,
        .temporalWeight = 2.0f,
    };
    require(saveSettings(ini.path(), outOfRange), "save sanitized settings");
    const auto sanitized = loadSettings(ini.path());
    require(sanitized.quality == 2, "quality clamp");
    require(sanitized.intensity == 4.0f, "intensity clamp");
    require(sanitized.baseScattering == 0.0f, "base clamp");
    require(sanitized.shaftIntensity == 4.0f, "shaft clamp");
    require(sanitized.densityContribution == 1.0f, "density mix clamp");
    require(sanitized.densityScale == 0.125f, "density scale clamp");
    require(sanitized.windSpeed == 0.0f, "wind clamp");
    require(sanitized.maxDistance == 256.0f, "distance clamp");
    require(sanitized.temporalWeight == 0.98f, "temporal clamp");

    const Settings roundTrip{
        .enabled = true,
        .quality = 0,
        .intensity = 0.9f,
        .baseScattering = 0.04f,
        .shaftIntensity = 1.8f,
        .densityContribution = 0.35f,
        .densityScale = 1.5f,
        .windSpeed = 3.0f,
        .maxDistance = 4096.0f,
        .temporalWeight = 0.75f,
        .heightFog=true, .heightFogDensity=1.5f, .heightFogHeight=128, .heightFogFalloff=512, .heightFogStart=256, .heightFogOpacity=.75f, .heightFogWeather=.5f, .heightFogVolume=.25f, .heightFogInteriorScale=.6f,
    };
    require(saveSettings(ini.path(), roundTrip), "save round trip");
    require(loadSettings(ini.path()) == roundTrip, "round trip");

    require(WritePrivateProfileStringW(L"CommunityShaders",L"bEnabled",L"0",ini.path().c_str())!=FALSE,"master off");
    const auto off=loadSettings(ini.path());require(!off.enabled && !off.heightFog,"master gate must disable both fog and shafts");
    std::cout << "Volumetric Lighting settings tests passed.\n";
    return EXIT_SUCCESS;
}
