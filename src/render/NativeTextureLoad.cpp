#include "render/NativeTextureLoad.h"

#include "Features/ibl/IblReferenceLoad.h"
#include "support/Logger.h"

#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif
#include <REL/Relocation.h>
#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace csah::render
{
    namespace
    {
        bool readable(const void* pointer, std::size_t size) noexcept
        {
            MEMORY_BASIC_INFORMATION region{};
            const auto address = reinterpret_cast<std::uintptr_t>(pointer);
            return pointer && VirtualQuery(pointer, &region, sizeof(region)) &&
                region.State == MEM_COMMIT && !(region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                address >= reinterpret_cast<std::uintptr_t>(region.BaseAddress) &&
                size <= region.RegionSize &&
                address - reinterpret_cast<std::uintptr_t>(region.BaseAddress) <= region.RegionSize - size;
        }

        // Raw FO4VR evidence: 141C245CA..141C245D7 attaches the stream before
        // 141C246A4 calls the renderer.
        [[nodiscard]] bool entriesVerified(std::uintptr_t image) noexcept
        {
            const auto verifiedEntry = [&](std::uint64_t id, std::uintptr_t rva, const auto& prefix) {
                REL::Relocation<std::uintptr_t> entry{ REL::ID(id) };
                const auto expected = image + rva;
                const bool accessible = readable(reinterpret_cast<void*>(expected), sizeof(prefix));
                const bool prefixMatches = accessible &&
                    std::memcmp(reinterpret_cast<void*>(expected), prefix, sizeof(prefix)) == 0;
                const bool accepted = entry.address() == expected && prefixMatches;
                if (!accepted) {
                    logging::warn(
                        "Stream-backed texture loader entry {} rejected: resolvedRVA=0x{:X}, expectedRVA=0x{:X}, readable={}, prefixMatches={}.",
                        id, entry.address() - image, rva, accessible, prefixMatches);
                }
                return accepted;
            };
            constexpr unsigned char openPrefix[]{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
            constexpr unsigned char closePrefix[]{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x51, 0x18 };
            constexpr unsigned char createPrefix[]{ 0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x20 };
            return verifiedEntry(1198116, 0x01C12B50, openPrefix) &&
                verifiedEntry(1516202, 0x01C12E10, closePrefix) &&
                verifiedEntry(685580, 0x01C24500, createPrefix);
        }
    }

    RE::NiPointer<RE::NiTexture> loadStreamBackedTexture(const char* path, bool sRgb) noexcept
    {
        try {
            if (!path || !*path) {
                return {};
            }
            const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            static const bool verified = entriesVerified(image);
            if (!verified) {
                return {};
            }
            using NativeStream = RE::BSResourceNiBinaryStream;
            using ConstructStream = NativeStream* (*)(void*, const char*, bool, RE::BSResource::Location*, bool);
            using DestroyStream = void (*)(NativeStream*);
            // 141C12EE5..141C12F15 releases +0x10 itself, without nulling it.
            // Avoid the SDK wrapper's additional automatic member destruction.
            ibl::NativeStreamOwner<NativeStream> owner(
                [&](void* storage) {
                    reinterpret_cast<ConstructStream>(image + 0x01C12B50)(storage, path, false, nullptr, false);
                },
                reinterpret_cast<DestroyStream>(image + 0x01C12E10));
            auto& opened = owner.get();
            if (!opened.stream || opened.lastError != RE::BSResource::ErrorCode::kNone) {
                return {};
            }
            return RE::NiPointer<RE::NiTexture>{ ibl::createTextureFromOpenedStream(
                opened, path,
                [sRgb](auto& stream, const char* name, bool isDdx, bool, bool allowDegrade) {
                    return RE::NiTexture::Create(stream, name, isDdx, sRgb, allowDegrade);
                }) };
        } catch (...) {
            return {};
        }
    }
}
