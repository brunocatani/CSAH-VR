#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "SettingsFixture.h"
#include "Features/native_shadows/NativeShadowPatchModel.h"

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
            std::cerr << "Native Shadows settings test failed: " << message
                      << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

}

int main()
{
    // Model the independently verified native selector: the unsafe branch
    // selects masks with bits 2/3 when renderer range exceeds cascade range.
    for(float initial : {3000.0f,12000.0f}) for(float requested : {3000.0f,12000.0f}) {
        for(int failAt : {-1,0,1,2}) {
            float renderer=initial, cascade=initial;
            int calls=0;
            const bool applied=csah::native_shadows::patch_model::writeFixedDistances(requested,renderer,
                [&](std::uintptr_t rva,float value) {
                    if(calls++==failAt) return false;
                    if(rva==csah::native_shadows::patch_model::kCascadeDistanceRva) cascade=value;
                    else {require(rva==csah::native_shadows::patch_model::kRendererDistanceRva,"unknown distance target");renderer=value;}
                    const unsigned nativeMask=cascade<renderer?15u:3u;
                    require((nativeMask & ~3u)==0,"distance update selected unallocated cascades");
                    return true;
                });
            if(applied) require(renderer==requested && cascade==requested,"far shadow range differs between culling and split setup");
            else require(cascade>=renderer,"failed distance write left an unsafe native mask");
        }
    }

    using csah::native_shadows::Settings;
    using csah::native_shadows::loadSettings;

    SettingsFixture ini("NativeShadowSettingsTests");
    ini.write(
        "[CommunityShaders]\n"
        "Enabled = false\n"
        "[NativeShadows]\n"
        "Enabled = true\n");
    require(
        loadSettings(ini.values()).enabled,
        "Community Shaders visual gate disabled independent Native Shadows");

    ini.write(
        "[NativeShadows]\n"
        "Enabled = false\n"
        "ExtendedDirectionalCascades = false\n"
        "TiledDeferredLighting = false\n"
        "DirectionalShadowDistance = 24000.0\n"
        "CascadeBlendDistance = 48.0\n"
        "OrthographicShadowFilter = 4\n");
    const auto disabled = loadSettings(ini.values());
    require(!disabled.enabled, "master key");
    require(
        !disabled.extendedDirectionalCascades,
        "extended-cascade key");
    require(!disabled.tiledDeferredLighting, "tiled-lighting key");
    require(
        disabled.directionalShadowDistance == 24000.0f,
        "fixed-distance key");
    require(
        disabled.cascadeBlendDistance == 48.0f,
        "cascade-blend key");
    require(
        disabled.orthographicShadowFilter == 4,
        "orthographic-filter key");

    ini.write(
        "[NativeShadows]\n"
        "DirectionalShadowDistance = 100.0\n"
        "CascadeBlendDistance = -20.0\n");
    require(
        loadSettings(ini.values()).directionalShadowDistance == 3000.0f,
        "minimum clamp");
    require(
        loadSettings(ini.values()).cascadeBlendDistance == 0.0f,
        "cascade-blend minimum clamp");

    ini.write(
        "[NativeShadows]\n"
        "DirectionalShadowDistance = 90000.0\n"
        "CascadeBlendDistance = 9000.0\n"
        "OrthographicShadowFilter = 99\n");
    require(
        loadSettings(ini.values()).directionalShadowDistance == 50000.0f,
        "maximum clamp");
    require(
        loadSettings(ini.values()).cascadeBlendDistance == 5000.0f,
        "cascade-blend maximum clamp");
    require(
        loadSettings(ini.values()).orthographicShadowFilter == 5,
        "orthographic-filter maximum clamp");

    std::cout << "Native Shadows settings tests passed.\n";
    return EXIT_SUCCESS;
}
