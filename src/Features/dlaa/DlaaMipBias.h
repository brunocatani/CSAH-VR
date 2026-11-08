#pragma once

#include <cstdint>

struct ID3D11Device;

namespace csah::dlaa::mip_bias
{
    // Texture mip LOD bias for DLAA/DLSS, NVIDIA's log2(render/display) - 1.
    //
    // FO4VR creates all 320 sampler states once into one flat table
    // (RVA 0x062350A0, index (addressMode*5 + filter)*16 + mip) with
    // MipLODBias 0. Three instructions name that base: the creation loop at
    // 0x01DBF0B8, the record cache at 0x0292F944 and the PS bind site at
    // 0x01D9BBB0; the shutdown releaser at 0x01DBF326 walks it too. Only the
    // anisotropic entries are biased: comparison (shadow) entries must never
    // move, point and linear-mip-point entries clamp MaxLOD to MinLOD, and
    // trilinear entries serve full-screen passes. MinLOD stays untouched so the
    // game's distance degrade remains.
    //
    // Ownership: the engine copies table pointers by value (0x01D97BAB) and
    // releases whatever the table holds at shutdown, so the originals are never
    // released and the biased duplicates live for the process lifetime. Only
    // pointers are swapped, and only between apply() and restore().
    //
    // Threading: render thread only, except install(), which reads code bytes.
    // Technique after Fo4VR-Upscaler's MipBias/SamplerBias, which follows
    // fallout4-community-shaders; no code was copied.
    [[nodiscard]] bool install() noexcept;

    // Builds duplicates for this bias when it changed. False leaves the game's
    // samplers in use.
    [[nodiscard]] bool build(ID3D11Device* device, float bias) noexcept;

    // Swaps the biased duplicates in for the world frame. Declines unless every
    // marked slot still holds the engine's original.
    void apply() noexcept;

    // Puts the originals back. Safe to call when nothing is applied.
    void restore() noexcept;

    [[nodiscard]] bool applied() noexcept;
}
