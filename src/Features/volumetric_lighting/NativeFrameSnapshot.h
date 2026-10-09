#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace csah::volumetric_lighting
{
    // The engine reuses constant buffers between its directional draw and the
    // later ImageSpace host. Retaining a COM reference does not freeze contents.
    inline bool snapshotConstantBuffer(ID3D11Device* device,ID3D11DeviceContext* context,
        ID3D11Buffer* source,Microsoft::WRL::ComPtr<ID3D11Buffer>& snapshot) noexcept
    {
        if(!device || !context || !source) return false;
        D3D11_BUFFER_DESC description{};source->GetDesc(&description);
        if(!(description.BindFlags&D3D11_BIND_CONSTANT_BUFFER) || !description.ByteWidth || description.ByteWidth%16) return false;
        D3D11_BUFFER_DESC previous{};if(snapshot) snapshot->GetDesc(&previous);
        if(previous.ByteWidth!=description.ByteWidth) {
            D3D11_BUFFER_DESC copy{};copy.ByteWidth=description.ByteWidth;copy.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device->CreateBuffer(&copy,nullptr,snapshot.ReleaseAndGetAddressOf()))) return false;
        }
        context->CopyResource(snapshot.Get(),source);return true;
    }
}
