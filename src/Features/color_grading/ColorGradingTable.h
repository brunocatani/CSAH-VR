#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>

namespace csah::color_grading
{
    // A colour table (LUT) as a cube texture: red, green and blue of the
    // input colour are its three coordinates.
    struct Table final
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view{};
        std::uint32_t size{};
        bool fromFile{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return view && size >= 2;
        }
    };

    // The table that returns every colour unchanged.
    [[nodiscard]] Table createIdentityTable(ID3D11Device* device) noexcept;

    // Reads an unwrapped table image, the layout ENB presets and most colour
    // tools export: N slices of N x N side by side, blue rising from slice to
    // slice, so the image is N * N wide and N high (4096 x 64 for N = 64).
    // Returns an empty table, with the reason logged, when the file is
    // missing or is not such an image. File I/O: call it at device creation,
    // never from a draw.
    [[nodiscard]] Table loadTable(
        ID3D11Device* device,
        const std::filesystem::path& file) noexcept;
}
