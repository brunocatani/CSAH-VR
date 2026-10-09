#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace csah::render
{
    struct CameraVector
    {
        float x{}, y{}, z{}, w{};
    };
    using CameraMatrix = std::array<CameraVector, 4>;
    inline constexpr std::uint32_t kFo4VrCameraRows = 77;
    inline constexpr std::uint32_t kFo4VrCurrentEyeOriginRow = 59;

    // Fallout4VR.exe: 141D93910 transposes/copies this 0x4D0 prefix.
    // 141DAA860 independently copies both current origins before both previous
    // origins, from the per-eye state produced by 141DAA370. The larger native
    // allocation is not evidence of initialized camera data after this prefix.
    struct Fo4VrFrameBuffer
    {
        CameraMatrix view;
        std::array<CameraMatrix, 2> projection;
        std::array<CameraMatrix, 2> viewProjection;
        CameraMatrix inverseView;
        std::array<CameraMatrix, 2> inverseViewProjection;
        std::array<CameraMatrix, 2> inverseProjection;
        // Optional native first-person data; both matrices can be NaN during
        // a valid VR world frame. Do not make world rendering depend on them.
        std::array<CameraMatrix, 2> inverseFirstPersonProjection;
        // Copied from global shader state by 141D93910, not from the camera
        // frustum. Derive clip planes from projection instead of these rows.
        CameraVector shaderParameters48;
        CameraVector shaderParameters49;
        CameraVector shaderParameters50;
        std::array<CameraMatrix, 2> previousViewProjectionUnjittered;
        // The native copies write xyz only; origin w lanes are padding.
        std::array<CameraVector, 2> currentEyePosition;
        std::array<CameraVector, 2> previousEyePosition;
        std::array<CameraMatrix, 2> viewProjectionUnjittered;
        std::array<CameraVector, 6> fog;
    };
    static_assert(offsetof(Fo4VrFrameBuffer, projection) == 0x40);
    static_assert(offsetof(Fo4VrFrameBuffer, inverseView) == 0x140);
    static_assert(offsetof(Fo4VrFrameBuffer, inverseProjection) == 0x200);
    static_assert(offsetof(Fo4VrFrameBuffer, inverseFirstPersonProjection) == 0x280);
    static_assert(offsetof(Fo4VrFrameBuffer, shaderParameters48) == 0x300);
    static_assert(offsetof(Fo4VrFrameBuffer, previousViewProjectionUnjittered) == 0x330);
    static_assert(offsetof(Fo4VrFrameBuffer, currentEyePosition) == 0x3B0);
    static_assert(offsetof(Fo4VrFrameBuffer, previousEyePosition) == 0x3D0);
    static_assert(offsetof(Fo4VrFrameBuffer, viewProjectionUnjittered) == 0x3F0);
    static_assert(sizeof(Fo4VrFrameBuffer) == 0x4D0);
    static_assert(sizeof(Fo4VrFrameBuffer) == kFo4VrCameraRows * sizeof(CameraVector));

    struct ProjectionPlanes
    {
        float nearPlane{}, farPlane{};
        bool inverted{};
        bool valid{};
    };

    [[nodiscard]] inline CameraMatrix cropClipMatrix(const CameraMatrix& matrix,
        float left, float top, float width, float height) noexcept
    {
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) || !std::isfinite(height) ||
            left < 0 || top < 0 || width <= 0 || height <= 0 || left + width > 1.000001F || top + height > 1.000001F)
            return {};
        auto result = matrix;
        const auto xBias = (1.0F - 2.0F * left - width) / width;
        const auto yBias = (2.0F * top + height - 1.0F) / height;
        const auto combine = [](const CameraVector& row, const CameraVector& w, float scale, float bias) {
            return CameraVector{row.x * scale + w.x * bias, row.y * scale + w.y * bias,
                row.z * scale + w.z * bias, row.w * scale + w.w * bias};
        };
        result[0] = combine(matrix[0], matrix[3], 1.0F / width, xBias);
        result[1] = combine(matrix[1], matrix[3], 1.0F / height, yBias);
        return result;
    }

    [[nodiscard]] inline ProjectionPlanes projectionPlanes(const CameraMatrix& projection) noexcept
    {
        const auto& z = projection[2];
        const auto& w = projection[3];
        // Native perspective construction: 141DA8E60. Solve clip z/w at 0
        // and 1 from the uploaded rows; this also handles an inverted matrix.
        if (!std::isfinite(z.z) || !std::isfinite(z.w) || !std::isfinite(w.z) ||
            !std::isfinite(z.x) || !std::isfinite(z.y) || !std::isfinite(w.x) || !std::isfinite(w.y) ||
            !std::isfinite(w.w) || std::abs(w.z) < 1e-8F ||
            std::abs(z.x) > 1e-6F || std::abs(z.y) > 1e-6F ||
            std::abs(w.x) > 1e-6F || std::abs(w.y) > 1e-6F ||
            z.z == 0 || z.z == w.z) return {};
        const auto atZero = -static_cast<double>(z.w) / z.z;
        const auto atOne = (static_cast<double>(w.w) - z.w) / (static_cast<double>(z.z) - w.z);
        if (!std::isfinite(atZero) || !std::isfinite(atOne) || atZero <= 0 || atOne <= 0 || atZero == atOne)
            return {};
        const auto nearPlane = static_cast<float>((std::min)(atZero, atOne));
        const auto farPlane = static_cast<float>((std::max)(atZero, atOne));
        return {nearPlane, farPlane, atZero > atOne,
            std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0 && farPlane > nearPlane};
    }
}
