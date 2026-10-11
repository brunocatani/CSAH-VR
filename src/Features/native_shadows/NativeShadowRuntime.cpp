#include "Features/native_shadows/NativeShadowRuntime.h"

#include "Features/native_shadows/NativeShadowPatchModel.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

#pragma push_macro("MEM_RELEASE")
#undef MEM_RELEASE
#include <RE/Bethesda/Settings.h>
#pragma pop_macro("MEM_RELEASE")

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>
#include <type_traits>

namespace csah::native_shadows
{
    namespace
    {
        using patch_model::BytePatch;

        constexpr std::size_t kSafetyCavePageSize = 512;
        constexpr std::size_t kRelativeJumpSize = 5;
        constexpr std::uint8_t kRelativeJumpOpcode = 0xE9;
        constexpr std::array<std::uint8_t, 13> kNodeAllocatorClearNext{
            0x48, 0x85, 0xD2, 0x74, 0x08,
            0x48, 0xC7, 0x42, 0x40, 0x00, 0x00, 0x00, 0x00,
        };

        struct MovImmediatePatch
        {
            std::uintptr_t address{};
            std::array<std::uint8_t, 8> bytes{};
            std::size_t size{};
            bool alreadyApplied{};
        };

        struct RuntimeState
        {
            Settings settings{};
            bool started{};
            bool earlyContractAccepted{};
            bool cascadeContractAccepted{};
            bool tiledLightingContractAccepted{};
            bool cascadePatchesOwned{};
            bool tiledLightingPatchesOwned{};
            bool safetyCavesOwned{};
            bool safetyCavesReady{};
            bool vrArrayReady{};
            bool setupNodeReady{};
            bool shaderFieldsReady{};
            bool lateCascadeStateReady{};
            bool fullCascadeMaskOwned{};
            bool tiledSettingForced{};
            bool fixedQualityForced{};
            std::uint32_t lateAttempts{};
            std::uint32_t failures{};
            void* cavePage{};
            std::uintptr_t nodeAllocatorCave{};
        };

        std::mutex g_mutex;
        RuntimeState g_state;

        [[nodiscard]] std::uintptr_t moduleBase() noexcept
        {
            return REL::Module::get().base();
        }

        [[nodiscard]] bool addWithoutOverflow(
            const std::uintptr_t value,
            const std::size_t amount,
            std::uintptr_t& result) noexcept
        {
            if (amount >
                std::numeric_limits<std::uintptr_t>::max() - value) {
                return false;
            }
            result = value + amount;
            return true;
        }

        [[nodiscard]] bool isAccessible(
            const std::uintptr_t address,
            const std::size_t bytes,
            const bool requireWrite = false) noexcept
        {
            if (address == 0 || bytes == 0) {
                return false;
            }
            std::uintptr_t end{};
            if (!addWithoutOverflow(address, bytes, end)) {
                return false;
            }

            auto cursor = address;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                if (requireWrite) {
                    const auto protection =
                        information.Protect & 0xFFu;
                    if (protection != PAGE_READWRITE &&
                        protection != PAGE_WRITECOPY &&
                        protection != PAGE_EXECUTE_READWRITE &&
                        protection != PAGE_EXECUTE_WRITECOPY) {
                        return false;
                    }
                }
                const auto region = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                std::uintptr_t regionEnd{};
                if (!addWithoutOverflow(
                        region, information.RegionSize, regionEnd) ||
                    regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(end, regionEnd);
            }
            return true;
        }

        [[nodiscard]] bool matches(
            const std::uintptr_t address,
            const std::span<const std::uint8_t> bytes) noexcept
        {
            return isAccessible(address, bytes.size()) &&
                std::memcmp(
                    reinterpret_cast<const void*>(address),
                    bytes.data(),
                    bytes.size()) == 0;
        }

        [[nodiscard]] bool relativeJumpTarget(
            const std::uintptr_t source,
            std::uintptr_t& target) noexcept
        {
            if (!isAccessible(source, kRelativeJumpSize)) {
                return false;
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(source);
            if (bytes[0] != kRelativeJumpOpcode) {
                return false;
            }

            std::int32_t displacement{};
            std::memcpy(&displacement, bytes + 1, sizeof(displacement));
            std::uintptr_t next{};
            if (!addWithoutOverflow(source, kRelativeJumpSize, next)) {
                return false;
            }
            if (displacement >= 0) {
                return addWithoutOverflow(
                    next,
                    static_cast<std::size_t>(displacement),
                    target);
            }

            const auto magnitude = static_cast<std::size_t>(
                -static_cast<std::int64_t>(displacement));
            if (magnitude > next) {
                return false;
            }
            target = next - magnitude;
            return true;
        }

        [[nodiscard]] bool executableReadOnlyRange(
            const std::uintptr_t address,
            const std::size_t bytes) noexcept
        {
            if (!isAccessible(address, bytes)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(
                    reinterpret_cast<const void*>(address),
                    &information,
                    sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & 0xFFu) != PAGE_EXECUTE_READ) {
                return false;
            }
            const auto region = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress);
            std::uintptr_t regionEnd{};
            std::uintptr_t requestedEnd{};
            return addWithoutOverflow(
                       region, information.RegionSize, regionEnd) &&
                addWithoutOverflow(address, bytes, requestedEnd) &&
                address >= region && requestedEnd <= regionEnd;
        }

