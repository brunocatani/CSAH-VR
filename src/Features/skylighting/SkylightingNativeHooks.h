#pragma once

#include <cstdint>

namespace csah::skylighting
{
    class ScopedOcclusionPassProduction final
    {
    public:
        explicit ScopedOcclusionPassProduction(
            std::uint32_t captureQuadrant) noexcept;
        ~ScopedOcclusionPassProduction() noexcept;

        ScopedOcclusionPassProduction(
            const ScopedOcclusionPassProduction&) = delete;
        ScopedOcclusionPassProduction& operator=(
            const ScopedOcclusionPassProduction&) = delete;

        [[nodiscard]] bool active() const noexcept;

    private:
        bool active_{};
    };

    [[nodiscard]] bool installNativeHooks() noexcept;
    [[nodiscard]] bool validateNativeHooks(const char* trigger) noexcept;
    // World translate of the node FO4VR's precipitation projection subtracts
    // when it builds the occlusion matrix. Valid only on the render thread
    // directly after a native precipitation render, which dereferenced the
    // same node.
    [[nodiscard]] bool readProjectionAnchor(float (&translate)[3]) noexcept;
}
