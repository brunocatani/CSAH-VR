#pragma once

#include <d3d11.h>

#include <cstdint>

namespace csah::diagnostics
{
    // Temporary Linear Lighting evidence: records the formats of the pixel
    // shader resources bound to replacement draws, per shader family, and logs
    // one summary per family once enough draws were seen. It answers whether
    // the textures CSAH re-decodes are already sampled through sRGB views.
    // Render thread only (immediate context draw hooks).
    void observeReplacementDraw(ID3D11DeviceContext* context, std::uint8_t family) noexcept;
}
