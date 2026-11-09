#pragma once

#include <filesystem>

namespace csah::diagnostics
{
    // renderDocCapture is CSAH.toml's [Diagnostics] RenderDocCapture; the
    // backend also needs RobCo PALM's Developer wheel, read from PALM.toml
    // beside CSAH's settings directory.
    void initializeDeveloperCapture(bool renderDocCapture, const std::filesystem::path& settingsDirectory) noexcept;
    void registerDeveloperCapture() noexcept;
    // Existing settings-worker service; consumes only explicit wheel requests.
    // settingsPath (CSAH.toml) is saved beside each capture.
    void serviceDeveloperCapture(const wchar_t* settingsPath) noexcept;
    // Opt-in startup only, before installing the game's D3D creation hook.
    // Loads the installed capture backend; never queues a capture itself.
    [[nodiscard]] bool initializeRenderDocCapture(const wchar_t* modulePath) noexcept;
    [[nodiscard]] bool isRenderDocCreationHook(const void* address) noexcept;
    // Settings-worker entry point. Validates/prepares an explicit output
    // directory and queues at most one request; it performs no D3D work.
    [[nodiscard]] bool requestRenderDocCapture(const wchar_t* directory,
        const wchar_t* settingsPath = nullptr) noexcept;

    // Existing renderer pre-frame boundary; consumes a manual request once.
    // Connects to the startup-enabled or externally attached capture backend.
    void processRenderDocCapture() noexcept;
}
