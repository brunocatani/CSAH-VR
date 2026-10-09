#pragma once
#include <algorithm>
#include <cmath>

namespace csah::lod_blending
{
    struct Settings
    {
        bool enabled{true};
        float terrainBrightness{1.0f};
        float terrainGamma{1.0f};
        float objectBrightness{1.0f};
        float objectGamma{1.0f};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] inline Settings sanitize(Settings value) noexcept
    {
        const auto clamp = [](float v, float minimum, float maximum) {
            return std::isfinite(v) ? std::clamp(v, minimum, maximum) : 1.0f;
        };
        value.terrainBrightness=clamp(value.terrainBrightness,0.01f,5.0f);
        value.objectBrightness=clamp(value.objectBrightness,0.01f,5.0f);
        value.terrainGamma=clamp(value.terrainGamma,0.1f,3.0f);
        value.objectGamma=clamp(value.objectGamma,0.1f,3.0f);
        return value;
    }
    [[nodiscard]] inline bool requested(const Settings& value) noexcept
    {
        return value.enabled && (value.terrainBrightness!=1.0f || value.terrainGamma!=1.0f ||
            value.objectBrightness!=1.0f || value.objectGamma!=1.0f);
    }
}
