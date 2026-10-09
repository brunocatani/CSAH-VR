#include "Features/dlaa/DlaaD3D11Hooks.h"

#include "Features/dlaa/DlaaRuntime.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace csah::dlaa
{
    namespace
    {
        using MapFunction = HRESULT(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*,
            ID3D11Resource*,
            UINT,
            D3D11_MAP,
            UINT,
            D3D11_MAPPED_SUBRESOURCE*);
        using UnmapFunction = void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*, ID3D11Resource*, UINT);

        constexpr std::size_t kMapVtableIndex = 14;
        constexpr std::size_t kUnmapVtableIndex = 15;

        struct PatchIdentity
        {
            const std::byte* address{};
            const void* destination{};
        };

        MapFunction originalMap{};
        UnmapFunction originalUnmap{};
        void* mapTarget{};
        void* unmapTarget{};
        PatchIdentity mapPatch{};
        PatchIdentity unmapPatch{};
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
                const auto base = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (base > (std::numeric_limits<std::uintptr_t>::max)() -
                        information.RegionSize) {
                    return false;
                }
                const auto end = base + information.RegionSize;
                if (end <= cursor) {
                    return false;
                }
                cursor = (std::min)(finish, end);
            }
            return true;
        }

        [[nodiscard]] bool isExecutableAddress(const void* address) noexcept
        {
            if (!isReadableRange(address, 1)) {
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

        [[nodiscard]] bool addressBelongsToD3D11(const void* address) noexcept
        {
            auto* module = reinterpret_cast<std::byte*>(
                GetModuleHandleW(L"d3d11.dll"));
            if (!module || !isReadableRange(module, sizeof(IMAGE_DOS_HEADER))) {
                return false;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
                return false;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                module + dos->e_lfanew);
            if (!isReadableRange(nt, sizeof(*nt)) ||
                nt->Signature != IMAGE_NT_SIGNATURE) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(module);
            const auto candidate = reinterpret_cast<std::uintptr_t>(address);
            const auto size = static_cast<std::uintptr_t>(
                nt->OptionalHeader.SizeOfImage);
            return candidate >= start && candidate - start < size;
        }

        [[nodiscard]] bool addRelativeDisplacement(
            std::uintptr_t next,
            std::int32_t displacement,
            std::uintptr_t& destination) noexcept
        {
            if (displacement >= 0) {
                const auto positive = static_cast<std::uintptr_t>(displacement);
                if (positive >
                    (std::numeric_limits<std::uintptr_t>::max)() - next) {
                    return false;
                }
                destination = next + positive;
                return true;
            }
            const auto magnitude = static_cast<std::uintptr_t>(
                -static_cast<std::int64_t>(displacement));
            if (magnitude > next) {
                return false;
            }
            destination = next - magnitude;
            return true;
        }

        [[nodiscard]] bool capturePatch(
            const void* target,
            PatchIdentity& identity) noexcept
        {
            identity = {};
            if (!isReadableRange(target, 5)) {
                return false;
            }
            auto* entry = static_cast<const std::byte*>(target);
            auto* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t displacement{};
                std::memcpy(&displacement, entry + 1, sizeof(displacement));
                if (displacement != -7 ||
                    reinterpret_cast<std::uintptr_t>(entry) < 5) {
                    return false;
                }
                patch = entry - 5;
                if (!isReadableRange(patch, 5)) {
                    return false;
                }
            }
            if (patch[0] != std::byte{ 0xE9 }) {
                return false;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, patch + 1, sizeof(displacement));
            std::uintptr_t destination{};
            if (!addRelativeDisplacement(
                    reinterpret_cast<std::uintptr_t>(patch) + 5,
                    displacement,
                    destination) ||
                !isExecutableAddress(
                    reinterpret_cast<const void*>(destination))) {
                return false;
            }
            identity = {
                .address = patch,
                .destination = reinterpret_cast<const void*>(destination),
            };
            return true;
        }

        [[nodiscard]] bool patchOwned(
            const void* target,
            const PatchIdentity& expected) noexcept
        {
            PatchIdentity current{};
            return expected.address && expected.destination &&
                capturePatch(target, current) &&
                current.address == expected.address &&
                current.destination == expected.destination;
        }

        [[nodiscard]] bool disableHook(void* target) noexcept
        {
            const auto result = target ? MH_DisableHook(target) : MH_OK;
            return result == MH_OK || result == MH_ERROR_DISABLED;
        }

        void rollback(bool mapCreated, bool unmapCreated) noexcept
        {
            owned.store(false, std::memory_order_release);
            const auto mapDisabled = !mapCreated || disableHook(mapTarget);
            const auto unmapDisabled = !unmapCreated || disableHook(unmapTarget);
            if (!mapDisabled || !unmapDisabled) {
                logging::critical(
                    "DLAA D3D11 camera-hook rollback could not prove both hooks disabled; resident hooks remain strict pass-through.");
                return;
            }
            if (mapCreated) {
                (void)MH_RemoveHook(mapTarget);
            }
            if (unmapCreated) {
                (void)MH_RemoveHook(unmapTarget);
            }
            originalMap = nullptr;
            originalUnmap = nullptr;
            mapTarget = nullptr;
            unmapTarget = nullptr;
            mapPatch = {};
            unmapPatch = {};
            installed.store(false, std::memory_order_release);
        }

        HRESULT STDMETHODCALLTYPE hookMap(
            ID3D11DeviceContext* context,
            ID3D11Resource* resource,
            UINT subresource,
            D3D11_MAP mapType,
            UINT mapFlags,
            D3D11_MAPPED_SUBRESOURCE* mapped) noexcept
        {
            const auto original = originalMap;
            if (!original) {
                return E_UNEXPECTED;
            }
            const auto result = original(
                context,
                resource,
                subresource,
                mapType,
                mapFlags,
                mapped);
            if (SUCCEEDED(result) && mapped) {
                Runtime::get().onMapSucceeded(
                    context,
                    resource,
                    subresource,
                    mapType,
                    *mapped);
            }
            return result;
        }

        void STDMETHODCALLTYPE hookUnmap(
            ID3D11DeviceContext* context,
            ID3D11Resource* resource,
            UINT subresource) noexcept
        {
            Runtime::get().onBeforeUnmap(context, resource, subresource);
            const auto original = originalUnmap;
            if (original) {
                original(context, resource, subresource);
            }
        }
    }

    bool installD3D11Hooks(ID3D11DeviceContext* context) noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return owned.load(std::memory_order_acquire);
        }
        if (!context) {
            return false;
        }
        auto** vtable = *reinterpret_cast<void***>(context);
        if (!isReadableRange(
                vtable,
                (kUnmapVtableIndex + 1) * sizeof(void*))) {
            logging::error(
                "DLAA rejected an unreadable D3D11 device-context vtable.");
            return false;
        }
        mapTarget = vtable[kMapVtableIndex];
        unmapTarget = vtable[kUnmapVtableIndex];
        if (!isExecutableAddress(mapTarget) ||
            !isExecutableAddress(unmapTarget) ||
            !addressBelongsToD3D11(mapTarget) ||
            !addressBelongsToD3D11(unmapTarget)) {
            logging::error(
                "DLAA rejected non-native D3D11 Map/Unmap method identities.");
            mapTarget = nullptr;
            unmapTarget = nullptr;
            return false;
        }

        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            logging::error(
                "DLAA MinHook initialization failed with status {}.",
                static_cast<int>(status));
            return false;
        }
        bool mapCreated{};
        bool unmapCreated{};
        void* mapTrampoline{};
        status = MH_CreateHook(
            mapTarget,
            reinterpret_cast<void*>(&hookMap),
            &mapTrampoline);
        if (status != MH_OK || !isExecutableAddress(mapTrampoline)) {
            logging::error(
                "DLAA D3D11 Map detour creation failed with status {}.",
                static_cast<int>(status));
            rollback(false, false);
            return false;
        }
        mapCreated = true;
        originalMap = reinterpret_cast<MapFunction>(mapTrampoline);

        void* unmapTrampoline{};
        status = MH_CreateHook(
            unmapTarget,
            reinterpret_cast<void*>(&hookUnmap),
            &unmapTrampoline);
        if (status != MH_OK || !isExecutableAddress(unmapTrampoline)) {
            logging::error(
                "DLAA D3D11 Unmap detour creation failed with status {}.",
                static_cast<int>(status));
            rollback(mapCreated, false);
            return false;
        }
        unmapCreated = true;
        originalUnmap = reinterpret_cast<UnmapFunction>(unmapTrampoline);

        const auto mapQueue = MH_QueueEnableHook(mapTarget);
        const auto unmapQueue = MH_QueueEnableHook(unmapTarget);
        status = mapQueue == MH_OK && unmapQueue == MH_OK ?
            MH_ApplyQueued() :
            MH_ERROR_NOT_EXECUTABLE;
        if (status != MH_OK || !capturePatch(mapTarget, mapPatch) ||
            !capturePatch(unmapTarget, unmapPatch)) {
            logging::error(
                "DLAA D3D11 Map/Unmap activation or ownership failed (mapQueue={}, unmapQueue={}, apply={}).",
                static_cast<int>(mapQueue),
                static_cast<int>(unmapQueue),
                static_cast<int>(status));
            rollback(mapCreated, unmapCreated);
            return false;
        }
        installed.store(true, std::memory_order_release);
        owned.store(true, std::memory_order_release);
        logging::info(
            "Installed isolated native D3D11 Map/Unmap detours for the exact FO4VR stereo camera-buffer transaction.");
        return true;
    }

    bool validateD3D11Hooks(const char* trigger) noexcept
    {
        const auto mapOwned = patchOwned(mapTarget, mapPatch);
        const auto unmapOwned = patchOwned(unmapTarget, unmapPatch);
        const auto allOwned = installed.load(std::memory_order_acquire) &&
            mapOwned && unmapOwned;
        owned.store(allOwned, std::memory_order_release);
        if (!allOwned) {
            const auto failures = validationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if (failures == 1 || (failures & (failures - 1)) == 0) {
                logging::error(
                    "DLAA D3D11 camera-hook ownership failed (trigger={}, map={}, unmap={}, failures={}); DLAA remains fail-closed.",
                    trigger ? trigger : "unknown",
                    mapOwned,
                    unmapOwned,
                    failures);
            }
        }
        return allOwned;
    }

    D3D11HookSnapshot d3d11HookSnapshot() noexcept
    {
        const auto installedNow = installed.load(std::memory_order_acquire);
        return {
            .installed = installedNow,
            .owned = owned.load(std::memory_order_acquire),
            .mapOwned = installedNow && patchOwned(mapTarget, mapPatch),
            .unmapOwned = installedNow && patchOwned(unmapTarget, unmapPatch),
            .validationFailures = validationFailures.load(
                std::memory_order_relaxed),
        };
    }
}
