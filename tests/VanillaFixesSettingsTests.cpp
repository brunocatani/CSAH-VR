#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "SettingsFixture.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Vanilla Fixes settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    using csah::vanilla_fixes::Settings;
    using csah::vanilla_fixes::DirectionalLightDiagnosticMode;
    using csah::vanilla_fixes::loadSettings;

    SettingsFixture ini("VanillaFixesSettingsTests");
    ini.write(
        "[CommunityShaders]\n"
        "Enabled = false\n"
        "[VanillaFixes]\n"
        "Enabled = true\n");
    require(
        loadSettings(ini.values()).enabled,
        "Community Shaders visual gate disabled independent Vanilla Fixes");

    ini.write(
        "[VanillaFixes]\n"
        "Enabled = false\n"
        "PrecipitationOcclusion = false\n"
        "AllowImageSpaceModifiers = false\n"
        "VrAllowSAO = false\n"
        "VrAllowScreenSpaceReflections = false\n"
        "NativeScreenSpaceMaterialPipeline = false\n"
        "LensFlareVr = false\n"
        "VrAllowFocusShadows = false\n"
        "VrSunOcclusion = false\n");
    const auto disabled = loadSettings(ini.values());
    require(!disabled.enabled, "master key");
    require(!disabled.precipitationOcclusion, "precipitation key");
    require(!disabled.imageSpaceModifiers, "image-space key");
    require(!disabled.sao, "SAO key");
    require(!disabled.screenSpaceReflections, "SSLR key");
    require(
        !disabled.nativeScreenSpaceMaterialPipeline,
        "native screen-space material pipeline key");
    require(!disabled.lensFlare, "lens-flare key");
    require(!disabled.focusShadows, "focus-shadow key");
    require(!disabled.stereoSunOcclusion, "stereo Sun-occlusion key");

    ini.write(
        "[VanillaFixes]\n"
        "Enabled = true\n"
        "[Diagnostics]\n"
        "DirectionalLightingMode = 15\n");
    require(
        loadSettings(ini.values()).directionalLightDiagnosticMode ==
            DirectionalLightDiagnosticMode::cubemapLookupOnly,
        "cubemap diagnostic key");

    ini.write(
        "[VanillaFixes]\n"
        "Enabled = true\n"
        "[Diagnostics]\n"
        "DirectionalLightingMode = 16\n");
    require(
        loadSettings(ini.values()).directionalLightDiagnosticMode ==
            DirectionalLightDiagnosticMode::off,
        "out-of-range diagnostic fallback");

    ini.write(
        "[VanillaFixes]\n"
        "Enabled = true\n"
        "PrecipitationOcclusion = false\n"
        "AllowImageSpaceModifiers = true\n"
        "VrAllowSAO = false\n"
        "VrAllowScreenSpaceReflections = true\n"
        "NativeScreenSpaceMaterialPipeline = false\n"
        "LensFlareVr = true\n"
        "VrAllowFocusShadows = false\n"
        "VrSunOcclusion = false\n"
        "[Diagnostics]\n"
        "DirectionalLightingMode = 13\n");
    const Settings mixed{
        .enabled = true,
        .precipitationOcclusion = false,
        .imageSpaceModifiers = true,
        .sao = false,
        .screenSpaceReflections = true,
        .nativeScreenSpaceMaterialPipeline = false,
        .lensFlare = true,
        .focusShadows = false,
        .stereoSunOcclusion = false,
        .directionalLightDiagnosticMode =
            DirectionalLightDiagnosticMode::directSpecularOnly,
    };
    require(loadSettings(ini.values()) == mixed, "every Vanilla Fixes key");

    std::cout << "Vanilla Fixes settings tests passed.\n";
    return EXIT_SUCCESS;
}
