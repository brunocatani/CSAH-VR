#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace csah::native_shadows::patch_model
{
    struct BytePatch
    {
        std::uintptr_t rva{};
        std::array<std::uint8_t, 9> expected{};
        std::array<std::uint8_t, 9> replacement{};
        std::size_t size{};
        const char* name{};
    };

    constexpr std::uintptr_t kCascadeCountRva = 0x03924818;
    constexpr std::uintptr_t kCascadeDistanceRva = 0x03924808;
    constexpr std::uintptr_t kRendererDistanceRva = 0x068788F0;
    // FO4VR 14284E9E0 selects masks containing cascades 2/3 when
    // cascadeDistance < rendererDistance. 14290DBD0 also clamps the two-
    // cascade frustum to cascadeDistance. Keep both distances coordinated,
    // including when four-cascade allocation patches are disabled.
    template<class Writer>
    [[nodiscard]] bool writeFixedDistances(float distance, float rendererDistance, Writer&& write)
    {
        const float guardDistance = distance > rendererDistance ? distance : rendererDistance;
        if (!write(kCascadeDistanceRva, guardDistance) ||
            !write(kRendererDistanceRva, distance)) return false;
        return guardDistance == distance || write(kCascadeDistanceRva, distance);
    }

    constexpr std::uintptr_t kShadowResolutionRva = 0x039266F0;
    constexpr std::uintptr_t kShadowRendererStateRva = 0x068787F0;
    constexpr std::size_t kCachedCascadeBlendOffset = 0x114;
    constexpr std::size_t kCachedOrthoShadowFilterOffset = 0x190;
    constexpr std::uint32_t kExtendedCascadeCount = 4;

    constexpr std::array<std::uintptr_t, 3> kCascadeCountReadRvas{
        0x027E929A,
        0x028A57A0,
        0x028A5C3C,
    };

    constexpr std::array<BytePatch, 5> kTiledLightingPatches{
        BytePatch{
            .rva = 0x02889ACF,
            .expected = { 0x74, 0x2B },
            .replacement = { 0x90, 0x90 },
            .size = 2,
            .name = "tiled dimensions VR gate",
        },
        BytePatch{
            .rva = 0x02889B62,
            .expected = { 0x0F, 0x84, 0xFA, 0x00, 0x00, 0x00 },
            .replacement = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 },
            .size = 6,
            .name = "tiled structured-buffer VR gate",
        },
        BytePatch{
            .rva = 0x028A5A86,
            .expected = { 0x74, 0x3A },
            .replacement = { 0x90, 0x90 },
            .size = 2,
            .name = "tiled VR render-target gate",
        },
        BytePatch{
            .rva = 0x028A5BCC,
            .expected = { 0x74, 0x3A },
            .replacement = { 0x90, 0x90 },
            .size = 2,
            .name = "tiled render-target gate",
        },
        BytePatch{
            .rva = 0x027EE5CC,
            .expected = { 0x75, 0x16 },
            .replacement = { 0x90, 0x90 },
            .size = 2,
            .name = "tiled dispatch VR gate",
        },
    };

    constexpr std::array<BytePatch, 3> kCascadeScalarPatches{
        BytePatch{
            .rva = 0x0290DC03,
            .expected = { 0x83, 0x3D, 0x0E, 0x6C, 0x01, 0x01, 0x02 },
            .replacement = { 0x83, 0x3D, 0x0E, 0x6C, 0x01, 0x01, 0x04 },
            .size = 7,
            .name = "cascade setup count comparison",
        },
        BytePatch{
            .rva = 0x027C340C,
            .expected = { 0x02 },
            .replacement = { 0x04 },
            .size = 1,
            .name = "cascade shader array capacity",
        },
        BytePatch{
            .rva = 0x027C34D8,
            .expected = { 0x02 },
            .replacement = { 0x04 },
            .size = 1,
            .name = "cascade shader stored count",
        },
    };

    constexpr std::array<BytePatch, 4> kSafeMaskPatches{
        BytePatch{
            .rva = 0x0284E9FB,
            .expected = { 0x0F },
            .replacement = { 0x03 },
            .size = 1,
            .name = "cascade initial safe mask",
        },
        BytePatch{
            .rva = 0x0284EA38,
            .expected = { 0x0F },
            .replacement = { 0x03 },
            .size = 1,
            .name = "cascade fallback safe mask",
        },
        BytePatch{
            .rva = 0x0284EA4C,
            .expected = { 0x05 },
            .replacement = { 0x03 },
            .size = 1,
            .name = "cascade rotating mask one",
        },
        BytePatch{
            .rva = 0x0284EA5F,
            .expected = { 0x09 },
            .replacement = { 0x03 },
            .size = 1,
            .name = "cascade rotating mask three",
        },
    };

    constexpr std::array<BytePatch, 4> kFullMaskPatches{
        BytePatch{
            .rva = 0x0284E9FB,
            .expected = { 0x03 },
            .replacement = { 0x0F },
            .size = 1,
            .name = "cascade full initial mask",
        },
        BytePatch{
            .rva = 0x0284EA38,
            .expected = { 0x03 },
            .replacement = { 0x0F },
            .size = 1,
            .name = "cascade full fallback mask",
        },
        BytePatch{
            .rva = 0x0284EA4C,
            .expected = { 0x03 },
            .replacement = { 0x0F },
            .size = 1,
            .name = "cascade full mask one",
        },
        BytePatch{
            .rva = 0x0284EA5F,
            .expected = { 0x03 },
            .replacement = { 0x0F },
            .size = 1,
            .name = "cascade full mask three",
        },
    };

    constexpr std::uintptr_t kZeroInitRva = 0x027A52A0;
    constexpr std::uintptr_t kNullSafetyRva = 0x0281377F;
    constexpr std::uintptr_t kNodeAllocatorRva = 0x0278E610;
    constexpr std::uintptr_t kPointerValidationRva = 0x027A49DA;
    constexpr std::uintptr_t kPointerValidationContinueRva = 0x027A49E3;
    constexpr std::uintptr_t kPointerValidationSkipRva = 0x027A4A6D;

    constexpr std::array<std::uint8_t, 8> kZeroInitSignature{
        0x4A, 0x89, 0x94, 0x10, 0x90, 0x00, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 7> kNullSafetySignature{
        0x49, 0x8B, 0xAA, 0x80, 0x01, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 7> kNodeAllocatorSignature{
        0x48, 0x83, 0xEC, 0x68, 0x4D, 0x8B, 0xD1,
    };
    constexpr std::array<std::uint8_t, 9> kPointerValidationSignature{
        0x4D, 0x85, 0xF6, 0x0F, 0x84, 0x8A, 0x00, 0x00, 0x00,
    };

    constexpr std::uintptr_t kVrArrayRva = 0x06878B18;
    constexpr std::uintptr_t kVrArrayCountRva = 0x06878B28;
    constexpr std::size_t kVrEntrySize = 0x180;
    constexpr std::array<std::size_t, 4> kVrPoolOffsets{
        0x70,
        0xA8,
        0xE8,
        0x128,
    };

    constexpr std::uintptr_t kRenderSceneNodeRva = 0x06879520;
    constexpr std::uintptr_t kSetupSceneNodeRva = 0x06885D40;
    constexpr std::size_t kCascadeGroupOffset = 0x248;
    constexpr std::size_t kCascadeVrFlagOffset = 0x173;
    constexpr std::size_t kFlatCountOffset = 0x190;
    constexpr std::size_t kFlatBufferOffset = 0x198;
    constexpr std::size_t kShaderObjectOffset = 0x2B8;
    constexpr std::size_t kFlatEntrySize = 0x110;
    constexpr std::size_t kFlatShadowMapOffset = 0x50;
    constexpr std::size_t kFlatLastCascadeOffset = 0x102;
    constexpr std::size_t kShaderStoredCountOffset = 0x1D8;
    constexpr std::size_t kShaderArrayCapacityOffset = 0x168;
    constexpr std::size_t kShaderArrayCountOffset = 0x16A;
}
