#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "Features/dlaa/DlaaEngineHooks.h"

#include "Features/dlaa/DlaaRuntime.h"
#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace csah::dlaa
{
    namespace
    {
        constexpr std::size_t kRelativeCallBytes = 5;
        constexpr std::size_t kAbsoluteJumpBytes = 12;
        constexpr std::size_t kJumpStubStride = 16;
        constexpr std::size_t kJumpIslandBytes = 2 * kJumpStubStride;

        // Fallout4VR.exe 1.2.72 contracts independently verified in the local
        // binary. These are deliberately not CommonLib flat-derived IDs.
        constexpr std::uintptr_t kPreRenderCallsiteRva = 0x0284EBC4;
        constexpr std::uintptr_t kPreRenderTargetRva = 0x01DBA040;
        constexpr std::uintptr_t kPostImageSpaceCallsiteRva = 0x0284E4A5;
        constexpr std::uintptr_t kPostImageSpaceTargetRva = 0x01DBA030;
        constexpr std::uintptr_t kDynamicResolutionManagerRva = 0x038AC010;
        constexpr std::uintptr_t kGraphicsStateRva = 0x065A2AB0;
        constexpr std::size_t kJitterXOffset = 0x4;
        constexpr std::size_t kJitterYOffset = 0x8;

        constexpr std::array<std::byte, 5> kPreRenderSignature{
            std::byte{ 0xE8 }, std::byte{ 0x77 }, std::byte{ 0xB4 },
            std::byte{ 0x56 }, std::byte{ 0xFF },
        };
        constexpr std::array<std::byte, 5> kPostImageSpaceSignature{
            std::byte{ 0xE8 }, std::byte{ 0x86 }, std::byte{ 0xBB },
            std::byte{ 0x56 }, std::byte{ 0xFF },
        };
        using PreRenderFunction = void(__fastcall*)(
            void*, const void*, const void*, const void*, const void*);
        using PostImageSpaceFunction = void(__fastcall*)(void*, std::uint32_t);

        std::byte* executableImage{};
        std::size_t executableImageSize{};
        std::byte* jumpIsland{};
        std::array<std::byte*, 2> jumpStubs{};
        PreRenderFunction originalPreRender{};
        PostImageSpaceFunction originalPostImageSpace{};
        std::atomic_bool installed{};
        std::atomic_bool owned{};
        std::atomic_uint64_t validationFailures{};

        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            if (start > (std::numeric_limits<std::uintptr_t>::max)() - size) {
                return false;
            }
            const auto finish = start + size;
            auto cursor = start;
            while (cursor < finish) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto regionStart = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (regionStart >
                    (std::numeric_limits<std::uintptr_t>::max)() -
                        information.RegionSize) {
                    return false;
                }
                const auto regionEnd = regionStart + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(finish, regionEnd);
            }
            return true;
        }

        [[nodiscard]] bool isExecutableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!isReadableRange(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & executable) != 0;
        }

        [[nodiscard]] bool addRelativeDisplacement(
            std::uintptr_t nextInstruction,
            std::int32_t displacement,
            std::uintptr_t& destination) noexcept
        {
            if (displacement >= 0) {
                const auto positive = static_cast<std::uintptr_t>(displacement);
                if (positive >
                    (std::numeric_limits<std::uintptr_t>::max)() -
                        nextInstruction) {
                    return false;
                }
                destination = nextInstruction + positive;
                return true;
            }
            const auto magnitude = static_cast<std::uintptr_t>(
                -static_cast<std::int64_t>(displacement));
            if (magnitude > nextInstruction) {
                return false;
            }
            destination = nextInstruction - magnitude;
            return true;
        }

        [[nodiscard]] const std::byte* resolveRelativeCall(
            const std::byte* instruction) noexcept
        {
            if (!isReadableRange(instruction, kRelativeCallBytes) ||
                instruction[0] != std::byte{ 0xE8 }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(
                &displacement,
                instruction + 1,
                sizeof(displacement));
            std::uintptr_t destination{};
            if (!addRelativeDisplacement(
                    reinterpret_cast<std::uintptr_t>(instruction) +
                        kRelativeCallBytes,
                    displacement,
                    destination)) {
                return nullptr;
            }
            return reinterpret_cast<const std::byte*>(destination);
        }

        [[nodiscard]] bool displacementForTarget(
            const std::byte* instruction,
            const void* destination,
            std::int32_t& displacement) noexcept
        {
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) +
                kRelativeCallBytes;
            const auto delta = static_cast<std::int64_t>(
                                   reinterpret_cast<std::uintptr_t>(
                                       destination)) -
                static_cast<std::int64_t>(next);
            if (delta < (std::numeric_limits<std::int32_t>::min)() ||
                delta > (std::numeric_limits<std::int32_t>::max)()) {
                return false;
            }
            displacement = static_cast<std::int32_t>(delta);
            return true;
        }

        [[nodiscard]] bool writeExecutableBytes(
            void* destination,
            const void* source,
            std::size_t size) noexcept
        {
            if (!destination || !source || size == 0) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    destination,
                    size,
                    PAGE_EXECUTE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            std::memcpy(destination, source, size);
            DWORD discardedProtection{};
            const auto restored = VirtualProtect(
                destination,
                size,
                oldProtection,
                &discardedProtection);
            FlushInstructionCache(GetCurrentProcess(), destination, size);
            return restored != FALSE;
        }

        [[nodiscard]] bool writeRelativeCall(
            std::byte* instruction,
            const void* destination) noexcept
        {
            std::int32_t displacement{};
            if (!displacementForTarget(
                    instruction,
                    destination,
                    displacement)) {
                return false;
            }
            std::array<std::byte, kRelativeCallBytes> replacement{};
            replacement[0] = std::byte{ 0xE8 };
            std::memcpy(
                replacement.data() + 1,
                &displacement,
                sizeof(displacement));
            return writeExecutableBytes(
                instruction,
                replacement.data(),
                replacement.size());
        }

        void buildAbsoluteJump(
            std::byte* stub,
            const void* destination) noexcept
        {
            stub[0] = std::byte{ 0x48 };
            stub[1] = std::byte{ 0xB8 };
            const auto address = reinterpret_cast<std::uintptr_t>(destination);
            std::memcpy(stub + 2, &address, sizeof(address));
            stub[10] = std::byte{ 0xFF };
            stub[11] = std::byte{ 0xE0 };
        }

        [[nodiscard]] bool absoluteJumpOwned(
            const std::byte* stub,
            const void* destination) noexcept
        {
            if (!isExecutableRange(stub, kAbsoluteJumpBytes) ||
                stub[0] != std::byte{ 0x48 } ||
                stub[1] != std::byte{ 0xB8 } ||
                stub[10] != std::byte{ 0xFF } ||
                stub[11] != std::byte{ 0xE0 }) {
                return false;
            }
            std::uintptr_t encoded{};
            std::memcpy(&encoded, stub + 2, sizeof(encoded));
            return encoded == reinterpret_cast<std::uintptr_t>(destination);
        }

        void __fastcall hookPreRender(
            void* manager,
            const void* minimum,
            const void* maximum,
            const void* target,
            const void* settling) noexcept
        {
            originalPreRender(manager, minimum, maximum, target, settling);
            if (manager == executableImage + kDynamicResolutionManagerRva) {
                auto* graphics = executableImage + kGraphicsStateRva;
                screen_space_gi::Runtime::get().beginFrame();
                cloud_shadows::Runtime::get().beginFrame();
                terrain_shadows::Runtime::get().beginFrame();
                Runtime::get().onPreRender(
                    manager,
                    reinterpret_cast<float*>(graphics + kJitterXOffset),
                    reinterpret_cast<float*>(graphics + kJitterYOffset));
            }
        }

        void __fastcall hookPostImageSpace(
            void* manager,
            std::uint32_t active) noexcept
        {
            originalPostImageSpace(manager, active);
            if (manager == executableImage + kDynamicResolutionManagerRva) {
                Runtime::get().onPostImageSpace();
            }
        }

        [[nodiscard]] bool callOwned(
            std::uintptr_t callsiteRva,
            std::size_t stubIndex,
            const void* hook) noexcept
        {
            return executableImage && stubIndex < jumpStubs.size() &&
                jumpStubs[stubIndex] &&
                resolveRelativeCall(executableImage + callsiteRva) ==
                    jumpStubs[stubIndex] &&
                absoluteJumpOwned(jumpStubs[stubIndex], hook);
        }

        void rollbackTransaction(std::size_t patchedCallsites) noexcept
        {
            owned.store(false, std::memory_order_release);
            constexpr std::array<std::uintptr_t, 2> callsites{
                kPreRenderCallsiteRva,
                kPostImageSpaceCallsiteRva,
            };
            constexpr std::array<std::uintptr_t, 2> targets{
                kPreRenderTargetRva,
                kPostImageSpaceTargetRva,
            };
            for (std::size_t index = 0;
                 index < patchedCallsites && index < callsites.size();
                 ++index) {
                (void)writeRelativeCall(
                    executableImage + callsites[index],
                    executableImage + targets[index]);
            }
            if (jumpIsland) {
                (void)VirtualFree(jumpIsland, 0, MEM_RELEASE);
            }
            jumpIsland = nullptr;
            jumpStubs = {};
            installed.store(false, std::memory_order_release);
        }
    }

    bool installEngineHooks() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return owned.load(std::memory_order_acquire);
        }

        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image) {
            logging::error("DLAA engine hooks rejected a missing image.");
            return false;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (!isReadableRange(dos, sizeof(*dos)) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error("DLAA engine hooks rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            logging::error("DLAA engine hooks rejected invalid PE metadata.");
            return false;
        }
        const auto imageSize = static_cast<std::size_t>(
            nt->OptionalHeader.SizeOfImage);
        const auto inBounds = [imageSize](
                                  std::uintptr_t rva,
                                  std::size_t bytes) noexcept {
            return rva <= imageSize && bytes <= imageSize - rva;
        };
        if (!inBounds(kPreRenderCallsiteRva, kPreRenderSignature.size()) ||
            !inBounds(
                kPostImageSpaceCallsiteRva,
                kPostImageSpaceSignature.size()) ||
            !inBounds(kDynamicResolutionManagerRva, 0x1685) ||
            !inBounds(kGraphicsStateRva + kJitterXOffset, 2 * sizeof(float))) {
            logging::error("DLAA engine hook RVAs exceed the live PE image.");
            return false;
        }

        auto* preCall = image + kPreRenderCallsiteRva;
        auto* postCall = image + kPostImageSpaceCallsiteRva;
        if (!isExecutableRange(preCall, kPreRenderSignature.size()) ||
            std::memcmp(
                preCall,
                kPreRenderSignature.data(),
                kPreRenderSignature.size()) != 0 ||
            resolveRelativeCall(preCall) != image + kPreRenderTargetRva ||
            !isExecutableRange(postCall, kPostImageSpaceSignature.size()) ||
            std::memcmp(
                postCall,
                kPostImageSpaceSignature.data(),
                kPostImageSpaceSignature.size()) != 0 ||
            resolveRelativeCall(postCall) !=
                image + kPostImageSpaceTargetRva) {
            logging::error(
                "DLAA FO4VR engine identity gate failed; vanilla TAA remains active.");
            return false;
        }

        const auto imageAddress = reinterpret_cast<std::uintptr_t>(image);
        if (imageAddress >
            (std::numeric_limits<std::uintptr_t>::max)() - imageSize) {
            logging::error("DLAA jump-island preferred address overflowed.");
            return false;
        }
        const std::array<std::uintptr_t, 2> nextInstructions{
            imageAddress + kPreRenderCallsiteRva + kRelativeCallBytes,
            imageAddress + kPostImageSpaceCallsiteRva + kRelativeCallBytes,
        };
        auto* island = static_cast<std::byte*>(
            support::near_allocation::allocateReachablePage(
                nextInstructions,
                imageAddress + imageSize,
                kJumpIslandBytes));
        if (!island) {
            logging::error(
                "DLAA could not allocate one callsite-reachable jump island.");
            return false;
        }
        std::array<std::byte*, 2> stubs{
            island,
            island + kJumpStubStride,
        };
        buildAbsoluteJump(stubs[0], reinterpret_cast<void*>(&hookPreRender));
        buildAbsoluteJump(
            stubs[1],
            reinterpret_cast<void*>(&hookPostImageSpace));
        DWORD oldProtection{};
        if (!VirtualProtect(
                island,
                kJumpIslandBytes,
                PAGE_EXECUTE_READ,
                &oldProtection)) {
            (void)VirtualFree(island, 0, MEM_RELEASE);
            logging::error("DLAA jump-island protection failed.");
            return false;
        }
        FlushInstructionCache(
            GetCurrentProcess(),
            island,
            kJumpIslandBytes);

        executableImage = image;
        executableImageSize = imageSize;
        jumpIsland = island;
        jumpStubs = stubs;
        originalPreRender = reinterpret_cast<PreRenderFunction>(
            image + kPreRenderTargetRva);
        originalPostImageSpace = reinterpret_cast<PostImageSpaceFunction>(
            image + kPostImageSpaceTargetRva);

        std::size_t patched{};
        if (!writeRelativeCall(preCall, stubs[0])) {
            rollbackTransaction(patched);
            logging::error("DLAA pre-render call patch failed.");
            return false;
        }
        ++patched;
        if (!writeRelativeCall(postCall, stubs[1])) {
            rollbackTransaction(patched);
            logging::error("DLAA post-image-space call patch failed.");
            return false;
        }
        ++patched;

        installed.store(true, std::memory_order_release);
        if (!validateEngineHooks("install")) {
            rollbackTransaction(patched);
            logging::error(
                "DLAA engine-hook post-install ownership failed; original engine paths were restored.");
            return false;
        }
        logging::info(
            "Installed validated FO4VR DLAA boundaries (pre-render RVA 0x{:08X}, post-image-space RVA 0x{:08X}); vanilla TAA remains active as the required upstream frame-preparation path.",
            kPreRenderCallsiteRva,
            kPostImageSpaceCallsiteRva);
        return true;
    }

    bool validateEngineHooks(const char* trigger) noexcept
    {
        const auto preOwned = callOwned(
            kPreRenderCallsiteRva,
            0,
            reinterpret_cast<void*>(&hookPreRender));
        const auto postOwned = callOwned(
            kPostImageSpaceCallsiteRva,
            1,
            reinterpret_cast<void*>(&hookPostImageSpace));
        const auto allOwned = installed.load(std::memory_order_acquire) &&
            preOwned && postOwned;
        owned.store(allOwned, std::memory_order_release);
        if (!allOwned) {
            const auto failures = validationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if (failures == 1 || (failures & (failures - 1)) == 0) {
                logging::error(
                    "DLAA engine-hook ownership failed (trigger={}, pre={}, post={}, failures={}); DLAA remains fail-closed.",
                    trigger ? trigger : "unknown",
                    preOwned,
                    postOwned,
                    failures);
            }
        }
        return allOwned;
    }

    EngineHookSnapshot engineHookSnapshot() noexcept
    {
        const auto installedNow = installed.load(std::memory_order_acquire);
        return {
            .installed = installedNow,
            .owned = owned.load(std::memory_order_acquire),
            .preRenderCallOwned = installedNow && callOwned(
                kPreRenderCallsiteRva,
                0,
                reinterpret_cast<void*>(&hookPreRender)),
            .postImageSpaceCallOwned = installedNow && callOwned(
                kPostImageSpaceCallsiteRva,
                1,
                reinterpret_cast<void*>(&hookPostImageSpace)),
            .validationFailures = validationFailures.load(
                std::memory_order_relaxed),
        };
    }
}
