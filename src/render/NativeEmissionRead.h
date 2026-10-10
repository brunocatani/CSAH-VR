#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace csah::render::native_emission_read
{
    // BSDFPrePass material setup (vtable slot 3), FO4VR 1.2.72. Three
    // independent channel producers load the same optional NiColor pointer.
    inline constexpr std::array<std::uintptr_t, 3> sites{0x287B2F3, 0x287B32B, 0x287B368};
    inline constexpr std::array<std::uint8_t, 7> load{0x49,0x8B,0x85,0xA0,0x01,0x00,0x00};
    inline constexpr std::size_t stride = 64;

    // Preserve native pointer/color identity when present. A missing color
    // contributes no emission, without allocating or mutating the property.
    // PUSHFQ/POPFQ preserve flags across TEST; no other register is touched.
    [[nodiscard]] inline std::array<std::uint8_t, 38> thunk(
        std::uintptr_t zeroColor, std::uintptr_t resume) noexcept
    {
        std::array<std::uint8_t, 38> bytes{
            0x49,0x8B,0x85,0xA0,0x01,0x00,0x00,
            0x9C,0x48,0x85,0xC0,0x75,0x0A,
            0x48,0xB8,0,0,0,0,0,0,0,0,0x9D,
            0xFF,0x25,0,0,0,0,0,0,0,0,0,0,0,0
        };
        std::memcpy(bytes.data() + 15, &zeroColor, sizeof(zeroColor));
        std::memcpy(bytes.data() + 30, &resume, sizeof(resume));
        return bytes;
    }

    [[nodiscard]] bool install() noexcept;
}
