#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace csah::water_effects
{
    struct Settings
    {
        bool enabled{false};
        bool parallax{true};
        bool caustics{true};
        // Water cubemaps are an explicit opt-in, independent of global cubemaps and FX reflections.
        bool reflections{false};
        float parallaxScale{1};
        float causticStrength{1};
        float reflectionStrength{1};
        float reflectionDistance{1024};
        bool screenReflections{};
        float reflectionFlatness{};
        float reflectionRoughness{};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings s) noexcept
    {
        s.parallaxScale=std::isfinite(s.parallaxScale)?std::clamp(s.parallaxScale,0.0f,2.0f):1;
        s.causticStrength=std::isfinite(s.causticStrength)?std::clamp(s.causticStrength,0.0f,1.0f):1;
        s.reflectionStrength=std::isfinite(s.reflectionStrength)?std::clamp(s.reflectionStrength,0.0f,1.0f):1;
        s.reflectionDistance=std::isfinite(s.reflectionDistance)?std::clamp(s.reflectionDistance,64.0f,16384.0f):1024;
        s.reflectionFlatness=std::isfinite(s.reflectionFlatness)?std::clamp(s.reflectionFlatness,0.0f,1.0f):0;
        s.reflectionRoughness=std::isfinite(s.reflectionRoughness)?std::clamp(s.reflectionRoughness,0.0f,1.0f):0;return s;
    }
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
