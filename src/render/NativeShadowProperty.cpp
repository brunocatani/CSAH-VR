#include "PCH.h"
#include "render/NativeShadowProperty.h"
#include "support/Logger.h"
#include "support/NearAllocation.h"

#include <Windows.h>
#include <limits>
#include <memory>

namespace csah::render::native_shadow_property
{
    namespace
    {
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
        std::array<std::uint8_t, original.size()> actual{};SIZE_T copied{};
        if (!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(base+site),actual.data(),actual.size(),&copied) || copied!=actual.size() || actual!=original) {
            logging::error("Native shadow-property guard rejected changed code at RVA 0x{:X}",site);return false;
        }
        const std::array<std::uintptr_t,1> next{base+site+5};
        std::unique_ptr<void,ReleasePage> page{support::near_allocation::allocateReachablePage(next,next[0],64)};
        if(!page) {logging::error("Native shadow-property guard could not allocate reachable code");return false;}
        const auto delta=static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(page.get()))-static_cast<std::int64_t>(next[0]);
        if(delta<(std::numeric_limits<std::int32_t>::min)() || delta>(std::numeric_limits<std::int32_t>::max)()) return false;
        const auto body=thunk(base+resume,base+empty);std::memcpy(page.get(),body.data(),body.size());
        DWORD old{};
        if(!VirtualProtect(page.get(),64,PAGE_EXECUTE_READ,&old)) {logging::error("Native shadow-property guard code sealing failed: {}",GetLastError());return false;}
        FlushInstructionCache(GetCurrentProcess(),page.get(),64);
        auto* target=reinterpret_cast<void*>(base+site);
        if(!VirtualProtect(target,original.size(),PAGE_EXECUTE_READWRITE,&old)) {logging::error("Native shadow-property guard patch protection failed: {}",GetLastError());return false;}
        std::array<std::uint8_t,original.size()> patch;patch.fill(0x90);patch[0]=0xE9;
        const auto relative=static_cast<std::int32_t>(delta);std::memcpy(patch.data()+1,&relative,4);std::memcpy(target,patch.data(),patch.size());
        FlushInstructionCache(GetCurrentProcess(),target,patch.size());DWORD ignored{};
        if(!VirtualProtect(target,original.size(),old,&ignored)) logging::error("Native shadow-property guard protection restoration failed: {}",GetLastError());
        installedPage = std::move(page);
        logging::info("Native shadow-property guard installed at RVA 0x281C78D; cleared shader properties take the native empty-pass return, valid shadow dispatch remains unchanged.");
        return true;
    }
}
