#include "PCH.h"
#include "render/EngineCubemapArray.h"
#include "support/Logger.h"

#include <Windows.h>
#include <MinHook.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace csah::render::engine_cubemap_array
{
    namespace
    {
        // Fallout4VR.exe 1.2.72, raw disassembly:
        //   0x1428B55C0  MOV RAX,[0x14689B400]; RET  - array getter, called
        //                only by BSDFCompositeShader setup (0x1429198EA) and
        //                its record builder (0x14291B349).
        //   0x1428B54A0  per-frame copy step: creates the array at
        //                0x1428B54EA/0x1428B54F9 when it is null and
        //                registrations are pending, then copies them.
        //   0x14292FFE0  record builder replaces a null texture with the
        //                static stand-in at 0x14689B9E0 (null view).
        //   0x141D97AA0  record replay binds **entry, the texture object's
        //                first qword, on every replay.
        constexpr std::uintptr_t kGetterRva = 0x028B55C0;
        constexpr std::uintptr_t kCopyStepRva = 0x028B54A0;
        constexpr std::uintptr_t kArrayRva = 0x0689B400;
        constexpr std::array<std::uint8_t, 8> kGetterSignature{
            0x48, 0x8B, 0x05, 0x39, 0x5E, 0xFE, 0x03, 0xC3,
        };
        constexpr std::array<std::uint8_t, 23> kCopyStepSignature{
            0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x8C, 0x2A, 0x07, 0x01,
            0x48, 0x89, 0x5C, 0x24, 0x30, 0x8B, 0x1D, 0x51, 0x5F, 0xFE, 0x03,
        };

        // Same 0x40-byte shape as the engine's own null stand-in. The record
        // builder reads +0x30 (0x141D8BCD0 waits on a load only when it is
        // non-zero) and the sampler byte at +0x3C (0x14292F910); both stay
        // zero exactly as in the engine stand-in. Replay reads only +0.
        struct alignas(16) StandInTexture
        {
            std::atomic<ID3D11ShaderResourceView*> view{};
            std::array<std::byte, 0x38> unused{};
        };
        static_assert(sizeof(std::atomic<ID3D11ShaderResourceView*>) == sizeof(void*));
        static_assert(sizeof(StandInTexture) == 0x40);

        // Process lifetime, like the engine array it mirrors (released only
        // at renderer shutdown). The retained reference keeps the published
        // view valid for records that hold the stand-in.
        StandInTexture standIn{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> retainedView;
        bool replacementLogged{};

        using CopyStepFunction = void(__fastcall*)(void* self);
        CopyStepFunction originalCopyStep{};
        const std::byte* image{};
        std::atomic_bool installed{};

        [[nodiscard]] void* engineArray() noexcept
        {
            return std::atomic_ref(*reinterpret_cast<void**>(const_cast<std::byte*>(image + kArrayRva)))
                .load(std::memory_order_acquire);
        }

        // Record-building tasks and composite setup call this; it never
        // writes shared state.
        void* __fastcall hookGetter() noexcept
        {
            auto* array = engineArray();
            return array ? array : &standIn;
        }

        // Runs where the engine creates the array, on the thread that owns
        // the copy step; the only writer of the stand-in.
        void __fastcall hookCopyStep(void* self) noexcept
        {
            originalCopyStep(self);
            auto* array = engineArray();
            if (!array) {
                return;
            }
            ID3D11ShaderResourceView* view{};
            std::memcpy(&view, array, sizeof(view));
            if (!view) {
                return;
            }
            auto* published = standIn.view.load(std::memory_order_relaxed);
            if (published == view) {
                return;
            }
            if (published) {
                // The engine creates the array once per renderer lifetime;
                // a second array would mean a changed lifecycle.
                if (!replacementLogged) {
                    replacementLogged = true;
                    logging::warn(
                        "Engine cubemap array was replaced after publication; records built before the first array keep its view.");
                }
                return;
            }
            view->AddRef();
            retainedView.Attach(view);
            standIn.view.store(view, std::memory_order_release);
            std::uint8_t samplerByte{};
            std::memcpy(&samplerByte, static_cast<const std::byte*>(array) + 0x3C, sizeof(samplerByte));
            logging::info(
                "Engine cubemap array published to early reflection-composite records (array sampler byte {}).",
                samplerByte);
        }

        template <std::size_t Size>
        [[nodiscard]] bool matches(std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected) noexcept
        {
            std::array<std::uint8_t, Size> actual{};
            SIZE_T copied{};
            return ReadProcessMemory(GetCurrentProcess(), image + rva, actual.data(), actual.size(), &copied) &&
                copied == actual.size() && actual == expected;
        }
    }

    bool install() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        if (!image || !matches(kGetterRva, kGetterSignature) || !matches(kCopyStepRva, kCopyStepSignature)) {
            logging::error(
                "Engine cubemap array fix rejected changed code at RVA 0x{:X}/0x{:X}; direct loads keep losing material cubemap reflections.",
                kGetterRva, kCopyStepRva);
            return false;
        }
        auto* getter = const_cast<std::byte*>(image + kGetterRva);
        auto* copyStep = const_cast<std::byte*>(image + kCopyStepRva);
        void* getterTrampoline{};
        void* copyStepTrampoline{};
        auto status = MH_CreateHook(getter, reinterpret_cast<void*>(&hookGetter), &getterTrampoline);
        if (status == MH_OK) {
            status = MH_CreateHook(copyStep, reinterpret_cast<void*>(&hookCopyStep), &copyStepTrampoline);
            if (status != MH_OK) {
                (void)MH_RemoveHook(getter);
            }
        }
        if (status != MH_OK) {
            logging::error("Engine cubemap array fix detour creation failed ({}).", MH_StatusToString(status));
            return false;
        }
        originalCopyStep = reinterpret_cast<CopyStepFunction>(copyStepTrampoline);
        // Publication must be live before the getter can hand out the
        // stand-in, so the copy step is enabled first.
        status = MH_EnableHook(copyStep);
        if (status == MH_OK) {
            status = MH_EnableHook(getter);
            if (status != MH_OK) {
                (void)MH_DisableHook(copyStep);
            }
        }
        if (status != MH_OK) {
            logging::error("Engine cubemap array fix detour enable failed ({}).", MH_StatusToString(status));
            (void)MH_RemoveHook(getter);
            (void)MH_RemoveHook(copyStep);
            originalCopyStep = nullptr;
            return false;
        }
        installed.store(true, std::memory_order_release);
        logging::info("Installed engine cubemap array fix (getter RVA 0x{:X}, copy step RVA 0x{:X}).", kGetterRva,
            kCopyStepRva);
        return true;
    }
}
