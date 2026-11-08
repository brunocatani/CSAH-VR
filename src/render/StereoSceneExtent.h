#pragma once

#include <atomic>
#include <cstdint>

namespace csah::render
{
    // Packed-stereo world scene extent pinned by DLAA qualification at the
    // verified post-TAA boundary. FO4VR also runs image-space passes for
    // secondary render-to-texture scenes; per-eye effects use this extent to
    // recognize the world frame instead of resizing for every pass.
    // Written and read on the render thread; atomics only keep the 64-bit
    // value tear-free. Zero means not yet qualified.
    inline std::atomic_uint64_t stereoSceneExtentValue{};

    inline void publishStereoSceneExtent(
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        stereoSceneExtentValue.store(
            (static_cast<std::uint64_t>(width) << 32) | height,
            std::memory_order_release);
    }

    struct StereoSceneExtent
    {
        std::uint32_t width{};
        std::uint32_t height{};

        [[nodiscard]] bool known() const noexcept
        {
            return width != 0 && height != 0;
        }
    };

    [[nodiscard]] inline StereoSceneExtent stereoSceneExtent() noexcept
    {
        const auto value =
            stereoSceneExtentValue.load(std::memory_order_acquire);
        return {
            static_cast<std::uint32_t>(value >> 32),
            static_cast<std::uint32_t>(value & 0xFFFFFFFFu),
        };
    }
}
