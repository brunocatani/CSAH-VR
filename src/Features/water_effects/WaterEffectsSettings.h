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
        float parallaxScale{1};
        float causticStrength{1};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings s) noexcept
    {
        s.parallaxScale=std::isfinite(s.parallaxScale)?std::clamp(s.parallaxScale,0.0f,2.0f):1;
        s.causticStrength=std::isfinite(s.causticStrength)?std::clamp(s.causticStrength,0.0f,1.0f):1;return s;
    }
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
