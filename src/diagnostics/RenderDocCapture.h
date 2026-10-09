#pragma once

namespace csah::diagnostics
{
    // Settings-worker entry point. Validates/prepares an explicit output
    // directory and queues at most one request; it performs no D3D work.
    [[nodiscard]] bool requestRenderDocCapture(const wchar_t* directory) noexcept;

    // Existing renderer pre-frame boundary; consumes a manual request once.
    // The plugin only connects to RenderDoc already loaded by the user.
    void processRenderDocCapture() noexcept;
}
