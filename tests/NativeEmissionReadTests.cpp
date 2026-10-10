#include "render/NativeEmissionRead.h"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace emission = csah::render::native_emission_read;
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

    void test()
    {
        struct Memory
        {
            void* value = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            ~Memory() { if (value) VirtualFree(value, 0, MEM_RELEASE); }
        } memory;
        require(memory.value != nullptr, "allocate executable regression");
        auto* code = static_cast<std::uint8_t*>(memory.value);
        constexpr std::array<float, 3> zero{};
        std::array<float, 3> authored{0.25f, 3.0f, 0.8f};
        std::array<std::byte, 0x1A8> property{};
        std::array<std::uint64_t, 2> flags{};
        // Save R13, put the property in the native input register, set carry,
        // record flags, CALL the patched load, then record flags and restore.
        std::array<std::uint8_t, 32> wrapper{
            0x41,0x55,0x49,0x89,0xCD,0xF9,0x9C,0x59,0x48,0x89,0x0A,
            0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,
            0x9C,0x59,0x48,0x89,0x4A,0x08,0x41,0x5D,0xC3
        };
        using Probe = const float* (*)(void*, void*);
        for (std::size_t channel = 0; channel < emission::sites.size(); ++channel) {
            auto* entry = code + 512 + channel * 64;
            auto* destination = code + 128 + channel * emission::stride;
            std::copy(emission::load.begin(), emission::load.end(), entry);
            entry[7] = 0xC3;
            const auto body = emission::thunk(reinterpret_cast<std::uintptr_t>(zero.data()),
                reinterpret_cast<std::uintptr_t>(entry + 7));
            std::copy(body.begin(), body.end(), destination);
            entry[0] = 0xE9;
            const auto delta = static_cast<std::int32_t>(destination - (entry + 5));
            std::memcpy(entry + 1, &delta, sizeof(delta));
            entry[5] = entry[6] = 0x90;
            const auto target = reinterpret_cast<std::uintptr_t>(entry);
            std::memcpy(wrapper.data() + 13, &target, sizeof(target));
            std::copy(wrapper.begin(), wrapper.end(), code);
            FlushInstructionCache(GetCurrentProcess(), code, 4096);
            const auto probe = reinterpret_cast<Probe>(code);
            for (const float* source : {authored.data(), static_cast<float*>(nullptr)}) {
                std::memcpy(property.data() + 0x1A0, &source, sizeof(source));
                const auto before = property;
                const auto* selected = probe(property.data(), flags.data());
                require(selected == (source ? source : zero.data()), "preserve valid pointer or select zero emission");
                require(selected[channel] == (source ? authored[channel] : 0.0f), "native channel load remains readable");
                require(flags[0] == flags[1] && (flags[1] & 1), "preserve native flags including carry");
                require(before == property, "do not mutate shared material storage");
            }
        }
    }
}

int main()
{
    try {
        test();
        std::cout << "All three native emission thunks preserve colors, flags and property ownership; absent storage reads black.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
