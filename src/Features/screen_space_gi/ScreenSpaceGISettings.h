#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
namespace csah::screen_space_gi
{
    struct Settings
    {
        bool enabled{false};
        bool indirectLight{true};
        bool specular{false};
        bool temporal{true};
        float radius{256.0f};
        float thickness{32.0f};
        float aoPower{2.0f};
        float strength{1.0f};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings s) noexcept
    {
        const auto finite=[](float v,float fallback,float low,float high) {return std::isfinite(v)?std::clamp(v,low,high):fallback;};
        s.radius=finite(s.radius,256,16,1024);s.thickness=finite(s.thickness,32,1,256);
        s.aoPower=finite(s.aoPower,2,0,4);s.strength=finite(s.strength,1,0,4);
        return s;
    }
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
