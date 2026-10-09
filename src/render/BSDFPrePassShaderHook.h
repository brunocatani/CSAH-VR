#pragma once

#include <cstdint>

namespace csah::render
{
    struct DFPrePassHookSnapshot
    {
        bool installed{};
        bool vtableCellOwned{};
        std::uint64_t setupCalls{};
        std::uint64_t validationFailures{};
        std::uint32_t lastDescriptor{};
    };

    // Fallout4VR.exe 1.2.72 BSDFPrePassShader slots 4/5 own the complete
    // SetupTechnique/RestoreTechnique descriptor lifetime. Slot 9 owns
    // per-draw SetupGeometry and exposes geometryState + 0x40 as reinforcement
    // for retained shader binds. Every hook and the descriptor-load
    // instruction are guarded by exact live identity bytes.
    [[nodiscard]] bool installBSDFPrePassShaderHook() noexcept;
    [[nodiscard]] bool validateBSDFPrePassShaderHook(
        const char* trigger) noexcept;

    // Descriptor capture is shared by independent material consumers. Publish
    // each runtime requirement so the hook takes the direct original-call path
    // only when no enabled feature needs the exact draw descriptor.
    void setDFPrePassLinearLightingEnabled(bool enabled) noexcept;
    void setDFPrePassComplexEnvironmentEnabled(bool enabled) noexcept;
    void setDFPrePassIblEnabled(bool enabled) noexcept;
    void setDFPrePassSurfaceClassificationEnabled(bool enabled) noexcept;
    void setDFPrePassLodBlendingEnabled(bool enabled) noexcept;
    void setDFPrePassAuthoredPbrEnabled(bool enabled) noexcept;

    [[nodiscard]] DFPrePassHookSnapshot
        dFPrePassHookSnapshot() noexcept;
}
