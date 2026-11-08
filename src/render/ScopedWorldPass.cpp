#include "PCH.h"
#include "render/ScopedWorldPass.h"

#include "support/Logger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace csah::render
{
    namespace
    {
        constexpr std::uintptr_t kIsScopedPassRva = 0x01D947D0;
        constexpr std::uintptr_t kRendererAnchorRva = 0x01DB627E;
        constexpr std::uintptr_t kRendererRva = 0x06239340;
        constexpr std::size_t kScopedPassFlagOffset = 0x04;

        constexpr std::array<std::byte, 5> kIsScopedPassBytes{
            std::byte{ 0x0F }, std::byte{ 0xB6 }, std::byte{ 0x41 },
            std::byte{ 0x04 }, std::byte{ 0xC3 } };
        constexpr std::array<std::byte, 3> kLeaRcxRip{
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x0D } };

        // Inside the FO4VR image's data section, so it stays readable for
        // the process lifetime once verified; null until then.
        const volatile std::uint8_t* scopedPassFlag{};
    }

    bool installScopedWorldPassProbe() noexcept
    {
        try {
            const auto base = REL::Module::get().base();
            const auto* accessor =
                reinterpret_cast<const std::byte*>(base + kIsScopedPassRva);
            const auto* anchor =
                reinterpret_cast<const std::byte*>(base + kRendererAnchorRva);
            if (std::memcmp(accessor, kIsScopedPassBytes.data(),
                    kIsScopedPassBytes.size()) != 0 ||
                std::memcmp(anchor, kLeaRcxRip.data(), kLeaRcxRip.size()) != 0) {
                logging::warn(
                    "Scoped world pass probe rejected the FO4VR IsScopedPass or renderer anchor bytes; scope passes are not distinguished.");
                return false;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, anchor + 3, sizeof(displacement));
            const auto renderer =
                reinterpret_cast<std::uintptr_t>(anchor + 7) + displacement;
            if (renderer != base + kRendererRva) {
                logging::warn(
                    "Scoped world pass probe resolved the renderer at RVA 0x{:X}, expected 0x{:X}; scope passes are not distinguished.",
                    renderer - base,
                    kRendererRva);
                return false;
            }
            scopedPassFlag = reinterpret_cast<const volatile std::uint8_t*>(
                renderer + kScopedPassFlagOffset);
            logging::info(
                "Scoped world pass probe verified FO4VR IsScopedPass (RVA 0x{:X}) on the renderer at RVA 0x{:X}.",
                kIsScopedPassRva,
                kRendererRva);
            return true;
        } catch (...) {
            logging::warn("Scoped world pass probe installation failed.");
            return false;
        }
    }

    bool scopedWorldPassActive() noexcept
    {
        const auto* flag = scopedPassFlag;
        return flag && *flag != 0;
    }
}
