#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "SettingsFixture.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Contact Shadows settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    using csah::contact_shadows::loadSettings;

    SettingsFixture ini("ContactShadowSettingsTests");
    ini.write(
        "[ContactShadows]\n"
        "Enabled = false\n"
        "Foveated = false\n"
        "Strength = 0.62\n"
        "MaxDistance = 72.0\n"
        "FadeDistance = 1536.0\n"
        "Thickness = 0.009\n"
        "SampleCount = 6\n");
    const auto configured = loadSettings(ini.values());
    require(!configured.enabled, "disabled owned key");
    require(!configured.foveated, "disabled foveation key");
    require(
        std::abs(configured.strength - 0.62f) < 1.0e-6f,
        "strength key");
    require(configured.maxDistance == 72.0f, "ray-distance key");
    require(configured.fadeDistance == 1536.0f, "fade-distance key");
    require(
        std::abs(configured.thickness - 0.009f) < 1.0e-6f,
        "thickness key");
    require(configured.sampleCount == 6, "sample-count key");

    ini.write(
        "[ContactShadows]\n"
        "Strength = 9.0\n"
        "MaxDistance = 1.0\n"
        "FadeDistance = 99999.0\n"
        "Thickness = 1.0\n"
        "SampleCount = 0\n");
    const auto clamped = loadSettings(ini.values());
    require(clamped.strength == 1.0f, "strength upper clamp");
    require(clamped.maxDistance == 8.0f, "ray-distance lower clamp");
    require(clamped.fadeDistance == 8192.0f, "fade-distance upper clamp");
    require(clamped.thickness == 0.05f, "thickness upper clamp");
    require(clamped.sampleCount == 2, "sample-count lower clamp");

    ini.write("[ContactShadows]\nSampleCount = 99\n");
    require(
        loadSettings(ini.values()).sampleCount == 8,
        "sample-count upper clamp");

    ini.write("[ContactShadows]\nEnabled = true\n[LinearLighting]\nEnabled = false\n");
    require(loadSettings(ini.values()).enabled, "independent settings table");

    std::cout << "Contact Shadows settings tests passed.\n";
    return EXIT_SUCCESS;
}
