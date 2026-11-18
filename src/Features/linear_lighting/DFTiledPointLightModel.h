#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"

namespace csah::linear_lighting
{
    constexpr float kVanillaPointLightGamma = kNativeLightingResponseGamma;

    struct DFTiledPointLightProducerState
    {
        bool enabled{};
        float gamma{ kVanillaPointLightGamma };
        float colorMultiplier{ 1.0f };
    };

    [[nodiscard]] inline DFTiledPointLightProducerState
    makeDFTiledPointLightProducerState(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        if (!safe.enabled) {
            return {};
        }
        return {
            .enabled = true,
            .gamma = safe.lightGamma,
            // FO4VR's tiled-light compute shaders consume record color at
            // engine scale and contain no reciprocal-PI normalization.
            .colorMultiplier = safe.pointLightMultiplier,
        };
    }
}
