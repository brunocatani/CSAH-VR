#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace csah::color_grading
{
    // Colour grading of the finished image. It runs inside the output-stage
    // shader Filmic Tonemapping already draws, so it adds no pass.
    struct Settings final
    {
        bool enabled{ true };
        float tableStrength{ 1.0f };
        float brightness{ 1.0f };
        float contrast{ 1.0f };
        float saturation{ 1.0f };
        float gamma{ 1.0f };
        float blackPoint{};
        float whitePoint{ 1.0f };
        float desaturateShadows{};
        std::array<float, 3> shadowTint{};
        std::array<float, 3> highlightTint{};

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        const auto bounded = [](float value, float low, float high, float fallback) {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        };
        auto result = settings;
        result.tableStrength = bounded(result.tableStrength, 0.0f, 1.0f, 1.0f);
        result.brightness = bounded(result.brightness, 0.0f, 2.0f, 1.0f);
        result.contrast = bounded(result.contrast, 0.0f, 2.0f, 1.0f);
        result.saturation = bounded(result.saturation, 0.0f, 2.0f, 1.0f);
        result.gamma = bounded(result.gamma, 0.5f, 2.0f, 1.0f);
        result.blackPoint = bounded(result.blackPoint, 0.0f, 0.25f, 0.0f);
        result.whitePoint = bounded(result.whitePoint, 0.5f, 1.0f, 1.0f);
        result.desaturateShadows =
            bounded(result.desaturateShadows, 0.0f, 1.0f, 0.0f);
        for (auto& value : result.shadowTint) {
            value = bounded(value, -0.25f, 0.25f, 0.0f);
        }
        for (auto& value : result.highlightTint) {
            value = bounded(value, -0.25f, 0.25f, 0.0f);
        }
        return result;
    }

    // True when the controls leave every colour as it is, so only a colour
    // table can still change the image.
    [[nodiscard]] inline bool controlsNeutral(const Settings& settings) noexcept
    {
        auto neutral = Settings{};
        neutral.enabled = settings.enabled;
        neutral.tableStrength = settings.tableStrength;
        return settings == neutral;
    }
}
