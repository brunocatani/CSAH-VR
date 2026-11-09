#include "Features/pbr/PbrSettingsStore.h"
#include "SettingsFixture.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "PBR settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    using csah::pbr::loadSettings;

    SettingsFixture ini("PbrSettingsTests");
    ini.write(
        "[PBR]\n"
        "Enabled = false\n"
        "DirectGGX = false\n"
        "EnvironmentFresnel = false\n"
        "RoughnessMultiplier = 1.5\n"
        "BaseF0Multiplier = 0.5\n");
    const auto configured = loadSettings(ini.values());
    require(!configured.enabled, "disabled owned key");
    require(!configured.directGgx, "direct GGX key");
    require(!configured.environmentFresnel, "environment Fresnel key");
    require(std::fabs(configured.roughnessMultiplier - 1.5f) < 1.0e-6f,
        "roughness multiplier key");
    require(std::fabs(configured.baseF0Multiplier - 0.5f) < 1.0e-6f,
        "base F0 key");

    ini.write(
        "[CommunityShaders]\n"
        "Enabled = false\n"
        "[PBR]\n"
        "Enabled = true\n");
    require(!loadSettings(ini.values()).enabled,
        "Community Shaders master gate did not disable PBR");

    ini.write(
        "[PBR]\n"
        "RoughnessMultiplier = 99.0\n"
        "MinimumF0 = -1.0\n"
        "DirectLightingScale = 99.0\n");
    const auto clamped = loadSettings(ini.values());
    require(clamped.roughnessMultiplier == 4.0f,
        "roughness upper clamp");
    require(clamped.minimumF0 == 0.0f, "minimum F0 lower clamp");
    require(clamped.directLightingScale == 4.0f,
        "direct-light scale upper clamp");

    std::cout << "PBR settings tests passed.\n";
    return EXIT_SUCCESS;
}
