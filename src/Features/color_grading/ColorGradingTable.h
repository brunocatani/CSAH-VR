#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
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

    // One look: its day, night and interior tables, in that order.
    inline constexpr std::size_t kTableSlotCount = 3;
    using TableSet = std::array<Table, kTableSlotCount>;

    // Every installed look, in the order the Look setting counts them. Each
    // entry of a counted look is a valid table: a case the look has no table
    // for holds the table that leaves colours unchanged.
    inline constexpr std::size_t kMaximumLooks = 8;
    struct LookLibrary final
    {
        std::array<TableSet, kMaximumLooks> looks{};
        std::size_t count{};
    };

    // The table that returns every colour unchanged.
    [[nodiscard]] Table createIdentityTable(ID3D11Device* device) noexcept;

    // Loads every look installed in the folder. Each subfolder is one look,
    // in alphabetical order; table images lying directly in the folder form
    // one more look, counted first. A subfolder may be a whole ENB preset,
    // its enbseries folder or only the table images: it is searched through
    // all its subfolders, so nothing has to be renamed or moved.
    //
    // A table image is recognised by its shape: N slices of N x N either side
    // by side (N * N wide, N high, for example 4096 x 64) or, for a file with
    // "lut" in its name, tiled in a square (for example 512 x 512 for N = 64).
    // Its name decides what it is for: "interior" or "indoor", "night", "day".
    // A table whose name says none of these serves every case that has no
    // table of its own.
    //
    // File I/O: call it at device creation, never from a draw.
    [[nodiscard]] LookLibrary loadLooks(
        ID3D11Device* device,
        const std::filesystem::path& folder) noexcept;
}
