#include "PCH.h"
#include "render/NativeEmissionRead.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

#include <Windows.h>
#include <limits>
#include <memory>

namespace csah::render::native_emission_read
{
    namespace
    {
        constexpr std::array<float, 3> zeroColor{};
        struct ReleasePage
        {
            void operator()(void* page) const noexcept { if (page) VirtualFree(page, 0, MEM_RELEASE); }
        };
        // Startup owner only. Installed native patches and sealed code have
        // process lifetime, matching F4SE's non-unloading plugin lifecycle.
        std::unique_ptr<void, ReleasePage> installedPage;
    }

    bool install() noexcept
    {
        if (installedPage) return true;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!base) return false;
        std::array<std::uintptr_t, sites.size()> next{};
        for (std::size_t i = 0; i < sites.size(); ++i) {
            std::array<std::uint8_t, load.size()> actual{};
            SIZE_T copied{};
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + sites[i]),
                    actual.data(), actual.size(), &copied) || copied != actual.size() || actual != load) {
                logging::error("Native emission read compatibility rejected changed code at RVA 0x{:X}", sites[i]);
                return false;
            }
            next[i] = base + sites[i] + 5;
        }
        std::unique_ptr<void, ReleasePage> page{
            support::near_allocation::allocateReachablePage(next, next[0], sites.size() * stride)};
        if (!page) {
            logging::error("Native emission read compatibility could not allocate reachable code");
            return false;
        }
        std::array<std::array<std::uint8_t, load.size()>, sites.size()> patches{};
        for (std::size_t i = 0; i < sites.size(); ++i) {
            auto* destination = static_cast<std::uint8_t*>(page.get()) + stride * i;
            const auto delta = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination)) -
                static_cast<std::int64_t>(next[i]);
            if (delta < (std::numeric_limits<std::int32_t>::min)() || delta > (std::numeric_limits<std::int32_t>::max)()) return false;
            const auto bytes = thunk(reinterpret_cast<std::uintptr_t>(zeroColor.data()), base + sites[i] + load.size());
            std::memcpy(destination, bytes.data(), bytes.size());
            patches[i] = {0xE9,0,0,0,0,0x90,0x90};
            const auto relative = static_cast<std::int32_t>(delta);
            std::memcpy(patches[i].data() + 1, &relative, sizeof(relative));
        }
        DWORD oldPageProtection{};
        if (!VirtualProtect(page.get(), sites.size() * stride, PAGE_EXECUTE_READ, &oldPageProtection)) {
            logging::error("Native emission read code sealing failed: {}", GetLastError());
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), page.get(), sites.size() * stride);

        // All three loads occupy the same native code page. Acquire write
        // permission once so a partial protection failure cannot leave a mix
        // of guarded and unguarded channel producers or restore PAGE_RWX.
        const auto extent = sites.back() - sites.front() + load.size();
        auto* first = reinterpret_cast<void*>(base + sites.front());
        DWORD originalProtection{};
        if (!VirtualProtect(first, extent, PAGE_EXECUTE_READWRITE, &originalProtection)) {
            logging::error("Native emission read patch protection failed: {}", GetLastError());
            return false;
        }
        for (std::size_t i = 0; i < sites.size(); ++i)
            std::memcpy(reinterpret_cast<void*>(base + sites[i]), patches[i].data(), patches[i].size());
        FlushInstructionCache(GetCurrentProcess(), first, extent);
        DWORD ignored{};
        if (!VirtualProtect(first, extent, originalProtection, &ignored))
            logging::error("Native emission read patch protection restoration failed: {}", GetLastError());
        installedPage = std::move(page);
        logging::info("Native emission read compatibility installed: three BSDFPrePass channel loads preserve authored colors and use zero emission for absent color storage (revision 1)");
        return true;
    }
}
