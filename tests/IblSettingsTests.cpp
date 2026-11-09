#include "Features/ibl/IblSettingsStore.h"
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
            std::cerr << "IBL settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    using csah::ibl::loadSettings;

    SettingsFixture ini("IblSettingsTests");
    ini.write(
        "[ImageBasedLighting]\n"
        "Enabled = false\n"
        "DiffuseEnabled = false\n"
        "DiffuseLevel = 1.35\n"
        "[DynamicCubemaps]\n"
        "Enabled = false\n");
    const auto configured = loadSettings(ini.values());
    require(!configured.enabled, "disabled owned key");
    require(
        !configured.dynamicCubemapsEnabled,
        "disabled Dynamic Cubemaps key");
    require(!configured.diffuseEnabled, "disabled diffuse key");
    require(
        std::abs(configured.diffuseLevel - 1.35f) < 1.0e-6f,
        "diffuse level key");
    ini.write("[ImageBasedLighting]\nEnabled = true\n");
    require(loadSettings(ini.values()).enabled, "enabled owned key");
    ini.write("[ImageBasedLighting]\nEnabled = true\n[LinearLighting]\nEnabled = false\n");
    require(loadSettings(ini.values()).enabled, "independent settings table");

    ini.write(
        "[ImageBasedLighting]\n"
        "DiffuseLevel = 99.0\n");
    require(
        loadSettings(ini.values()).diffuseLevel == 2.0f,
        "diffuse level upper clamp");

    std::cout << "IBL settings tests passed.\n";
    return EXIT_SUCCESS;
}
