#pragma once

#include "Features/color_grading/ColorGradingSettings.h"

#include <algorithm>
#include <cmath>

namespace csah::filmic_tonemapping
{
    struct Settings final
    {
        bool enabled{ true };
        bool useNativeAutoExposure{ true };
        float exposureCompensationEV{};
        float filmicStrength{ 1.0f };
        float whitePointScale{ 1.0f };
        // Colour grading shares this output shader and has its own settings
        // table and switch; it works with Filmic Tonemapping on or off.
        color_grading::Settings grading{};

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        auto result = settings;
        result.exposureCompensationEV =
            std::isfinite(result.exposureCompensationEV) ?
            std::clamp(result.exposureCompensationEV, -3.0f, 3.0f) : 0.0f;
        result.filmicStrength = std::isfinite(result.filmicStrength) ?
            std::clamp(result.filmicStrength, 0.0f, 1.0f) : 1.0f;
        result.whitePointScale = std::isfinite(result.whitePointScale) ?
            std::clamp(result.whitePointScale, 0.5f, 2.0f) : 1.0f;
        result.grading = color_grading::sanitize(result.grading);
        return result;
    }
}
