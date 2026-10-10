#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
namespace csah::support {
// One validation per memory region, scoped to this copy. Nothing is cached
// across callbacks or retained after an engine object can change lifetime.
inline bool readMemory(std::uintptr_t address, void* output, std::size_t size) noexcept
{
    if (!address || !output || !size || address > (std::numeric_limits<std::uintptr_t>::max)()-size) return false;
    const auto end=address+size;
    for(auto cursor=address;cursor<end;) {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m))!=sizeof(m) ||
            m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        const auto start=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        if(m.RegionSize>(std::numeric_limits<std::uintptr_t>::max)()-start) return false;
        const auto regionEnd=start+m.RegionSize;
        if(cursor<start || regionEnd<=cursor) return false;
        cursor=(std::min)(end,regionEnd);
    }
    std::memcpy(output,reinterpret_cast<void*>(address),size);return true;
}
template<class T> bool readMemory(std::uintptr_t address,T& value) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);
    return readMemory(address,&value,sizeof(T));
}
inline bool readCString(std::uintptr_t address,char* output,std::size_t capacity) noexcept
{
    if(!address || !output || !capacity || address>(std::numeric_limits<std::uintptr_t>::max)()-capacity) return false;
    std::size_t copied=0;
    while(copied<capacity) {
        MEMORY_BASIC_INFORMATION m{};const auto cursor=address+copied;
        if(VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m))!=sizeof(m) ||
            m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        const auto start=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        if(cursor<start || cursor-start>=m.RegionSize) return false;
        const auto count=(std::min)(capacity-copied,m.RegionSize-(cursor-start));
        const auto* source=reinterpret_cast<const char*>(cursor);
        const auto* terminator=static_cast<const char*>(std::memchr(source,0,count));
        const auto length=terminator?std::size_t(terminator-source)+1:count;
        std::memcpy(output+copied,source,length);copied+=length;
        if(terminator) return true;
    }
    return false;
}
}
