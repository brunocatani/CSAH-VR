#pragma once

// Windows.h defines MEM_RELEASE, which CommonLib spells as a scoped constant.
#pragma push_macro("MEM_RELEASE")
#undef MEM_RELEASE
#include <RE/NetImmerse/NiSmartPointer.h>
#include <RE/NetImmerse/NiTexture.h>
#pragma pop_macro("MEM_RELEASE")

namespace csah::render
{
    // Loads a texture through the VR resource system with an opened stream.
    // The filename-only NiTexture factory (0x141C24220) never attaches a
    // stream in FO4VR; the renderer at 0x1429391F0 then passes null to
    // 0x141C12C50 and crashes at 0x141C12DC1. Returns null on any failure;
    // the caller owns the fallback. Main thread only.
    [[nodiscard]] RE::NiPointer<RE::NiTexture> loadStreamBackedTexture(const char* path, bool sRgb) noexcept;
}
