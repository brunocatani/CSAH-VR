#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace csah::render::native_shadow_property
{
    // FO4VR 1.2.72 shared shadow-pass resolver. Both 2026-10-10 interior
    // dumps have RSI=property, [RSI]=0, R8D=15 at its virtual call +0x170.
    inline constexpr std::uintptr_t site=0x281C78D, resume=0x281C797, empty=0x281C816;
    inline constexpr std::array<std::uint8_t,10> original{0x48,0x8B,0x06,0x44,0x8B,0x87,0x88,0xF6,0,0};
    // Keep valid calls and their flags/registers intact. A cleared property
    // takes the native empty-pass epilogue; no scene object is mutated.
    inline std::array<std::uint8_t,46> thunk(std::uintptr_t continuation,std::uintptr_t noPass) noexcept
    {
        std::array<std::uint8_t,46> code{
            0x48,0x8B,0x06, 0x9C, 0x48,0x85,0xC0, 0x74,0x16,
            0x9D, 0x44,0x8B,0x87,0x88,0xF6,0,0,
            0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
            0x9D, 0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0};
        std::memcpy(code.data()+23,&continuation,8);
        std::memcpy(code.data()+38,&noPass,8);
        return code;
    }
    bool install() noexcept;
}
