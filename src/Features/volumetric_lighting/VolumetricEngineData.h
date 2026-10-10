#pragma once

#include <array>

namespace csah::volumetric_lighting
{
    struct StereoFrame final
    {
        bool valid{};
        std::array<std::array<float, 4>, 2> eyeOrigin{};
        std::array<std::array<float, 16>, 2> viewProjection{};
    };

    struct WeatherFrame final
    {
        bool valid{};
        bool cellOverride{};
        std::array<float, 11> medium{};
        float intensity{};
    };

    [[nodiscard]] bool initializeEngineData() noexcept;
    [[nodiscard]] StereoFrame captureStereoFrame() noexcept;
    enum class FogDomain { unavailable, interior, exterior };
    [[nodiscard]] FogDomain heightFogDomain() noexcept;
    [[nodiscard]] WeatherFrame captureWeatherFrame() noexcept;
}
