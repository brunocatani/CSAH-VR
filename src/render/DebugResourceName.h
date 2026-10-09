#pragma once

#include <d3d11.h>
#include <cstddef>

namespace csah::render
{
    // D3D copies this label at allocation time. RenderDoc exposes the names
    // beside captured textures/buffers without any recurring inspection pass.
    template <std::size_t N>
    void nameResource(ID3D11DeviceChild* resource, const char (&name)[N]) noexcept
    {
        if (resource) (void)resource->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(N - 1), name);
    }
}
