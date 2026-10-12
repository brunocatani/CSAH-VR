#pragma once

#include <Windows.h>
#include "../../extern/RenderDoc/renderdoc_app.h"

namespace csah::render
{
    // Installation-time check only. RenderDoc returns wrapped COM objects, so
    // their methods belong to its API-verified module rather than d3d11.dll.
    // Hook the wrapper and chain its trampoline; never unwrap its COM pointers.
    [[nodiscard]] inline bool isSupportedD3D11Method(const void* address) noexcept
    {
        MEMORY_BASIC_INFORMATION memory{};
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!address || VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(memory.Protect & executable)) return false;

        HMODULE owner{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address), &owner)) return false;
        if (owner == GetModuleHandleW(L"d3d11.dll")) return true;
        if (owner != GetModuleHandleW(L"renderdoc.dll")) return false;

        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(owner, "RENDERDOC_GetAPI"));
        RENDERDOC_API_1_1_2* api{};
        return getApi && getApi(eRENDERDOC_API_Version_1_1_2,
            reinterpret_cast<void**>(&api)) == 1 && api;
    }
}
