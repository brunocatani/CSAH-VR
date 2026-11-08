#pragma once

namespace csah::render
{
    // FO4VR renders the world a second time for the vanilla weapon scope.
    // BSGraphics::IsScopedPass (RVA 0x01D947D0) is exactly
    // `MOVZX EAX, byte ptr [RCX+4]; RET`, and its callers at 0x00D83886,
    // 0x00D87B85 and 0x027EE5C5 all pass the fixed renderer object
    // (RVA 0x06239340, from `LEA RCX,[rip]` at 0x01DB627E). The byte is set
    // only while the nested scope world and image-space sequence runs.
    // Per-eye stereo work must stay out of that mono scope pass.
    [[nodiscard]] bool installScopedWorldPassProbe() noexcept;

    // Render thread only. False when the probe is not installed.
    [[nodiscard]] bool scopedWorldPassActive() noexcept;
}