        [[nodiscard]] bool verifiedNodeAllocatorPatchLocked(
            const std::uintptr_t entry) noexcept
        {
            constexpr auto displacedPrefix =
                patch_model::kNodeAllocatorSignature.size();
            constexpr auto caveSize = kNodeAllocatorClearNext.size() +
                displacedPrefix + kRelativeJumpSize;
            if (!g_state.safetyCavesReady || !g_state.safetyCavesOwned ||
                !g_state.cavePage || !g_state.nodeAllocatorCave ||
                entry != moduleBase() + patch_model::kNodeAllocatorRva ||
                !isAccessible(entry, displacedPrefix)) {
                return false;
            }

            const auto page = reinterpret_cast<std::uintptr_t>(
                g_state.cavePage);
            const auto cave = g_state.nodeAllocatorCave;
            std::uintptr_t pageEnd{};
            std::uintptr_t caveEnd{};
            if (!addWithoutOverflow(page, kSafetyCavePageSize, pageEnd) ||
                !addWithoutOverflow(cave, caveSize, caveEnd) || cave < page ||
                caveEnd > pageEnd || !executableReadOnlyRange(cave, caveSize)) {
                return false;
            }

            std::uintptr_t entryTarget{};
            if (!relativeJumpTarget(entry, entryTarget) ||
                entryTarget != cave) {
                return false;
            }
            const auto* entryBytes = reinterpret_cast<const std::uint8_t*>(
                entry);
            for (auto index = kRelativeJumpSize; index < displacedPrefix;
                 ++index) {
                if (entryBytes[index] != 0x90) {
                    return false;
                }
            }

            if (!matches(cave, kNodeAllocatorClearNext) ||
                !matches(
                    cave + kNodeAllocatorClearNext.size(),
                    patch_model::kNodeAllocatorSignature)) {
                return false;
            }
            const auto caveReturn = cave + kNodeAllocatorClearNext.size() +
                displacedPrefix;
            std::uintptr_t returnTarget{};
            return relativeJumpTarget(caveReturn, returnTarget) &&
                returnTarget == entry + displacedPrefix;
        }

        [[nodiscard]] std::span<const std::uint8_t> expectedBytes(
            const BytePatch& patch) noexcept
        {
            return { patch.expected.data(), patch.size };
        }

        [[nodiscard]] std::span<const std::uint8_t> replacementBytes(
            const BytePatch& patch) noexcept
        {
            return { patch.replacement.data(), patch.size };
        }

        [[nodiscard]] bool validatePatch(
            const std::uintptr_t base,
            const BytePatch& patch) noexcept
        {
            const auto address = base + patch.rva;
            if (matches(address, replacementBytes(patch)) ||
                matches(address, expectedBytes(patch))) {
                return true;
            }
            logging::error(
                "Native Shadows rejected {} at RVA 0x{:08X}: live bytes match neither the verified FO4VR 1.2.72 signature nor the owned replacement.",
                patch.name,
                patch.rva);
            return false;
        }

