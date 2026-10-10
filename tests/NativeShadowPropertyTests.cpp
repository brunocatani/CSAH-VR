#include "render/NativeShadowProperty.h"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void test() {
    struct Page {void* value=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);~Page(){if(value)VirtualFree(value,0,MEM_RELEASE);}} page;
    require(page.value!=nullptr,"allocate executable fixture");auto* code=static_cast<std::uint8_t*>(page.value);
    namespace guard=csah::render::native_shadow_property;
    const auto body=guard::thunk(reinterpret_cast<std::uintptr_t>(code+256),reinterpret_cast<std::uintptr_t>(code+320));
    std::copy(body.begin(),body.end(),code+128);
    // Native input registers, a distinct prior pass index and carry flag.
    std::uint8_t wrapper[]{0x56,0x57,0x48,0x83,0xEC,0x28,0x48,0x89,0xCE,0x48,0x89,0xD7,0x4D,0x89,0xC2,
        0x41,0xB8,0x88,0x77,0x66,0x55,0x49,0x89,0xD1,0xF9,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,
        0x48,0x83,0xC4,0x28,0x5F,0x5E,0xC3};
    const auto entry=reinterpret_cast<std::uintptr_t>(code+128);std::memcpy(wrapper+27,&entry,8);std::copy(std::begin(wrapper),std::end(wrapper),code);
    const std::uint8_t record[]{0x9C,0x59,0x49,0x89,0x0A,0x49,0x89,0x42,0x08,0x4D,0x89,0x42,0x10,0x4D,0x89,0x4A,0x18,0x49,0x89,0x72,0x20,0x49,0x89,0x7A,0x28,0xC3};
    for(auto offset:{256,320})std::copy(std::begin(record),std::end(record),code+offset);
    DWORD old{};require(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"seal fixture");FlushInstructionCache(GetCurrentProcess(),code,4096);
    std::array<std::uint8_t,0xF690> accumulator{};std::uint32_t pass=15;std::memcpy(accumulator.data()+0xF688,&pass,4);
    using Probe=void(*)(void*,void*,void*);auto probe=reinterpret_cast<Probe>(code);
    for(std::uintptr_t table:{std::uintptr_t{0},std::uintptr_t{0x123456789abcdef}}) {
        std::array<std::uintptr_t,8> property{};property[0]=table;const auto originalProperty=property;const auto originalAccumulator=accumulator;
        std::array<std::uintptr_t,6> result{};probe(property.data(),accumulator.data(),result.data());
        require((result[0]&1)!=0,"native carry flag lost");require(result[1]==table,"native table register changed");
        require(result[2]==(table?15:0x55667788),"valid pass not forwarded or invalid dispatch continued");
        require(result[3]==reinterpret_cast<std::uintptr_t>(accumulator.data()),"accumulator argument changed");
        require(result[4]==reinterpret_cast<std::uintptr_t>(property.data()) && result[5]==result[3],"nonvolatile native registers changed");
        require(property==originalProperty && accumulator==originalAccumulator,"native scene storage mutated");
    }
}
}
int main(){try{test();std::cout<<"Shadow-property thunk: null table returns without dispatch; valid pass, registers, flags and scene storage preserved.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
