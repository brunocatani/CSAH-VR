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
        // Every hook entry, counted before any gate, plus the published
        // consumer mask, so a hook the engine never calls is distinguishable
        // from one that is gated off.
        std::uint64_t techniqueEntries{};
        std::uint64_t techniqueAccepted{};
        std::uint64_t geometryEntries{};
        std::uint32_t consumerMask{};
        // Renderer SetShaders descriptor source used by cached G-buffer
        // command records, which never call the vtable slots above.
        bool setShadersInstalled{};
        std::uint64_t shaderKeyPublications{};
        // Live dispatch of the engine's BSDFPrePassShader singleton: its
        // vtable pointer and the SetupTechnique/SetupGeometry entries the
        // renderer actually calls (0 when unreadable).
        std::uintptr_t liveVtable{};
        std::uintptr_t liveSetupTechnique{};
        std::uintptr_t liveSetupGeometry{};
        std::uintptr_t patchedVtable{};
    };

    // Fallout4VR.exe 1.2.72 BSDFPrePassShader slots 4/5 own the
    // SetupTechnique/RestoreTechnique descriptor lifetime of the virtual
    // per-pass path. Slot 9 owns per-draw SetupGeometry and exposes
    // geometryState + 0x40 as reinforcement for retained shader binds. Most
    // G-buffer draws are cached command records that bypass those slots; the
    // renderer SetShaders detour publishes the bound pixel-shader key for
    // every bind instead. Every hook and the instructions it relies on are
    // guarded by exact live identity bytes.
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
    void setDFPrePassTerrainVariationEnabled(bool enabled) noexcept;
    void setDFPrePassLodBlendingEnabled(bool enabled) noexcept;
    void setDFPrePassAuthoredPbrEnabled(bool enabled) noexcept;

    [[nodiscard]] DFPrePassHookSnapshot
        dFPrePassHookSnapshot() noexcept;
}
