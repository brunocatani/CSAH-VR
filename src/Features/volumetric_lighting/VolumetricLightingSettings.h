#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace csah::volumetric_lighting
{
    struct Settings final
    {
        bool enabled{ false };
        std::uint32_t quality{ 2 };
        float intensity{ 1.0f };
        float baseScattering{};
        float shaftIntensity{ 1.35f };
        float densityContribution{ 0.50f };
        float densityScale{ 1.0f };
        float windSpeed{};
        float maxDistance{ 3000.0f };
        float temporalWeight{ 0.90f };

        bool heightFog{false};
        float heightFogDensity{0.5f};
        float heightFogHeight{0};
        float heightFogFalloff{1024};
        float heightFogStart{128};
        float heightFogOpacity{0.9f};
        float heightFogWeather{1};
        float heightFogVolume{0};

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings sanitize(const Settings& settings) noexcept
    {
        const Settings defaults{};
        auto result = settings;
        result.quality = std::clamp(result.quality, 0u, 2u);
        const auto finiteClamp = [](float value, float fallback, float minimum,
                                     float maximum) noexcept {
            return std::isfinite(value) ?
                std::clamp(value, minimum, maximum) : fallback;
        };
        result.intensity = finiteClamp(
            result.intensity, defaults.intensity, 0.0f, 4.0f);
        result.baseScattering = finiteClamp(
            result.baseScattering, defaults.baseScattering, 0.0f, 1.0f);
        result.shaftIntensity = finiteClamp(
            result.shaftIntensity, defaults.shaftIntensity, 0.0f, 4.0f);
        result.densityContribution = finiteClamp(
            result.densityContribution,
            defaults.densityContribution,
            0.0f,
            1.0f);
        result.densityScale = finiteClamp(
            result.densityScale, defaults.densityScale, 0.125f, 8.0f);
        result.windSpeed = finiteClamp(
            result.windSpeed, defaults.windSpeed, 0.0f, 100.0f);
        result.maxDistance = finiteClamp(
            result.maxDistance, defaults.maxDistance, 256.0f, 20000.0f);
        result.temporalWeight = finiteClamp(
            result.temporalWeight, defaults.temporalWeight, 0.0f, 0.98f);
        result.heightFogDensity=finiteClamp(result.heightFogDensity,defaults.heightFogDensity,0.0f,10.0f);
        result.heightFogHeight=finiteClamp(result.heightFogHeight,defaults.heightFogHeight,-22000.0f,22000.0f);
        result.heightFogFalloff=finiteClamp(result.heightFogFalloff,defaults.heightFogFalloff,64.0f,16384.0f);
        result.heightFogStart=finiteClamp(result.heightFogStart,defaults.heightFogStart,0.0f,20000.0f);
        result.heightFogOpacity=finiteClamp(result.heightFogOpacity,defaults.heightFogOpacity,0.0f,1.0f);
        result.heightFogWeather=finiteClamp(result.heightFogWeather,defaults.heightFogWeather,0.0f,1.0f);
        result.heightFogVolume=finiteClamp(result.heightFogVolume,defaults.heightFogVolume,0.0f,1.0f);
        return result;
    }
}
