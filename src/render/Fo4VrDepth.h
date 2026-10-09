#pragma once

#include "render/Fo4VrCamera.h"

namespace csah::render
{
    struct alignas(16) DepthPreparationConstants
    {
        std::uint32_t width{}, height{}, sourceLeft{}, sourceTop{};
        CameraVector localUvScaleBias;
        CameraVector firstPersonToWorldZ;
        CameraVector firstPersonToWorldW;
        float farDepth{1.0F};
        std::uint32_t rawProbe{};
        std::uint32_t firstPersonProjectionAvailable{};
        std::uint32_t nativeDepthOutputEnabled{};
    };
    static_assert(sizeof(DepthPreparationConstants) == 80);

    [[nodiscard]] inline CameraVector rowThroughMatrix(
        const CameraVector& r, const CameraMatrix& m) noexcept
    {
        return {
            r.x * m[0].x + r.y * m[1].x + r.z * m[2].x + r.w * m[3].x,
            r.x * m[0].y + r.y * m[1].y + r.z * m[2].y + r.w * m[3].y,
            r.x * m[0].z + r.y * m[1].z + r.z * m[2].z + r.w * m[3].z,
            r.x * m[0].w + r.y * m[1].w + r.z * m[2].w + r.w * m[3].w,
        };
    }
}
