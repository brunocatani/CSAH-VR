#pragma once

#include "render/Fo4VrCamera.h"

namespace csah::dlaa
{
    enum CameraFailure : std::uint32_t
    {
        projectionPlanesInvalid = 0x001,
        projectionNonfinite = 0x002,
        projectionEmpty = 0x004,
        inverseProjectionNonfinite = 0x008,
        inverseProjectionEmpty = 0x010,
        firstPersonInverseNonfinite = 0x020,
        currentHistoryNonfinite = 0x040,
        previousHistoryNonfinite = 0x080,
        eyeOriginNonfinite = 0x100,
    };

    struct CameraValidation
    {
        std::array<std::uint32_t, 2> eyeFailures{};
        [[nodiscard]] bool valid() const noexcept { return eyeFailures[0] == 0 && eyeFailures[1] == 0; }
    };

    [[nodiscard]] inline CameraValidation inspectCamera(const render::Fo4VrFrameBuffer& frame) noexcept
    {
        const auto finite = [](const render::CameraMatrix& matrix) {
            return std::ranges::all_of(matrix, [](const auto& row) {
                return std::isfinite(row.x) && std::isfinite(row.y) && std::isfinite(row.z) && std::isfinite(row.w);
            });
        };
        const auto nonempty = [](const render::CameraMatrix& matrix) {
            return std::ranges::any_of(matrix, [](const auto& row) {
                return std::abs(row.x) > 0.01F || std::abs(row.y) > 0.01F ||
                    std::abs(row.z) > 0.01F || std::abs(row.w) > 0.01F;
            });
        };
        CameraValidation result;
        for (std::size_t eye = 0; eye < 2; ++eye) {
            auto& failures = result.eyeFailures[eye];
            if (!render::projectionPlanes(frame.projection[eye]).valid) failures |= projectionPlanesInvalid;
            if (!finite(frame.projection[eye])) failures |= projectionNonfinite;
            if (!nonempty(frame.projection[eye])) failures |= projectionEmpty;
            if (!finite(frame.inverseProjection[eye])) failures |= inverseProjectionNonfinite;
            if (!nonempty(frame.inverseProjection[eye])) failures |= inverseProjectionEmpty;
            if (!finite(frame.inverseFirstPersonProjection[eye])) failures |= firstPersonInverseNonfinite;
            if (!finite(frame.viewProjectionUnjittered[eye])) failures |= currentHistoryNonfinite;
            if (!finite(frame.previousViewProjectionUnjittered[eye])) failures |= previousHistoryNonfinite;
            const auto& position = frame.currentEyePosition[eye];
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) failures |= eyeOriginNonfinite;
        }
        return result;
    }
}
