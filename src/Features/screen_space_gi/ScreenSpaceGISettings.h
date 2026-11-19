#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "settings/SettingsFile.h"
namespace csah::screen_space_gi
{
    // Defaults and ranges follow upstream Community Shaders FO4 ScreenSpaceGI;
    // the sample counts are the VR preset of the Skyrim feature it derives
    // from (3 directions of 6 samples).
    struct Settings
    {
        bool enabled{false};
        bool indirectLight{true};
        bool specular{false};
        bool temporal{true};
        bool blur{true};
        // Internal resolution per eye: 0 full, 1 half, 2 quarter.
        std::int32_t resolutionMode{1};
        std::int32_t slices{3};
        std::int32_t steps{6};
        // Fewer samples per direction on flat or distant surfaces.
        bool adaptiveSampling{true};
        // AO and bounce fade out before this distance from the camera and are
        // not computed beyond it; 0 leaves only the far fade below.
        float cullDistance{1500.0f};
        float minScreenRadius{0.01f};
        float aoRadius{256.0f};
        float giRadius{256.0f};
        float thickness{32.0f};
        float fadeStart{40000.0f};
        float fadeEnd{50000.0f};
        float saturation{0.8f};
        float distanceCompensation{0.0f};
        // Upstream FO4 calibration for interiors lit by placed lights.
        float aoPower{4.0f};
        float strength{1.0f};
        float depthDisocclusion{0.1f};
        std::int32_t maxAccumFrames{16};
        float blurRadius{2.0f};
        float distanceNormalisation{2.0f};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings s) noexcept
    {
        const auto finite=[](float v,float fallback,float low,float high) {return std::isfinite(v)?std::clamp(v,low,high):fallback;};
        s.resolutionMode=std::clamp(s.resolutionMode,0,2);
        s.slices=std::clamp(s.slices,1,10);s.steps=std::clamp(s.steps,1,20);
        s.minScreenRadius=finite(s.minScreenRadius,0.01f,0,0.05f);
        s.aoRadius=finite(s.aoRadius,256,10,1024);s.giRadius=finite(s.giRadius,256,10,1024);
        s.thickness=finite(s.thickness,32,0,256);
        s.cullDistance=finite(s.cullDistance,1500,0,20480);
        s.fadeStart=finite(s.fadeStart,40000,0,200000);s.fadeEnd=finite(s.fadeEnd,50000,0,200000);
        if(s.fadeEnd<s.fadeStart+1) s.fadeEnd=s.fadeStart+1;
        s.saturation=finite(s.saturation,0.8f,0,1);s.distanceCompensation=finite(s.distanceCompensation,0,-5,5);
        s.aoPower=finite(s.aoPower,4,0,12);s.strength=finite(s.strength,1,0,6);
        s.depthDisocclusion=finite(s.depthDisocclusion,0.1f,0,0.2f);
        s.maxAccumFrames=std::clamp(s.maxAccumFrames,1,64);
        s.blurRadius=finite(s.blurRadius,2,0,30);s.distanceNormalisation=finite(s.distanceNormalisation,2,0,5);
        return s;
    }
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
