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
        bool reflections{true};
        float parallaxScale{1};
        float causticStrength{1};
        float reflectionStrength{1};
        float reflectionDistance{1024};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings s) noexcept
    {
        s.parallaxScale=std::isfinite(s.parallaxScale)?std::clamp(s.parallaxScale,0.0f,2.0f):1;
        s.causticStrength=std::isfinite(s.causticStrength)?std::clamp(s.causticStrength,0.0f,1.0f):1;
        s.reflectionStrength=std::isfinite(s.reflectionStrength)?std::clamp(s.reflectionStrength,0.0f,1.0f):1;
        s.reflectionDistance=std::isfinite(s.reflectionDistance)?std::clamp(s.reflectionDistance,64.0f,16384.0f):1024;return s;
    }
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
