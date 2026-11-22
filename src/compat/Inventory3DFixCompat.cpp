#include "compat/Inventory3DFixCompat.h"

#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace csah::compat::inventory_3d_fix
{
    namespace
    {
        // Inventory3DFix (Nexus 102195) clears the environment-map flag on
        // the materials of an inventory model while it is drawn and writes
        // the saved values back afterwards: the flags at +0x30 and four
        // bytes at +0xD8. It takes every object behind a geometry's two
        // property slots whose first field points into the game image, so
        // effect materials and alpha properties pass as well. Those are
        // smaller than a lighting material. An effect material sits in a
        // 0xA0-byte heap block, so +0xD8 is the render-pass list of the next
        // effect material; writing the saved value back there re-links
        // passes the draw has already freed, and the game later crashes
        // freeing them again in BSShaderProperty::RenderPassArray::Clear.
        //
        // The patch replaces that test, and the alpha-range test after it,
        // with one comparison against the lighting material's vtable, the
        // only type the mod means to change. RDX holds the game image base
        // at this point: the lead-in below loads it.
        constexpr std::uintptr_t kLeadInRva = 0x65D3;
        constexpr std::uintptr_t kFilterRva = 0x6604;
        constexpr std::uintptr_t kSkipRva = 0x6659;

        constexpr std::array<std::uint8_t, 49> kLeadIn{
            0x48, 0x85, 0xD2,                          // test rdx,rdx
            0x75, 0x2C,                                // jne filter
            0x33, 0xC9,                                // xor ecx,ecx
            0xFF, 0x15, 0xE8, 0x2E, 0x0D, 0x00,        // call GetModuleHandle
            0x44, 0x8B, 0x15, 0x41, 0xB9, 0x12, 0x00,  // mov r10d,[saved count]
            0x48, 0x8B, 0xD0,                          // mov rdx,rax
            0x48, 0x89, 0x05, 0x77, 0xB5, 0x12, 0x00,  // mov [image base],rax
            0x48, 0x63, 0x40, 0x3C,                    // movsxd rax,[rax+0x3C]
            0x44, 0x8B, 0x44, 0x10, 0x50,              // mov r8d,[rax+rdx+0x50]
            0x4C, 0x03, 0xC2,                          // add r8,rdx
            0x4C, 0x89, 0x05, 0x0C, 0xB7, 0x12, 0x00,  // mov [image end],r8
        };
        constexpr std::array<std::uint8_t, 25> kOriginalFilter{
            0x48, 0x3B, 0xFA,              // cmp rdi,rdx
            0x72, 0x50,                    // jb skip
            0x49, 0x3B, 0xF8,              // cmp rdi,r8
            0x73, 0x4B,                    // jae skip
            0xF3, 0x0F, 0x10, 0x43, 0x28,  // movss xmm0,[rbx+0x28]
            0x0F, 0x2F, 0xF8,              // comiss xmm7,xmm0
            0x77, 0x41,                    // ja skip
            0x0F, 0x2F, 0xC6,              // comiss xmm0,xmm6
            0x77, 0x3C,                    // ja skip
        };
        constexpr std::array<std::uint8_t, 25> kLightingOnlyFilter{
            0x48, 0x8D, 0x82, 0x18, 0x5C, 0x0A, 0x03,  // lea rax,[rdx+0x030A5C18]
            0x48, 0x3B, 0xF8,                          // cmp rdi,rax
            0x75, 0x49,                                // jne skip
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        };
        constexpr std::array<std::uint8_t, 4> kSkipTarget{
            0x48, 0x83, 0xC6, 0x04,  // add rsi,4
        };

        // BSLightingShaderProperty's constructor installs its vtable with
        // lea rax,[rip+0x0090384F] at this address, which resolves to image
        // base + 0x030A5C18, the constant in the patched comparison.
        constexpr std::uintptr_t kLightingVtableLoadRva = 0x027A23C2;
        constexpr std::array<std::uint8_t, 7> kLightingVtableLoad{
            0x48, 0x8D, 0x05, 0x4F, 0x38, 0x90, 0x00,
        };

        template <std::size_t Size>
        [[nodiscard]] bool matches(
            const std::uintptr_t address,
            const std::array<std::uint8_t, Size>& bytes) noexcept
        {
            return std::memcmp(
                       reinterpret_cast<const void*>(address),
                       bytes.data(),
                       bytes.size()) == 0;
        }

        [[nodiscard]] std::size_t imageSize(const std::uintptr_t base) noexcept
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
                return 0;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));
            return nt->Signature == IMAGE_NT_SIGNATURE
                       ? nt->OptionalHeader.SizeOfImage
                       : 0;
        }
    }

    void apply() noexcept
    {
        const auto mod = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(L"Inventory3DFix.dll"));
        if (!mod) {
            return;
        }
        const auto filter = mod + kFilterRva;
        const bool mapped =
            imageSize(mod) >= kSkipRva + kSkipTarget.size();
        if (mapped && matches(filter, kLightingOnlyFilter)) {
            return;
        }
        const auto game = REL::Module::get().base();
        if (!mapped || !matches(mod + kLeadInRva, kLeadIn) ||
            !matches(filter, kOriginalFilter) ||
            !matches(mod + kSkipRva, kSkipTarget) ||
            !matches(game + kLightingVtableLoadRva, kLightingVtableLoad)) {
            logging::warn(
                "Inventory3DFix is loaded but is not the known build; its out-of-bounds material restore was left unpatched.");
            return;
        }

        DWORD previous{};
        if (!VirtualProtect(
                reinterpret_cast<void*>(filter),
                kLightingOnlyFilter.size(),
                PAGE_EXECUTE_READWRITE,
                &previous)) {
            logging::warn(
                "Inventory3DFix could not be patched: its code page did not become writable (error {}).",
                GetLastError());
            return;
        }
        std::memcpy(
            reinterpret_cast<void*>(filter),
            kLightingOnlyFilter.data(),
            kLightingOnlyFilter.size());
        DWORD ignored{};
        VirtualProtect(
            reinterpret_cast<void*>(filter),
            kLightingOnlyFilter.size(),
            previous,
            &ignored);
        FlushInstructionCache(
            GetCurrentProcess(),
            reinterpret_cast<void*>(filter),
            kLightingOnlyFilter.size());
        logging::info(
            "Inventory3DFix patched: its material save and restore now covers lighting materials only.");
    }
}