        [[nodiscard]] bool writeBytes(
            const std::uintptr_t address,
            const std::span<const std::uint8_t> bytes,
            const char* name) noexcept
        {
            if (!isAccessible(address, bytes.size())) {
                logging::error(
                    "Native Shadows cannot access {} patch destination 0x{:X}.",
                    name,
                    address);
                return false;
            }
            DWORD previous{};
            if (VirtualProtect(
                    reinterpret_cast<void*>(address),
                    bytes.size(),
                    PAGE_EXECUTE_READWRITE,
                    &previous) == FALSE) {
                logging::error(
                    "Native Shadows could not make {} writable; error {}.",
                    name,
                    GetLastError());
                return false;
            }
            std::memcpy(
                reinterpret_cast<void*>(address), bytes.data(), bytes.size());
            DWORD ignored{};
            const auto restored = VirtualProtect(
                reinterpret_cast<void*>(address),
                bytes.size(),
                previous,
                &ignored);
            FlushInstructionCache(
                GetCurrentProcess(),
                reinterpret_cast<const void*>(address),
                bytes.size());
            if (restored == FALSE) {
                logging::error(
                    "Native Shadows wrote {}, but failed to restore page protection; error {}.",
                    name,
                    GetLastError());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool applyPatch(
            const std::uintptr_t base,
            const BytePatch& patch,
            bool& owned) noexcept
        {
            const auto address = base + patch.rva;
            if (matches(address, replacementBytes(patch))) {
                return true;
            }
            if (!matches(address, expectedBytes(patch))) {
                return false;
            }
            if (!writeBytes(address, replacementBytes(patch), patch.name)) {
                return false;
            }
            owned = true;
            return true;
        }

        template <class Value>
        [[nodiscard]] bool readValue(
            const std::uintptr_t address,
            Value& result) noexcept
        {
            static_assert(std::is_trivially_copyable_v<Value>);
            if (!isAccessible(address, sizeof(Value))) {
                return false;
            }
            std::memcpy(&result, reinterpret_cast<const void*>(address),
                sizeof(Value));
            return true;
        }

        template <class Value>
        [[nodiscard]] bool writeValue(
            const std::uintptr_t address,
            const Value& value,
            const char* name) noexcept
        {
            static_assert(std::is_trivially_copyable_v<Value>);
            const auto bytes = std::as_bytes(std::span(&value, 1));
            return writeBytes(
                address,
                { reinterpret_cast<const std::uint8_t*>(bytes.data()),
                    bytes.size() },
                name);
        }

        [[nodiscard]] bool buildMovImmediatePatch(
            const std::uintptr_t base,
            const std::uintptr_t instructionRva,
            MovImmediatePatch& result) noexcept
        {
            const auto address = base + instructionRva;
            if (!isAccessible(address, 8)) {
                return false;
            }
            const auto* live = reinterpret_cast<const std::uint8_t*>(address);
            const auto hasRex = (live[0] & 0xF0u) == 0x40u;
            const auto opcode = hasRex ? 1u : 0u;
            const auto instructionSize = opcode + 6u;

            std::array<std::uint8_t, 8> replacement{};
            if (hasRex) {
                const auto modrm = live[2];
                const auto reg = static_cast<std::uint8_t>((modrm >> 3u) & 7u);
                const auto extended = (live[0] & 0x04u) != 0;
                replacement[0] = extended ? 0x41 : 0x40;
                replacement[1] = static_cast<std::uint8_t>(0xB8u + reg);
                std::memcpy(
                    replacement.data() + 2,
                    &patch_model::kExtendedCascadeCount,
                    sizeof(std::uint32_t));
            } else {
                const auto modrm = live[1];
                const auto reg = static_cast<std::uint8_t>((modrm >> 3u) & 7u);
                replacement[0] = static_cast<std::uint8_t>(0xB8u + reg);
                std::memcpy(
                    replacement.data() + 1,
                    &patch_model::kExtendedCascadeCount,
                    sizeof(std::uint32_t));
            }
            for (auto index = hasRex ? 6u : 5u; index < instructionSize;
                 ++index) {
                replacement[index] = 0x90;
            }

            result.address = address;
            result.bytes = replacement;
            result.size = instructionSize;
            if (std::memcmp(live, replacement.data(), instructionSize) == 0) {
                result.alreadyApplied = true;
                return true;
            }

            if (live[opcode] != 0x8B ||
                (live[opcode + 1] & 0xC7u) != 0x05u) {
                logging::error(
                    "Native Shadows rejected cascade-count read at RVA 0x{:08X}: instruction is not the verified RIP-relative MOV.",
                    instructionRva);
                return false;
            }
            std::int32_t displacement{};
            std::memcpy(
                &displacement, live + opcode + 2, sizeof(displacement));
            const auto target = static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(address + instructionSize) +
                displacement);
            if (target != base + patch_model::kCascadeCountRva) {
                logging::error(
                    "Native Shadows rejected cascade-count read at RVA 0x{:08X}: live target 0x{:X} does not own the verified count global 0x{:X}.",
                    instructionRva,
                    target,
                    base + patch_model::kCascadeCountRva);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool applyMovImmediatePatch(
            const MovImmediatePatch& patch,
            bool& owned) noexcept
        {
            if (patch.alreadyApplied) {
                return true;
            }
            if (!writeBytes(
                    patch.address,
                    { patch.bytes.data(), patch.size },
                    "cascade-count immediate")) {
                return false;
            }
            owned = true;
            return true;
        }

        struct Emitter
        {
            std::uint8_t* page{};
            std::size_t position{};
            std::size_t capacity{};
            bool valid{ true };

            void byte(const std::uint8_t value) noexcept
            {
                if (!valid || position >= capacity) {
                    valid = false;
                    return;
                }
                page[position++] = value;
            }

            void bytes(const std::span<const std::uint8_t> source) noexcept
            {
                if (!valid || source.size() > capacity - position) {
                    valid = false;
                    return;
                }
                std::memcpy(page + position, source.data(), source.size());
                position += source.size();
            }

            void align(const std::size_t alignment) noexcept
            {
                while (valid && position % alignment != 0) {
                    byte(0xCC);
                }
            }

            [[nodiscard]] std::uintptr_t address() const noexcept
            {
                return reinterpret_cast<std::uintptr_t>(page + position);
            }

            void jump(const std::uintptr_t target) noexcept
            {
                byte(0xE9);
                if (!valid) {
                    return;
                }
                const auto next = address() + sizeof(std::int32_t);
                const auto difference = static_cast<std::intptr_t>(target) -
                    static_cast<std::intptr_t>(next);
                if (difference <
                        (std::numeric_limits<std::int32_t>::min)() ||
                    difference >
                        (std::numeric_limits<std::int32_t>::max)()) {
                    valid = false;
                    return;
                }
                const auto relative = static_cast<std::int32_t>(difference);
                bytes({
                    reinterpret_cast<const std::uint8_t*>(&relative),
                    sizeof(relative),
                });
            }
        };

        [[nodiscard]] std::uintptr_t emitZeroInitCave(
            Emitter& emitter,
            const std::uintptr_t base) noexcept
        {
            emitter.align(16);
            const auto result = emitter.address();
            constexpr std::array<std::uint8_t, 73> prefix{
                0x51,
                0x4A, 0x8D, 0x8C, 0x10, 0x90, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x08, 0x00, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x10, 0x00, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x18, 0x00, 0x00, 0x00, 0x00,
                0x4A, 0x8D, 0x8C, 0x10, 0x30, 0x01, 0x00, 0x00,
                0x48, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x08, 0x00, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x10, 0x00, 0x00, 0x00, 0x00,
                0x48, 0xC7, 0x41, 0x18, 0x00, 0x00, 0x00, 0x00,
                0x59,
            };
            emitter.bytes(prefix);
            emitter.bytes(patch_model::kZeroInitSignature);
            emitter.jump(
                base + patch_model::kZeroInitRva +
                patch_model::kZeroInitSignature.size());
            return result;
        }

        [[nodiscard]] std::uintptr_t emitNullSafetyCave(
            Emitter& emitter,
            const std::uintptr_t base) noexcept
        {
            emitter.align(16);
            const auto result = emitter.address();
            constexpr std::array<std::uint8_t, 5> prefix{
                0x4D, 0x85, 0xD2, 0x74, 0x13,
            };
            constexpr std::array<std::uint8_t, 7> checks{
                0x48, 0x85, 0xED, 0x74, 0x02, 0x78, 0x05,
            };
            emitter.bytes(prefix);
            emitter.bytes(patch_model::kNullSafetySignature);
            emitter.bytes(checks);
            emitter.jump(
                base + patch_model::kNullSafetyRva +
                patch_model::kNullSafetySignature.size());
            emitter.byte(0x31);
            emitter.byte(0xED);
            emitter.jump(
                base + patch_model::kNullSafetyRva +
                patch_model::kNullSafetySignature.size());
            return result;
        }

        [[nodiscard]] std::uintptr_t emitNodeAllocatorCave(
            Emitter& emitter,
            const std::uintptr_t base) noexcept
        {
            emitter.align(16);
            const auto result = emitter.address();
            emitter.bytes(kNodeAllocatorClearNext);
            emitter.bytes(patch_model::kNodeAllocatorSignature);
            emitter.jump(
                base + patch_model::kNodeAllocatorRva +
                patch_model::kNodeAllocatorSignature.size());
            return result;
        }

        [[nodiscard]] std::uintptr_t emitPointerValidationCave(
            Emitter& emitter,
            const std::uintptr_t base) noexcept
        {
            emitter.align(16);
            const auto result = emitter.address();
            constexpr std::array<std::uint8_t, 25> validation{
                0x4D, 0x85, 0xF6, 0x74, 0x25,
                0x50,
                0x4C, 0x89, 0xF0,
                0x48, 0xC1, 0xE8, 0x2F,
                0x85, 0xC0,
                0x75, 0x0D,
                0x44, 0x89, 0xF0,
                0x85, 0xC0,
                0x74, 0x06,
                0x58,
            };
            emitter.bytes(validation);
            emitter.jump(base + patch_model::kPointerValidationContinueRva);
            constexpr std::array<std::uint8_t, 12> selfHeal{
                0x58,
                0x49, 0xC7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00,
                0x45, 0x31, 0xF6,
            };
            emitter.bytes(selfHeal);
            emitter.jump(base + patch_model::kPointerValidationSkipRva);
            return result;
        }

        template <std::size_t Size>
        [[nodiscard]] bool crashSiteIsOriginalOrOwned(
            const std::uintptr_t address,
            const std::array<std::uint8_t, Size>& signature) noexcept
        {
            if (!isAccessible(address, Size)) {
                return false;
            }
            const auto* live = reinterpret_cast<const std::uint8_t*>(address);
            return std::memcmp(live, signature.data(), Size) == 0 ||
                live[0] == 0xE9;
        }

        [[nodiscard]] bool patchJumpSite(
            const std::uintptr_t source,
            const std::size_t size,
            const std::uintptr_t destination,
            const char* name) noexcept
        {
            if (!isAccessible(source, size) || size < 5) {
                return false;
            }
            if (*reinterpret_cast<const std::uint8_t*>(source) == 0xE9) {
                logging::warn(
                    "Native Shadows found a pre-existing jump at {}; accepting external safety ownership without rewriting it.",
                    name);
                return true;
            }
            std::array<std::uint8_t, 9> patch{};
            patch.fill(0x90);
            patch[0] = 0xE9;
            const auto difference = static_cast<std::intptr_t>(destination) -
                static_cast<std::intptr_t>(source + 5);
            if (difference <
                    (std::numeric_limits<std::int32_t>::min)() ||
                difference >
                    (std::numeric_limits<std::int32_t>::max)()) {
                return false;
            }
            const auto relative = static_cast<std::int32_t>(difference);
            std::memcpy(patch.data() + 1, &relative, sizeof(relative));
            return writeBytes(source, { patch.data(), size }, name);
        }

        [[nodiscard]] bool installSafetyCaves(
            const std::uintptr_t base,
            bool& owned) noexcept
        {
            const auto zero = base + patch_model::kZeroInitRva;
            const auto nullSafety = base + patch_model::kNullSafetyRva;
            const auto node = base + patch_model::kNodeAllocatorRva;
            const auto pointer = base + patch_model::kPointerValidationRva;

            if (!crashSiteIsOriginalOrOwned(
                    zero, patch_model::kZeroInitSignature) ||
                !crashSiteIsOriginalOrOwned(
                    nullSafety, patch_model::kNullSafetySignature) ||
                !crashSiteIsOriginalOrOwned(
                    node, patch_model::kNodeAllocatorSignature) ||
                !crashSiteIsOriginalOrOwned(
                    pointer, patch_model::kPointerValidationSignature)) {
                logging::error(
                    "Native Shadows rejected the four-cascade safety contract because at least one verified crash-prevention site has unknown live bytes.");
                return false;
            }

            const auto externallyOwned =
                *reinterpret_cast<const std::uint8_t*>(zero) == 0xE9 ||
                *reinterpret_cast<const std::uint8_t*>(nullSafety) == 0xE9 ||
                *reinterpret_cast<const std::uint8_t*>(node) == 0xE9 ||
                *reinterpret_cast<const std::uint8_t*>(pointer) == 0xE9;
            if (externallyOwned) {
                const auto allExternal =
                    *reinterpret_cast<const std::uint8_t*>(zero) == 0xE9 &&
                    *reinterpret_cast<const std::uint8_t*>(nullSafety) == 0xE9 &&
                    *reinterpret_cast<const std::uint8_t*>(node) == 0xE9 &&
                    *reinterpret_cast<const std::uint8_t*>(pointer) == 0xE9;
                if (!allExternal) {
                    logging::error(
                        "Native Shadows found partial external safety ownership and will not combine unknown code caves with its verified transaction.");
                    return false;
                }
                return true;
            }

            constexpr std::array<std::uintptr_t, 4> returnSites{
                patch_model::kZeroInitRva +
                    patch_model::kZeroInitSignature.size(),
                patch_model::kNullSafetyRva +
                    patch_model::kNullSafetySignature.size(),
                patch_model::kNodeAllocatorRva +
                    patch_model::kNodeAllocatorSignature.size(),
                patch_model::kPointerValidationRva +
                    patch_model::kPointerValidationSignature.size(),
            };
            std::array<std::uintptr_t, returnSites.size()> absoluteSites{};
            for (std::size_t index = 0; index < returnSites.size(); ++index) {
                absoluteSites[index] = base + returnSites[index];
            }
            auto* page = static_cast<std::uint8_t*>(
                support::near_allocation::allocateReachablePage(
                    absoluteSites,
                    base + 0x02800000,
                    kSafetyCavePageSize));
            if (!page) {
                logging::error(
                    "Native Shadows could not reserve one rel32-reachable page for the cascade safety transaction.");
                return false;
            }

            Emitter emitter{ .page = page, .capacity = kSafetyCavePageSize };
            const auto zeroCave = emitZeroInitCave(emitter, base);
            const auto nullCave = emitNullSafetyCave(emitter, base);
            const auto nodeCave = emitNodeAllocatorCave(emitter, base);
            const auto pointerCave = emitPointerValidationCave(emitter, base);
            if (!emitter.valid) {
                (void)VirtualFree(page, 0, MEM_RELEASE);
                logging::error(
                    "Native Shadows failed to construct the bounded cascade safety page.");
                return false;
            }

            DWORD previous{};
            if (VirtualProtect(
                    page,
                    kSafetyCavePageSize,
                    PAGE_EXECUTE_READ,
                    &previous) == FALSE) {
                (void)VirtualFree(page, 0, MEM_RELEASE);
                logging::error(
                    "Native Shadows could not seal the cascade safety page executable; error {}.",
                    GetLastError());
                return false;
            }
            FlushInstructionCache(GetCurrentProcess(), page, emitter.position);

            const auto installed =
                patchJumpSite(
                    zero,
                    patch_model::kZeroInitSignature.size(),
                    zeroCave,
                    "cascade entry zero-initialization") &&
                patchJumpSite(
                    nullSafety,
                    patch_model::kNullSafetySignature.size(),
                    nullCave,
                    "cascade null safety") &&
                patchJumpSite(
                    node,
                    patch_model::kNodeAllocatorSignature.size(),
                    nodeCave,
                    "cascade node reuse cleanup") &&
                patchJumpSite(
                    pointer,
                    patch_model::kPointerValidationSignature.size(),
                    pointerCave,
                    "cascade pointer validation");
            if (!installed) {
                logging::error(
                    "Native Shadows could not complete the four-site safety transaction; extended cascades remain fail-closed.");
                return false;
            }
            g_state.cavePage = page;
            g_state.nodeAllocatorCave = nodeCave;
            owned = true;
            logging::info(
                "Native Shadows installed four verified cascade safety caves in one sealed process-lifetime page ({} bytes used).",
                emitter.position);
            return true;
        }

        [[nodiscard]] bool applyTiledLightingPatches(
            const std::uintptr_t base) noexcept
        {
            for (const auto& patch : patch_model::kTiledLightingPatches) {
                if (!validatePatch(base, patch)) {
                    return false;
                }
            }
            bool owned{};
            for (const auto& patch : patch_model::kTiledLightingPatches) {
                if (!applyPatch(base, patch, owned)) {
                    return false;
                }
            }
            g_state.tiledLightingPatchesOwned = owned;
            logging::info(
                "Native Shadows {} all five fixed tiled-deferred-lighting VR gates.",
                owned ? "now owns" : "accepted existing ownership of");
            return true;
        }

        [[nodiscard]] bool applyCascadePatches(
            const std::uintptr_t base) noexcept
        {
            std::array<MovImmediatePatch,
                patch_model::kCascadeCountReadRvas.size()>
                countReads{};
            for (std::size_t index = 0; index < countReads.size(); ++index) {
                if (!buildMovImmediatePatch(
                        base,
                        patch_model::kCascadeCountReadRvas[index],
                        countReads[index])) {
                    return false;
                }
            }
            for (const auto& patch : patch_model::kCascadeScalarPatches) {
                if (!validatePatch(base, patch)) {
                    return false;
                }
            }
            for (const auto& patch : patch_model::kSafeMaskPatches) {
                if (!validatePatch(base, patch)) {
                    return false;
                }
            }

            bool safetyOwned{};
            if (!installSafetyCaves(base, safetyOwned)) {
                return false;
            }
            g_state.safetyCavesOwned = safetyOwned;
            g_state.safetyCavesReady = true;

            bool cascadeOwned{};
            for (const auto& patch : patch_model::kSafeMaskPatches) {
                if (!applyPatch(base, patch, cascadeOwned)) {
                    return false;
                }
            }
            for (const auto& patch : countReads) {
                if (!applyMovImmediatePatch(patch, cascadeOwned)) {
                    return false;
                }
            }
            for (const auto& patch : patch_model::kCascadeScalarPatches) {
                if (!applyPatch(base, patch, cascadeOwned)) {
                    return false;
                }
            }
            if (!writeValue(
                    base + patch_model::kCascadeCountRva,
                    patch_model::kExtendedCascadeCount,
                    "cascade count global") ||
                !writeValue(
                    base + patch_model::kCascadeDistanceRva,
                    g_state.settings.directionalShadowDistance,
                    "fixed cascade distance")) {
                return false;
            }
            g_state.cascadePatchesOwned = cascadeOwned;
            logging::info(
                "Native Shadows enabled the verified four-cascade allocation contract with safe two-cascade masks until all late arrays are proven ready; fixed distance={:.1f}.",
                g_state.settings.directionalShadowDistance);
            return true;
        }

        [[nodiscard]] bool forceFixedDistance(
            const std::uintptr_t base) noexcept
        {
            if (!g_state.settings.enabled) {
                return true;
            }
            float rendererDistance{};
            if (!readValue(base + patch_model::kRendererDistanceRva, rendererDistance) ||
                !std::isfinite(rendererDistance) || rendererDistance <= 0.0f) {
                logging::error("Native Shadows could not read a valid renderer distance; paired distance update rejected.");
                return false;
            }
            return patch_model::writeFixedDistances(
                g_state.settings.directionalShadowDistance, rendererDistance,
                [base](std::uintptr_t rva, float value) noexcept {
                    return writeValue(base + rva, value, "fixed paired shadow distance");
                });
        }

        [[nodiscard]] bool forceTiledSetting() noexcept
        {
            if (!g_state.settings.enabled ||
                !g_state.settings.tiledDeferredLighting) {
                return true;
            }
            auto* setting = RE::GetINISetting(
                "bComputeShaderDeferredTiledLighting:Display");
            if (!setting) {
                logging::error(
                    "Native Shadows could not resolve bComputeShaderDeferredTiledLighting:Display after GameDataReady.");
                return false;
            }
            setting->SetInt(1);
            g_state.tiledSettingForced = setting->GetInt() != 0;
            return g_state.tiledSettingForced;
        }

        [[nodiscard]] bool forceFixedShadowQuality(
            const std::uintptr_t base) noexcept
        {
            if (!g_state.settings.enabled) {
                return true;
            }

            auto* blendSetting =
                RE::GetINISetting("fBlendSplitDirShadow:Display");
            auto* filterSetting =
                RE::GetINISetting("uiOrthoShadowFilter:Display");
            if (!blendSetting || !filterSetting) {
                logging::error(
                    "Native Shadows could not resolve the directional cascade blend or orthographic filter settings after GameDataReady.");
                return false;
            }

            const auto state = base + patch_model::kShadowRendererStateRva;
            const auto blendAddress =
                state + patch_model::kCachedCascadeBlendOffset;
            const auto filterAddress =
                state + patch_model::kCachedOrthoShadowFilterOffset;
            float previousBlend{};
            std::uint32_t previousFilter{};
            if (!readValue(blendAddress, previousBlend) ||
                !readValue(filterAddress, previousFilter) ||
                !std::isfinite(previousBlend) || previousBlend < 0.0f ||
                previousFilter > 5) {
                logging::error(
                    "Native Shadows rejected the cached close-shadow quality state because its verified FO4VR fields were inaccessible or outside engine bounds.");
                return false;
            }

            const auto previousSettingBlend = blendSetting->GetFloat();
            const auto previousSettingFilter = filterSetting->GetUInt();
            const auto restore = [&]() noexcept {
                blendSetting->SetFloat(previousSettingBlend);
                filterSetting->SetUInt(previousSettingFilter);
                const auto blendRestored = writeValue(
                    blendAddress,
                    previousBlend,
                    "restored cached cascade blend distance");
                const auto filterRestored = writeValue(
                    filterAddress,
                    previousFilter,
                    "restored cached orthographic shadow filter");
                if (!blendRestored || !filterRestored) {
                    logging::critical(
                        "Native Shadows could not fully restore cached close-shadow quality state after a rejected transaction.");
                }
            };

            if (!writeValue(
                    blendAddress,
                    g_state.settings.cascadeBlendDistance,
                    "cached cascade blend distance")) {
                restore();
                return false;
            }
            if (!writeValue(
                    filterAddress,
                    g_state.settings.orthographicShadowFilter,
                    "cached orthographic shadow filter")) {
                restore();
                return false;
            }
            blendSetting->SetFloat(g_state.settings.cascadeBlendDistance);
            filterSetting->SetUInt(
                g_state.settings.orthographicShadowFilter);

            float observedBlend{};
            std::uint32_t observedFilter{};
            const auto applied =
                blendSetting->GetFloat() ==
                    g_state.settings.cascadeBlendDistance &&
                filterSetting->GetUInt() ==
                    g_state.settings.orthographicShadowFilter &&
                readValue(blendAddress, observedBlend) &&
                readValue(filterAddress, observedFilter) &&
                observedBlend == g_state.settings.cascadeBlendDistance &&
                observedFilter ==
                    g_state.settings.orthographicShadowFilter;
            if (!applied) {
                restore();
                logging::error(
                    "Native Shadows could not verify the fixed close-shadow quality values after writing both engine settings and cached renderer state.");
                return false;
            }

            if (!g_state.fixedQualityForced) {
                logging::info(
                    "Native Shadows corrected close-shadow quality parity: cascade blend {:.1f}->{:.1f}, orthographic filter {}->{}; fixed startup values only.",
                    previousBlend,
                    observedBlend,
                    previousFilter,
                    observedFilter);
            }
            g_state.fixedQualityForced = true;
            return true;
        }

        [[nodiscard]] bool prepareVrArray(
            const std::uintptr_t base) noexcept
        {
            if (g_state.vrArrayReady) {
                return true;
            }
            const auto container = base + patch_model::kVrArrayRva;
            const auto countAddress = base + patch_model::kVrArrayCountRva;
            std::uintptr_t buffer{};
            std::uint32_t capacity{};
            std::uint32_t count{};
            if (!readValue(container, buffer) ||
                !readValue(container + 8, capacity) ||
                !readValue(countAddress, count) || buffer == 0) {
                return false;
            }
            if (capacity < patch_model::kExtendedCascadeCount) {
                logging::error(
                    "Native Shadows late cascade array has capacity {} instead of 4. The module will not replace an engine-owned buffer; full masks remain fail-closed.",
                    capacity);
                return false;
            }
            constexpr auto required =
                patch_model::kVrEntrySize *
                patch_model::kExtendedCascadeCount;
            if (!isAccessible(buffer, required, true)) {
                logging::error(
                    "Native Shadows rejected the late VR cascade buffer because its four-entry range is not writable.");
                return false;
            }

            const auto initializeEntry = [buffer](const std::uint32_t index) {
                auto* destination = reinterpret_cast<std::uint8_t*>(
                    buffer + index * patch_model::kVrEntrySize);
                const auto entryAddress = reinterpret_cast<std::uintptr_t>(
                    destination);
                auto initialized = true;
                for (const auto offset : patch_model::kVrPoolOffsets) {
                    std::uintptr_t tail{};
                    std::memcpy(
                        &tail, destination + offset + 8, sizeof(tail));
                    if (tail != entryAddress + offset) {
                        initialized = false;
                        break;
                    }
                }
                if (initialized) {
                    return;
                }
                std::memcpy(
                    destination,
                    reinterpret_cast<const void*>(buffer),
                    patch_model::kVrEntrySize);
                std::uint32_t zero{};
                std::memcpy(destination, &zero, sizeof(zero));
                std::memcpy(destination + 4, &zero, sizeof(zero));
                for (const auto offset : patch_model::kVrPoolOffsets) {
                    const std::uintptr_t head{};
                    const auto tail = entryAddress + offset;
                    std::memcpy(
                        destination + offset, &head, sizeof(head));
                    std::memcpy(
                        destination + offset + 8, &tail, sizeof(tail));
                }
            };

            initializeEntry(2);
            initializeEntry(3);
            for (std::uint32_t index = 0; index < 2; ++index) {
                auto* entry = reinterpret_cast<std::uint8_t*>(
                    buffer + index * patch_model::kVrEntrySize);
                for (const auto offset : patch_model::kVrPoolOffsets) {
                    std::uintptr_t tail{};
                    std::memcpy(&tail, entry + offset + 8, sizeof(tail));
                    if (tail == 0) {
                        tail = reinterpret_cast<std::uintptr_t>(entry + offset);
                        std::memcpy(
                            entry + offset + 8, &tail, sizeof(tail));
                    }
                }
            }
            if (count < patch_model::kExtendedCascadeCount &&
                !writeValue(
                    countAddress,
                    patch_model::kExtendedCascadeCount,
                    "VR cascade array count")) {
                return false;
            }
            g_state.vrArrayReady = true;
            logging::info(
                "Native Shadows verified a four-entry engine-owned VR cascade buffer (capacity={}, previousCount={}) without reallocating it.",
                capacity,
                count);
            return true;
        }

        [[nodiscard]] bool resolveCascadeGroup(
            const std::uintptr_t sceneNode,
            std::uintptr_t& cascadeGroup) noexcept
        {
            return sceneNode != 0 &&
                readValue(
                    sceneNode + patch_model::kCascadeGroupOffset,
                    cascadeGroup) &&
                cascadeGroup != 0;
        }

        [[nodiscard]] bool prepareSceneNodes(
            const std::uintptr_t base,
            std::uintptr_t& cascadeGroup) noexcept
        {
            std::uintptr_t renderNode{};
            std::uintptr_t setupNode{};
            if (!readValue(
                    base + patch_model::kRenderSceneNodeRva, renderNode) ||
                renderNode == 0) {
                return false;
            }
            if (!readValue(
                    base + patch_model::kSetupSceneNodeRva, setupNode)) {
                return false;
            }
            if (setupNode == 0) {
                if (!writeValue(
                        base + patch_model::kSetupSceneNodeRva,
                        renderNode,
                        "shadow setup scene node")) {
                    return false;
                }
                setupNode = renderNode;
                logging::info(
                    "Native Shadows supplied the missing VR setup scene node from the verified render scene node.");
            }
            g_state.setupNodeReady = setupNode != 0;
            return resolveCascadeGroup(renderNode, cascadeGroup);
        }

        [[nodiscard]] bool prepareShaderFields(
            const std::uintptr_t cascadeGroup) noexcept
        {
            if (!writeValue(
                    cascadeGroup + patch_model::kCascadeVrFlagOffset,
                    std::uint8_t{ 1 },
                    "cascade-group VR flag")) {
                return false;
            }
            std::uintptr_t shader{};
            if (!readValue(
                    cascadeGroup + patch_model::kShaderObjectOffset,
                    shader) ||
                shader == 0) {
                return false;
            }
            if (!writeValue(
                    shader + patch_model::kShaderStoredCountOffset,
                    std::uint32_t{ 4 },
                    "cascade shader stored count") ||
                !writeValue(
                    shader + patch_model::kShaderArrayCapacityOffset,
                    std::uint16_t{ 4 },
                    "cascade shader array capacity") ||
                !writeValue(
                    shader + patch_model::kShaderArrayCountOffset,
                    std::uint16_t{ 4 },
                    "cascade shader array count")) {
                return false;
            }
            g_state.shaderFieldsReady = true;
            return true;
        }

        [[nodiscard]] bool flatCascadeArrayReady(
            const std::uintptr_t cascadeGroup,
            std::uintptr_t& flatBuffer) noexcept
        {
            std::uint32_t flatCount{};
            if (!readValue(
                    cascadeGroup + patch_model::kFlatCountOffset,
                    flatCount) ||
                !readValue(
                    cascadeGroup + patch_model::kFlatBufferOffset,
                    flatBuffer) ||
                flatCount < patch_model::kExtendedCascadeCount ||
                flatBuffer == 0 ||
                !isAccessible(
                    flatBuffer,
                    patch_model::kFlatEntrySize *
                        patch_model::kExtendedCascadeCount,
                    true)) {
                return false;
            }
            for (std::uint32_t index = 0;
                 index < patch_model::kExtendedCascadeCount;
                 ++index) {
                std::uintptr_t shadowMap{};
                if (!readValue(
                        flatBuffer + index * patch_model::kFlatEntrySize +
                            patch_model::kFlatShadowMapOffset,
                        shadowMap) ||
                    shadowMap == 0) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool activateFullCascadeMasks(
            const std::uintptr_t base,
            const std::uintptr_t flatBuffer) noexcept
        {
            if (!writeValue(
                    flatBuffer + 3 * patch_model::kFlatEntrySize +
                        patch_model::kFlatLastCascadeOffset,
                    std::uint8_t{ 0 },
                    "far-cascade terminal flag")) {
                return false;
            }
            for (const auto& patch : patch_model::kFullMaskPatches) {
                if (!validatePatch(base, patch)) {
                    return false;
                }
            }
            bool owned{};
            for (const auto& patch : patch_model::kFullMaskPatches) {
                if (!applyPatch(base, patch, owned)) {
                    return false;
                }
            }
            g_state.fullCascadeMaskOwned = owned;
            return true;
        }

        void attemptLateCascadeInitialization(
            const char* boundary) noexcept
        {
            if (!g_state.settings.enabled ||
                !g_state.settings.extendedDirectionalCascades ||
                g_state.lateCascadeStateReady) {
                return;
            }
            ++g_state.lateAttempts;
            const auto base = moduleBase();
            std::uintptr_t cascadeGroup{};
            std::uintptr_t flatBuffer{};
            if (!prepareVrArray(base) ||
                !prepareSceneNodes(base, cascadeGroup) ||
                !prepareShaderFields(cascadeGroup) ||
                !flatCascadeArrayReady(cascadeGroup, flatBuffer)) {
                logging::info(
                    "Native Shadows late four-cascade state is not ready at {} (attempt {}); safe two-cascade masks remain active until the next deterministic lifecycle boundary.",
                    boundary,
                    g_state.lateAttempts);
                return;
            }
            if (!activateFullCascadeMasks(base, flatBuffer)) {
                ++g_state.failures;
                logging::error(
                    "Native Shadows could not activate full four-cascade masks at {}; safe masks remain active.",
                    boundary);
                return;
            }
            g_state.lateCascadeStateReady = true;
            logging::info(
                "Native Shadows four-cascade runtime is active at {}: all four cascades render every frame; fixed distance={:.1f}; no FPS controller or adaptive-quality path exists.",
                boundary,
                g_state.settings.directionalShadowDistance);
        }

        [[nodiscard]] RuntimeSnapshot snapshotLocked() noexcept
        {
            RuntimeSnapshot result{
                .settings = g_state.settings,
                .earlyContractAccepted = g_state.earlyContractAccepted,
                .cascadePatchesOwned = g_state.cascadePatchesOwned,
                .tiledLightingPatchesOwned =
                    g_state.tiledLightingPatchesOwned,
                .safetyCavesOwned = g_state.safetyCavesOwned,
                .lateCascadeStateReady = g_state.lateCascadeStateReady,
                .fullCascadeMaskOwned = g_state.fullCascadeMaskOwned,
                .tiledSettingForced = g_state.tiledSettingForced,
                .fixedQualityForced = g_state.fixedQualityForced,
                .lateAttempts = g_state.lateAttempts,
                .failures = g_state.failures,
            };
            if (!g_state.started) {
                return result;
            }
            const auto base = moduleBase();
            (void)readValue(
                base + patch_model::kCascadeCountRva,
                result.observedCascadeCount);
            (void)readValue(
                base + patch_model::kShadowResolutionRva,
                result.observedShadowResolution);
            (void)readValue(
                base + patch_model::kCascadeDistanceRva,
                result.observedCascadeDistance);
            (void)readValue(
                base + patch_model::kRendererDistanceRva,
                result.observedRendererDistance);
            const auto shadowState =
                base + patch_model::kShadowRendererStateRva;
            (void)readValue(
                shadowState + patch_model::kCachedCascadeBlendOffset,
                result.observedCascadeBlendDistance);
            (void)readValue(
                shadowState + patch_model::kCachedOrthoShadowFilterOffset,
                result.observedOrthographicShadowFilter);
            return result;
        }
    }

    bool startRuntime(const Settings& settings) noexcept
    {
        std::scoped_lock lock(g_mutex);
        if (g_state.started) {
            logging::error(
                "Native Shadows runtime start was requested more than once.");
            return false;
        }
        g_state.started = true;
        g_state.settings = sanitize(settings);
        if (!g_state.settings.enabled) {
            logging::info(
                "Native Shadows is disabled; no engine bytes, distances, or settings were changed.");
            g_state.earlyContractAccepted = true;
            return true;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            ++g_state.failures;
            logging::critical(
                "Native Shadows rejected a non-FO4VR-1.2.72 module before touching engine state.");
            return false;
        }

        const auto base = moduleBase();
        // The tiled-lighting gates and extended cascades are independent
        // transactions. FO4VR still needs the verified tiled path when an
        // extended-cascade identity is unavailable.
        auto cascadeAccepted = true;
        if (g_state.settings.extendedDirectionalCascades) {
            cascadeAccepted = applyCascadePatches(base);
        }
        auto tiledAccepted = true;
        if (g_state.settings.tiledDeferredLighting) {
            tiledAccepted = applyTiledLightingPatches(base);
        }
        g_state.cascadeContractAccepted = cascadeAccepted;
        g_state.tiledLightingContractAccepted = tiledAccepted;
        const auto accepted = cascadeAccepted && tiledAccepted;
        g_state.earlyContractAccepted = accepted;
        if (!accepted) {
            ++g_state.failures;
            logging::error(
                "Native Shadows early contract result: tiledLighting={}, extendedCascades={}. Rejected transactions and their later mutations remain inactive.",
                tiledAccepted,
                cascadeAccepted);
        }
        return accepted;
    }

    void onGameDataReady() noexcept
    {
        std::scoped_lock lock(g_mutex);
        if (!g_state.started || !g_state.settings.enabled) {
            return;
        }
        if (g_state.settings.tiledDeferredLighting &&
            g_state.tiledLightingContractAccepted &&
            !forceTiledSetting()) {
            ++g_state.failures;
        }
        if (!g_state.cascadeContractAccepted) {
            logging::info(
                "Native Shadows is running the verified tiled two-cascade path; rejected extended-cascade settings, distances, scene nodes, arrays, shader fields, and masks remain untouched.");
            return;
        }
        if (!forceFixedShadowQuality(moduleBase())) {
            ++g_state.failures;
        }
        if (!forceFixedDistance(moduleBase())) {
            ++g_state.failures;
        }
        attemptLateCascadeInitialization("GameDataReady");
        const auto current = snapshotLocked();
        logging::info(
            "Native Shadows GameDataReady audit: resolution={}x{}, cascades={}, cascadeDistance={:.1f}, rendererDistance={:.1f}, cascadeBlend={:.1f}, orthoFilter={}, tiledSetting={}, fixedQuality={}, lateReady={}, failures={}.",
            current.observedShadowResolution,
            current.observedShadowResolution,
            current.observedCascadeCount,
            current.observedCascadeDistance,
            current.observedRendererDistance,
            current.observedCascadeBlendDistance,
            current.observedOrthographicShadowFilter,
            current.tiledSettingForced,
            current.fixedQualityForced,
            current.lateCascadeStateReady,
            current.failures);
    }

    void onWorldReady(const char* boundary) noexcept
    {
        std::scoped_lock lock(g_mutex);
        if (!g_state.started || !g_state.settings.enabled ||
            !g_state.cascadeContractAccepted) {
            return;
        }
        if (!forceFixedDistance(moduleBase())) {
            ++g_state.failures;
        }
        if (!forceFixedShadowQuality(moduleBase())) {
            ++g_state.failures;
        }
        attemptLateCascadeInitialization(boundary ? boundary : "WorldReady");
    }

    std::size_t verifiedNodeAllocatorPatchPrefix(const void* entry) noexcept
    {
        std::scoped_lock lock(g_mutex);
        return verifiedNodeAllocatorPatchLocked(
                   reinterpret_cast<std::uintptr_t>(entry)) ?
            patch_model::kNodeAllocatorSignature.size() :
            0;
    }

    RuntimeSnapshot snapshot() noexcept
    {
        std::scoped_lock lock(g_mutex);
        return snapshotLocked();
    }
}
