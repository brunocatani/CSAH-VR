#include "PCH.h"

#pragma push_macro("MEM_RELEASE")
#pragma push_macro("MAX_PATH")
#pragma push_macro("near")
#pragma push_macro("far")
#undef MEM_RELEASE
#undef MAX_PATH
#undef near
#undef far
#include <RE/Fallout.h>
#pragma pop_macro("far")
#pragma pop_macro("near")
#pragma pop_macro("MAX_PATH")
#pragma pop_macro("MEM_RELEASE")

#include "Features/skylighting/SkylightingNativeHooks.h"

#include "Features/native_shadows/NativeShadowRuntime.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "support/NearAllocation.h"
#include "support/Logger.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#pragma intrinsic(_ReturnAddress)

namespace csah::skylighting
{
    namespace
    {
        constexpr std::uintptr_t kWrapperRva = 0x00634300;
        constexpr std::uintptr_t kRenderRva = 0x006350C0;
        constexpr std::uintptr_t kProjectionRva = 0x00635530;
        // FO4VR precipitation projection calls this VR frustum fan-out at
        // 0x140635A76 before it builds the capture matrix used by traversal
        // and probes. Own only that callsite. The shared function has other
        // engine and plugin consumers that must remain in their current chain.
        constexpr std::uintptr_t kSetViewFrustumVrCallsiteRva = 0x00635A76;
        constexpr std::uintptr_t kSetViewFrustumVrRva = 0x01C2BFA0;
        constexpr std::uintptr_t kWrapperFirstCallTargetRva = 0x0012FB50;
        constexpr std::uintptr_t kDepthTargetMapperRva = 0x01DB9E40;
        constexpr std::uintptr_t kRendererStateRva = 0x038AC010;
        constexpr std::uintptr_t kCubeSizeRva = 0x05A3CFA4;
        constexpr std::uintptr_t kDirectionRva = 0x05A3CFC8;
        constexpr std::uintptr_t kGpuCullingEnabledRva = 0x027E0D50;
        constexpr std::uintptr_t kPass14ResolverRva = 0x0281CB50;
        constexpr std::uintptr_t kAccumulatorPassCollectorRva = 0x0281E760;
        constexpr std::uintptr_t kLightingPrecipitationBuilderRva =
            0x027A48B0;
        constexpr std::uintptr_t kLightingPassListResolverRva = 0x027A51E0;
        constexpr std::uintptr_t kPassListClearRva = 0x0278E3E0;
        constexpr std::uintptr_t kPassListEmplaceRva = 0x0278E610;
        constexpr std::uintptr_t kUtilityShaderSingletonRva = 0x0689B4F0;
        constexpr std::uintptr_t kUtilityShaderVtableRva = 0x030BD988;
        constexpr std::uintptr_t kUtilityShaderSecondaryVtableRva =
            0x030BD9F8;
        constexpr std::uintptr_t kBsxFlagsVtableRva = 0x02E72CB8;
        constexpr std::uintptr_t kSpecialGeometryNiRttiRva = 0x0689B458;
        constexpr std::size_t kRenderDepthTargetSetupOffset = 0x1CC;
        constexpr std::size_t kRenderGpuCullingQueryOffset = 0x212;
        constexpr std::size_t kRenderGpuCullingReturnOffset = 0x217;
        constexpr std::size_t kDepthTargetMapperSignatureOffset = 0x1A;
        constexpr std::size_t kDepthTargetMapOffset = 0x15FC;
        constexpr std::ptrdiff_t kPrecipitationManagerOffset = 0xA0;
        constexpr std::size_t kPrecipitationManagerReadableSize = 0x98;
        constexpr std::size_t kAccumulatorPassIndexOffset = 0xF6B0;
        constexpr std::size_t kAccumulatorPassKeyOffset = 0xF6B8;
        constexpr std::uint32_t kAccumulatorPassListCount = 4;
        constexpr std::size_t kLightingPrecipitationBuilderSlotOffset =
            0x170;
        constexpr std::size_t kRenderPassNextOffset = 0x40;
        constexpr std::size_t kRenderPassCategoryOffset = 0x4C;
        constexpr std::size_t kMaximumCollectedPasses = 16;
        constexpr std::size_t kBsxValueOffset = 0x18;
        constexpr std::size_t kUtilityShaderSecondaryVtableOffset = 0x10;
        constexpr std::size_t kUtilityShaderKindOffset = 0x18;
        constexpr std::uint32_t kUtilityShaderKind = 1;
        constexpr std::size_t kUtilityShaderIdentitySize = 0x1C;
        constexpr std::size_t kMaximumParentTraversal = 64;
        constexpr std::size_t kRelativeCallSize = 5;
        constexpr std::size_t kAbsoluteJumpSize = 14;
        constexpr float kMinimumOccluderRadius = 32.0f;
        constexpr std::uint32_t kUtilityVertexColorDescriptor = 1u << 0;
        constexpr std::uint32_t kUtilityTextureDescriptor = 1u << 1;
        constexpr std::uint32_t kUtilityAlphaTestDescriptor = 1u << 7;
        constexpr std::uint32_t kUtilityRenderDepthDescriptor = 1u << 13;
        constexpr std::uint32_t kUtilityTreeAnimDescriptor = 1u << 26;
        constexpr std::uint8_t kUtilityDepthPassCategory = 0x1E;
        constexpr std::uint16_t kNiAlphaPropertyAlphaTest = 1u << 9;
        constexpr std::int32_t kExcludedBsxFlags = 0x3D54;

        constexpr std::uint64_t kPropertySkinned = 1ull << 1;
        constexpr std::uint64_t kPropertyTempRefraction = 1ull << 2;
        constexpr std::uint64_t kPropertyRefraction = 1ull << 15;
        constexpr std::uint64_t kPropertyEyeReflect = 1ull << 17;
        constexpr std::uint64_t kPropertyDecal = 1ull << 26;
        constexpr std::uint64_t kPropertyDynamicDecal = 1ull << 27;
        constexpr std::uint64_t kPropertyZBufferWrite = 1ull << 32;
        constexpr std::uint64_t kPropertyLodLandscape = 1ull << 33;
        constexpr std::uint64_t kPropertyVertexColors = 1ull << 37;
        constexpr std::uint64_t kPropertyTreeAnim = 1ull << 61;
        constexpr std::array<std::byte, 6> kWrapperSignature{
            std::byte{ 0x40 }, std::byte{ 0x53 }, std::byte{ 0x48 },
            std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x30 },
        };
        constexpr std::array<std::byte, 20> kRenderSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x55 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0xA8 },
            std::byte{ 0x68 }, std::byte{ 0xFB }, std::byte{ 0xFF },
            std::byte{ 0xFF }, std::byte{ 0x48 }, std::byte{ 0x81 },
            std::byte{ 0xEC }, std::byte{ 0x88 }, std::byte{ 0x05 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 30> kProjectionSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x58 },
            std::byte{ 0x08 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x70 }, std::byte{ 0x18 }, std::byte{ 0x48 },
            std::byte{ 0x89 }, std::byte{ 0x78 }, std::byte{ 0x20 },
            std::byte{ 0x55 }, std::byte{ 0x48 }, std::byte{ 0x8D },
            std::byte{ 0xA8 }, std::byte{ 0x18 }, std::byte{ 0xFF },
            std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x48 },
            std::byte{ 0x81 }, std::byte{ 0xEC }, std::byte{ 0xE0 },
            std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        // ComputeProjection loads its anchor node at +0x2C0:
        //   48 8B 05 rel32 (MOV RAX,[global]); 48 8B 98 80 01 00 00 (MOV RBX,[RAX+0x180]).
        // The render wrapper at 0x006350C0 subtracts the same node's world
        // translate (+0xA0) when it builds the output occlusion matrix, so
        // probe positions must be expressed relative to this node.
        constexpr std::size_t kProjectionAnchorLoadOffset = 0x2C0;
        constexpr std::size_t kProjectionAnchorLoadSize = 14;
        constexpr std::size_t kAnchorNodeOffset = 0x180;
        constexpr std::size_t kNiAVObjectWorldTranslateOffset = 0xA0;
        // ComputeProjection stores the camera's local rotation rows at +0x30,
        // +0x40 and +0x50 as r0=d, r1=cross(d,r2), r2=normalize(cross(d,up)),
        // whose determinant is -1. The mirrored basis flips rasterizer
        // winding, so single-sided terrain and roofs are culled from the
        // capture and the occlusion depth stays clear.
        constexpr std::size_t kNiAVObjectLocalRotateOffset = 0x30;
        constexpr std::size_t kNiMatrix3RowStride = 0x10;
        constexpr std::array<std::byte, 16> kSetViewFrustumVrSignature{
            std::byte{ 0x57 }, std::byte{ 0x44 }, std::byte{ 0x8B },
            std::byte{ 0x99 }, std::byte{ 0xB0 }, std::byte{ 0x01 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x33 },
            std::byte{ 0xFF }, std::byte{ 0x4C }, std::byte{ 0x8B },
            std::byte{ 0xC2 }, std::byte{ 0x4C }, std::byte{ 0x8B },
            std::byte{ 0xD1 },
        };
        constexpr std::array<std::byte, 27> kRenderDepthTargetSetupSignature{
            std::byte{ 0x45 }, std::byte{ 0x33 }, std::byte{ 0xC9 },
            std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x0D },
            std::byte{ 0x7A }, std::byte{ 0x6D }, std::byte{ 0x27 },
            std::byte{ 0x03 }, std::byte{ 0x41 }, std::byte{ 0x8D },
            std::byte{ 0x51 }, std::byte{ 0x09 }, std::byte{ 0x45 },
            std::byte{ 0x33 }, std::byte{ 0xC0 }, std::byte{ 0xC6 },
            std::byte{ 0x44 }, std::byte{ 0x24 }, std::byte{ 0x20 },
            std::byte{ 0x00 }, std::byte{ 0xE8 }, std::byte{ 0x99 },
            std::byte{ 0x4B }, std::byte{ 0x78 }, std::byte{ 0x01 },
        };
        constexpr std::array<std::byte, 17> kDepthTargetMapperSignature{
            std::byte{ 0x48 }, std::byte{ 0x63 }, std::byte{ 0xC2 },
            std::byte{ 0x41 }, std::byte{ 0x89 }, std::byte{ 0x92 },
            std::byte{ 0x88 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x8B }, std::byte{ 0x8C },
            std::byte{ 0x81 }, std::byte{ 0xFC }, std::byte{ 0x15 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 33> kGpuCullingEnabledSignature{
            std::byte{ 0x80 }, std::byte{ 0x3D }, std::byte{ 0x79 },
            std::byte{ 0x7D }, std::byte{ 0x09 }, std::byte{ 0x04 },
            std::byte{ 0x00 }, std::byte{ 0x74 }, std::byte{ 0x15 },
            std::byte{ 0x80 }, std::byte{ 0x3D }, std::byte{ 0xD0 },
            std::byte{ 0xCA }, std::byte{ 0x13 }, std::byte{ 0x01 },
            std::byte{ 0x00 }, std::byte{ 0x74 }, std::byte{ 0x0C },
            std::byte{ 0x80 }, std::byte{ 0x3D }, std::byte{ 0x68 },
            std::byte{ 0x7D }, std::byte{ 0x09 }, std::byte{ 0x04 },
            std::byte{ 0x00 }, std::byte{ 0x75 }, std::byte{ 0x03 },
            std::byte{ 0xB0 }, std::byte{ 0x01 }, std::byte{ 0xC3 },
            std::byte{ 0x32 }, std::byte{ 0xC0 }, std::byte{ 0xC3 },
        };
        constexpr std::array<std::byte, 32> kPass14ResolverSignature{
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x5C },
            std::byte{ 0x24 }, std::byte{ 0x08 }, std::byte{ 0x48 },
            std::byte{ 0x89 }, std::byte{ 0x74 }, std::byte{ 0x24 },
            std::byte{ 0x10 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x7C }, std::byte{ 0x24 }, std::byte{ 0x18 },
            std::byte{ 0x41 }, std::byte{ 0x56 }, std::byte{ 0x48 },
            std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x20 },
            std::byte{ 0x49 }, std::byte{ 0x8B }, std::byte{ 0x40 },
            std::byte{ 0x30 }, std::byte{ 0x41 }, std::byte{ 0x8B },
            std::byte{ 0xF1 }, std::byte{ 0x4D }, std::byte{ 0x8B },
            std::byte{ 0xF0 }, std::byte{ 0x48 },
        };
        constexpr std::array<std::byte, 32>
            kAccumulatorPassCollectorSignature{
                std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x5C },
                std::byte{ 0x24 }, std::byte{ 0x08 }, std::byte{ 0x48 },
                std::byte{ 0x89 }, std::byte{ 0x74 }, std::byte{ 0x24 },
                std::byte{ 0x10 }, std::byte{ 0x57 }, std::byte{ 0x48 },
                std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x30 },
                std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xF2 },
                std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0x52 },
                std::byte{ 0x18 }, std::byte{ 0x48 }, std::byte{ 0x8B },
                std::byte{ 0xF9 }, std::byte{ 0x48 }, std::byte{ 0x8B },
                std::byte{ 0x4E }, std::byte{ 0x08 }, std::byte{ 0x41 },
                std::byte{ 0x8B }, std::byte{ 0xD8 },
        };
        constexpr std::array<std::byte, 33>
            kLightingPrecipitationBuilderSignature{
                std::byte{ 0x40 }, std::byte{ 0x53 }, std::byte{ 0x55 },
                std::byte{ 0x56 }, std::byte{ 0x41 }, std::byte{ 0x55 },
                std::byte{ 0x41 }, std::byte{ 0x57 }, std::byte{ 0x48 },
                std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x60 },
                std::byte{ 0x65 }, std::byte{ 0x48 }, std::byte{ 0x8B },
                std::byte{ 0x04 }, std::byte{ 0x25 }, std::byte{ 0x58 },
                std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
                std::byte{ 0x44 }, std::byte{ 0x8B }, std::byte{ 0x15 },
                std::byte{ 0x00 }, std::byte{ 0x82 }, std::byte{ 0x0F },
                std::byte{ 0x04 }, std::byte{ 0x41 }, std::byte{ 0xBB },
                std::byte{ 0xC0 }, std::byte{ 0x09 }, std::byte{ 0x00 },
            };
        constexpr std::array<std::byte, 32>
            kLightingPassListResolverSignature{
                std::byte{ 0x45 }, std::byte{ 0x33 }, std::byte{ 0xC0 },
                std::byte{ 0x4C }, std::byte{ 0x8B }, std::byte{ 0xD1 },
                std::byte{ 0x48 }, std::byte{ 0x8D }, std::byte{ 0x81 },
                std::byte{ 0x90 }, std::byte{ 0x00 }, std::byte{ 0x00 },
                std::byte{ 0x00 }, std::byte{ 0x45 }, std::byte{ 0x33 },
                std::byte{ 0xC9 }, std::byte{ 0x48 }, std::byte{ 0x39 },
                std::byte{ 0x10 }, std::byte{ 0x74 }, std::byte{ 0x10 },
                std::byte{ 0x49 }, std::byte{ 0xFF }, std::byte{ 0xC1 },
                std::byte{ 0x41 }, std::byte{ 0xFF }, std::byte{ 0xC0 },
                std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xC0 },
                std::byte{ 0x20 }, std::byte{ 0x49 },
            };
        constexpr std::array<std::byte, 32> kPassListClearSignature{
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x5C },
            std::byte{ 0x24 }, std::byte{ 0x10 }, std::byte{ 0x56 },
            std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xEC },
            std::byte{ 0x20 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0x19 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0xF1 }, std::byte{ 0x48 }, std::byte{ 0x85 },
            std::byte{ 0xDB }, std::byte{ 0x74 }, std::byte{ 0x3F },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x7C },
            std::byte{ 0x24 }, std::byte{ 0x30 }, std::byte{ 0x66 },
            std::byte{ 0x0F }, std::byte{ 0x1F }, std::byte{ 0x44 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 32> kPassListEmplaceSignature{
            std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xEC },
            std::byte{ 0x68 }, std::byte{ 0x4D }, std::byte{ 0x8B },
            std::byte{ 0xD1 }, std::byte{ 0x48 }, std::byte{ 0x85 },
            std::byte{ 0xD2 }, std::byte{ 0x75 }, std::byte{ 0x48 },
            std::byte{ 0x44 }, std::byte{ 0x8B }, std::byte{ 0x8C },
            std::byte{ 0x24 }, std::byte{ 0x90 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x33 },
            std::byte{ 0xC0 }, std::byte{ 0x49 }, std::byte{ 0x8B },
            std::byte{ 0xD2 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x44 }, std::byte{ 0x24 }, std::byte{ 0x50 },
            std::byte{ 0x48 }, std::byte{ 0x89 },
        };

        using WrapperFunction = void(__fastcall*)();
        using NativeSkySingleton = void*(__fastcall*)();
        using NativeGpuCullingEnabled = std::uint8_t(__fastcall*)();
        using SetViewFrustumVr = void(__fastcall*)(
            void* camera,
            RE::NiFrustum* frustum);
        using Pass14Resolver = std::uint64_t(__fastcall*)(
            void* accumulator,
            void* geometry,
            void* property,
            std::uint32_t bucketIndex);
        using AccumulatorPassCollector = void(__fastcall*)(
            void* accumulator,
            void* pass,
            std::uint32_t bucketIndex);
        using PassListClear = void(__fastcall*)(void** list);
        using LightingPassListResolver = void**(__fastcall*)(
            void* property,
            void* passKey);
        using PassListEmplace = void*(__fastcall*)(
            void** list,
            void* reusablePass,
            void* property,
            void* shader,
            std::uint32_t descriptor,
            std::uint8_t passCategory,
            void* geometry);

        struct DetourIdentity
        {
            const std::byte* patch{};
            const void* destination{};
        };

        struct DirectCallIdentity
        {
            const std::byte* callsite{};
            const void* destination{};
        };

        struct UtilityShaderIdentity
        {
            void* primaryVtable{};
            void* secondaryVtable{};
            std::uint32_t kind{};
            bool primaryExact{};
            bool primaryCallable{};
            bool secondaryExact{};

            [[nodiscard]] bool valid() const noexcept
            {
                return primaryCallable && secondaryExact &&
                    kind == kUtilityShaderKind;
            }
        };

        enum class BsxFilterResult
        {
            include,
            exclude,
            invalid,
        };

        WrapperFunction originalWrapper{};
        NativeSkySingleton nativeSkySingleton{};
        NativePrecipitationRender nativeRender{};
        NativeProjectionSetup nativeProjection{};
        NativeGpuCullingEnabled originalGpuCullingEnabled{};
        SetViewFrustumVr originalSetViewFrustumVr{};
        Pass14Resolver originalPass14Resolver{};
        AccumulatorPassCollector collectAccumulatorPass{};
        LightingPassListResolver resolveLightingPassList{};
        PassListClear clearPassList{};
        PassListEmplace emplacePass{};
        void* const* projectionAnchorRoot{};
        void* utilityShader{};
        const void* utilityShaderVtable{};
        const void* utilityShaderSecondaryVtable{};
        const void* bsxFlagsVtable{};
        std::optional<RE::BSFixedString> bsxKey;
        std::byte* wrapperTarget{};
        std::byte* gpuCullingEnabledTarget{};
        std::byte* setViewFrustumVrCallsite{};
        void* setViewFrustumVrCallThunk{};
        const void* privateRenderGpuCullingReturnAddress{};
        std::byte* pass14Target{};
        const RE::NiRTTI* specialGeometryNiRtti{};
        const void* lightingPrecipitationPassBuilder{};
        DetourIdentity installedWrapperIdentity{};
        DetourIdentity installedGpuCullingEnabledIdentity{};
        DirectCallIdentity installedSetViewFrustumVrCallIdentity{};
        DetourIdentity installedPass14Identity{};
        std::atomic_bool installed{};
        std::atomic_bool passProducerReady{};
        std::atomic_bool utilityShaderDeferredLogged{};
        std::atomic_bool firstCallbackLogged{};
        std::atomic_bool missingManagerLogged{};
        std::atomic_bool passProductionActive{};
        std::atomic_uint32_t activeCaptureQuadrant{ 4u };

        [[nodiscard]] bool isReadableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
                return false;
            }
            const auto end = begin + size;
            auto cursor = begin;
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
                const auto region = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (information.RegionSize >
                    std::numeric_limits<std::uintptr_t>::max() - region) {
                    return false;
                }
                const auto regionEnd = region + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(end, regionEnd);
            }
            return true;
        }

        [[nodiscard]] bool isExecutableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!isReadableRange(address, size)) {
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

        [[nodiscard]] void* readPointerCell(void** cell) noexcept
        {
            return cell ? ReadPointerAcquire(
                              reinterpret_cast<void* const volatile*>(cell)) :
                          nullptr;
        }

        [[nodiscard]] UtilityShaderIdentity inspectUtilityShader(
            void* instance,
            const void* expectedPrimaryVtable,
            const void* expectedSecondaryVtable) noexcept
        {
            UtilityShaderIdentity identity;
            if (!isReadableRange(instance, kUtilityShaderIdentitySize)) {
                return identity;
            }
            auto* bytes = static_cast<std::byte*>(instance);
            identity.primaryVtable = readPointerCell(
                reinterpret_cast<void**>(bytes));
            identity.secondaryVtable = readPointerCell(
                reinterpret_cast<void**>(
                    bytes + kUtilityShaderSecondaryVtableOffset));
            std::memcpy(
                &identity.kind,
                bytes + kUtilityShaderKindOffset,
                sizeof(identity.kind));
            identity.primaryExact =
                identity.primaryVtable == expectedPrimaryVtable;
            identity.secondaryExact =
                identity.secondaryVtable == expectedSecondaryVtable;
            if (isReadableRange(identity.primaryVtable, sizeof(void*))) {
                auto* firstMethod = readPointerCell(
                    reinterpret_cast<void**>(identity.primaryVtable));
                identity.primaryCallable =
                    isExecutableRange(firstMethod, 1);
            }
            return identity;
        }

        [[nodiscard]] BsxFilterResult filterBsxFlags(
            RE::BSGeometry* geometry) noexcept
        {
            if (!geometry || geometry->userData == 0) {
                return BsxFilterResult::include;
            }
            if (!bsxKey || !bsxFlagsVtable) {
                return BsxFilterResult::invalid;
            }
            auto* parent = geometry->parent;
            std::size_t depth{};
            while (parent && depth < kMaximumParentTraversal) {
                auto* extra = parent->GetExtraData(*bsxKey);
                if (extra) {
                    auto* vtable = *reinterpret_cast<void**>(extra);
                    if (vtable != bsxFlagsVtable) {
                        return BsxFilterResult::invalid;
                    }
                    std::int32_t value{};
                    std::memcpy(
                        &value,
                        reinterpret_cast<const std::byte*>(extra) +
                            kBsxValueOffset,
                        sizeof(value));
                    return (value & kExcludedBsxFlags) != 0 ?
                        BsxFilterResult::exclude :
                        BsxFilterResult::include;
                }
                parent = parent->parent;
                ++depth;
            }
            return parent ? BsxFilterResult::invalid :
                            BsxFilterResult::include;
        }

        [[nodiscard]] bool usesLightingPrecipitationBuilder(
            void* propertyAddress) noexcept
        {
            if (!propertyAddress || !lightingPrecipitationPassBuilder) {
                return false;
            }
            auto* vtable = *reinterpret_cast<void**>(propertyAddress);
            if (!vtable) {
                return false;
            }
            auto* target = *reinterpret_cast<void**>(
                static_cast<std::byte*>(vtable) +
                kLightingPrecipitationBuilderSlotOffset);
            return target == lightingPrecipitationPassBuilder;
        }

        void** buildOcclusionPasses(
            void* propertyAddress,
            void* geometryAddress,
            void* accumulator) noexcept
        {
            if (!propertyAddress || !geometryAddress || !accumulator ||
                !resolveLightingPassList || !clearPassList || !emplacePass ||
                !utilityShader) {
                return nullptr;
            }

            auto* accumulatorBytes = static_cast<std::byte*>(accumulator);
            std::uint32_t passIndex{};
            std::memcpy(
                &passIndex,
                accumulatorBytes + kAccumulatorPassIndexOffset,
                sizeof(passIndex));
            if (passIndex >= kAccumulatorPassListCount) {
                return nullptr;
            }
            auto* passKey = *reinterpret_cast<void**>(
                accumulatorBytes + kAccumulatorPassKeyOffset);
            auto** passBucket = resolveLightingPassList(
                propertyAddress,
                passKey);
            if (!passBucket) {
                return nullptr;
            }
            auto** passList = passBucket + passIndex;
            clearPassList(passList);

            auto* property = static_cast<RE::BSShaderProperty*>(
                propertyAddress);
            auto* geometry = static_cast<RE::BSGeometry*>(geometryAddress);
            const auto propertyFlags = property->flags.underlying();
            const auto treeAnimated =
                (propertyFlags & kPropertyTreeAnim) != 0;
            if ((propertyFlags & kPropertySkinned) != 0 && !treeAnimated) {
                return passList;
            }
            if (!std::isfinite(geometry->worldBound.fRadius)) {
                return passList;
            }
            if (geometry->worldBound.fRadius <= kMinimumOccluderRadius) {
                return passList;
            }

            switch (filterBsxFlags(geometry)) {
            case BsxFilterResult::exclude:
                return passList;
            case BsxFilterResult::invalid:
                return passList;
            case BsxFilterResult::include:
                break;
            }

            constexpr auto excludedPropertyFlags =
                kPropertyRefraction | kPropertyTempRefraction |
                kPropertyLodLandscape | kPropertyEyeReflect |
                kPropertyDecal | kPropertyDynamicDecal;
            if ((propertyFlags & kPropertyZBufferWrite) == 0 ||
                (propertyFlags & excludedPropertyFlags) != 0) {
                return passList;
            }

            auto descriptor = kUtilityRenderDepthDescriptor;
            if ((propertyFlags & kPropertyVertexColors) != 0) {
                descriptor |= kUtilityVertexColorDescriptor;
            }
            const auto& geometryRuntime = geometry->GetRuntimeData();
            auto* alphaProperty = geometryRuntime.properties[0].get();
            if (alphaProperty) {
                std::uint16_t alphaFlags{};
                std::memcpy(
                    &alphaFlags,
                    reinterpret_cast<const std::byte*>(alphaProperty) + 0x28,
                    sizeof(alphaFlags));
                if ((alphaFlags & kNiAlphaPropertyAlphaTest) != 0) {
                    descriptor |= kUtilityTextureDescriptor |
                        kUtilityAlphaTestDescriptor;
                }
            }
            if (treeAnimated) {
                descriptor |= kUtilityTreeAnimDescriptor;
            }
            // FO4VR's VS canonicalizer at 0x142974C50 strips the Skyrim
            // utility LOD bit. Do not publish an unsupported PS key.

            auto* pass = emplacePass(
                passList,
                nullptr,
                propertyAddress,
                utilityShader,
                descriptor,
                kUtilityDepthPassCategory,
                geometryAddress);
            if (!pass) {
                return passList;
            }
            return passList;
        }

        void __fastcall hookSetViewFrustumVr(
            void* camera,
            RE::NiFrustum* frustum) noexcept
        {
            if (!originalSetViewFrustumVr) {
                return;
            }
            const auto quadrant =
                activeCaptureQuadrant.load(std::memory_order_acquire);
            if (!passProductionActive.load(std::memory_order_acquire) ||
                quadrant >= 4u || !frustum) {
                originalSetViewFrustumVr(camera, frustum);
                return;
            }

            // Only the private capture sees a right-handed basis: the native
            // Update that follows this call republishes the world transform,
            // and the projection restore after the capture rewrites the rows
            // for stock precipitation.
            if (camera) {
                auto* rows = static_cast<std::byte*>(camera) +
                    kNiAVObjectLocalRotateOffset;
                const auto row = [rows](std::size_t index) noexcept {
                    return reinterpret_cast<float*>(
                        rows + index * kNiMatrix3RowStride);
                };
                const float* r0 = row(0);
                float* r1 = row(1);
                const float* r2 = row(2);
                const float determinant =
                    r0[0] * (r1[1] * r2[2] - r1[2] * r2[1]) -
                    r0[1] * (r1[0] * r2[2] - r1[2] * r2[0]) +
                    r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]);
                if (determinant < 0.0f) {
                    r1[0] = -r1[0];
                    r1[1] = -r1[1];
                    r1[2] = -r1[2];
                }
            }

            auto quarter = *frustum;
            const auto horizontalCenter =
                (quarter.left + quarter.right) * 0.5f;
            const auto verticalCenter =
                (quarter.top + quarter.bottom) * 0.5f;
            if ((quadrant & 1u) == 0u) {
                quarter.right = horizontalCenter;
            } else {
                quarter.left = horizontalCenter;
            }
            if ((quadrant & 2u) == 0u) {
                quarter.top = verticalCenter;
            } else {
                quarter.bottom = verticalCenter;
            }
            originalSetViewFrustumVr(camera, &quarter);
        }

        __declspec(noinline) std::uint8_t __fastcall
            hookGpuCullingEnabled() noexcept
        {
            if (passProductionActive.load(std::memory_order_acquire) &&
                _ReturnAddress() ==
                    privateRenderGpuCullingReturnAddress) {
                return 0;
            }
            return originalGpuCullingEnabled ?
                originalGpuCullingEnabled() :
                0;
        }

        std::uint64_t __fastcall hookPass14Resolver(
            void* accumulator,
            void* geometryAddress,
            void* propertyAddress,
            std::uint32_t bucketIndex) noexcept
        {
            if (!passProductionActive.load(std::memory_order_acquire)) {
                return originalPass14Resolver ?
                    originalPass14Resolver(
                        accumulator,
                        geometryAddress,
                        propertyAddress,
                        bucketIndex) :
                    1;
            }
            if (!geometryAddress || !propertyAddress ||
                !collectAccumulatorPass) {
                return originalPass14Resolver ?
                    originalPass14Resolver(
                        accumulator,
                        geometryAddress,
                        propertyAddress,
                        bucketIndex) :
                    1;
            }

            auto* geometry = static_cast<RE::BSGeometry*>(geometryAddress);
            if (geometry->GetRTTI() == specialGeometryNiRtti) {
                return originalPass14Resolver ?
                    originalPass14Resolver(
                        accumulator,
                        geometryAddress,
                        propertyAddress,
                        bucketIndex) :
                    1;
            }
            if (!usesLightingPrecipitationBuilder(propertyAddress)) {
                return originalPass14Resolver ?
                    originalPass14Resolver(
                        accumulator,
                        geometryAddress,
                        propertyAddress,
                        bucketIndex) :
                    1;
            }

            auto** passList = buildOcclusionPasses(
                propertyAddress,
                geometryAddress,
                accumulator);
            if (!passList) {
                return originalPass14Resolver ?
                    originalPass14Resolver(
                        accumulator,
                        geometryAddress,
                        propertyAddress,
                        bucketIndex) :
                    1;
            }
            auto* pass = *passList;
            std::size_t passCount{};
            while (pass && passCount < kMaximumCollectedPasses) {
                std::uint8_t category{};
                std::memcpy(
                    &category,
                    static_cast<std::byte*>(pass) +
                        kRenderPassCategoryOffset,
                    sizeof(category));
                if (category == kUtilityDepthPassCategory) {
                    collectAccumulatorPass(
                        accumulator,
                        pass,
                        bucketIndex);
                }
                pass = *reinterpret_cast<void**>(
                    static_cast<std::byte*>(pass) +
                    kRenderPassNextOffset);
                ++passCount;
            }
            return 1;
        }

        [[nodiscard]] const std::byte* relativeTarget(
            const std::byte* instruction) noexcept
        {
            if (!isReadableRange(instruction, 5) ||
                instruction[0] != std::byte{ 0xE8 }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, instruction + 1, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) + 5;
            const auto target = static_cast<std::int64_t>(next) +
                static_cast<std::int64_t>(displacement);
            if (target <= 0 ||
                static_cast<std::uint64_t>(target) >
                    std::numeric_limits<std::uintptr_t>::max()) {
                return nullptr;
            }
            return reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(target));
        }

        [[nodiscard]] const std::byte* ripRelativeTarget(
            const std::byte* instruction) noexcept
        {
            if (!isReadableRange(instruction, 7) ||
                instruction[0] != std::byte{ 0x48 } ||
                instruction[1] != std::byte{ 0x8D } ||
                instruction[2] != std::byte{ 0x0D }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, instruction + 3, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(instruction) + 7;
            const auto target = static_cast<std::int64_t>(next) +
                static_cast<std::int64_t>(displacement);
            if (target <= 0 ||
                static_cast<std::uint64_t>(target) >
                    std::numeric_limits<std::uintptr_t>::max()) {
                return nullptr;
            }
            return reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(target));
        }

        [[nodiscard]] void* resolveNativePrecipitationManager() noexcept
        {
            if (!nativeSkySingleton) {
                return nullptr;
            }
            auto* skyState = nativeSkySingleton();
            if (!skyState) {
                return nullptr;
            }
            auto* slot = static_cast<std::byte*>(skyState) +
                kPrecipitationManagerOffset;
            if (!isReadableRange(slot, sizeof(void*))) {
                return nullptr;
            }
            void* precipitation{};
            std::memcpy(&precipitation, slot, sizeof(precipitation));
            if (!isReadableRange(
                    precipitation,
                    kPrecipitationManagerReadableSize)) {
                return nullptr;
            }
            return precipitation;
        }

        [[nodiscard]] bool captureDetourIdentity(
            const void* target,
            DetourIdentity& identity) noexcept
        {
            identity = {};
            if (!isReadableRange(target, 5)) {
                return false;
            }
            auto* entry = static_cast<const std::byte*>(target);
            const auto* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t displacement{};
                std::memcpy(&displacement, entry + 1, sizeof(displacement));
                if (displacement != -7 ||
                    reinterpret_cast<std::uintptr_t>(entry) < 5) {
                    return false;
                }
                patch = entry - 5;
            }
            if (!isReadableRange(patch, 5) ||
                patch[0] != std::byte{ 0xE9 }) {
                return false;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, patch + 1, sizeof(displacement));
            const auto next = reinterpret_cast<std::uintptr_t>(patch) + 5;
            const auto destinationAddress = static_cast<std::int64_t>(next) +
                static_cast<std::int64_t>(displacement);
            if (destinationAddress <= 0) {
                return false;
            }
            const auto* destination = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(destinationAddress));
            if (!isExecutableRange(destination, 1)) {
                return false;
            }
            identity = { patch, destination };
            return true;
        }

        [[nodiscard]] bool captureDirectCallIdentity(
            const std::byte* callsite,
            DirectCallIdentity& identity) noexcept
        {
            identity = {};
            const auto* destination = relativeTarget(callsite);
            if (!destination || !isExecutableRange(destination, 1)) {
                return false;
            }
            identity = { callsite, destination };
            return true;
        }

        [[nodiscard]] bool writeCallBytes(
            std::byte* callsite,
            const std::array<std::byte, kRelativeCallSize>& bytes) noexcept
        {
            if (!isExecutableRange(callsite, bytes.size())) {
                return false;
            }
            DWORD previousProtection{};
            if (!VirtualProtect(
                    callsite,
                    bytes.size(),
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection)) {
                return false;
            }
            std::memcpy(callsite, bytes.data(), bytes.size());
            const auto flushed = FlushInstructionCache(
                                     GetCurrentProcess(),
                                     callsite,
                                     bytes.size()) != FALSE;
            DWORD ignored{};
            const auto restored = VirtualProtect(
                                      callsite,
                                      bytes.size(),
                                      previousProtection,
                                      &ignored) != FALSE;
            return flushed && restored;
        }

        [[nodiscard]] bool encodeRelativeCall(
            const std::byte* callsite,
            const void* destination,
            std::array<std::byte, kRelativeCallSize>& bytes) noexcept
        {
            const auto next = reinterpret_cast<std::uintptr_t>(callsite) +
                kRelativeCallSize;
            const auto difference = static_cast<std::int64_t>(
                                        reinterpret_cast<std::uintptr_t>(
                                            destination)) -
                static_cast<std::int64_t>(next);
            if (difference < (std::numeric_limits<std::int32_t>::min)() ||
                difference > (std::numeric_limits<std::int32_t>::max)()) {
                return false;
            }
            bytes.fill(std::byte{});
            bytes[0] = std::byte{ 0xE8 };
            const auto displacement = static_cast<std::int32_t>(difference);
            std::memcpy(
                bytes.data() + 1,
                &displacement,
                sizeof(displacement));
            return true;
        }

        [[nodiscard]] void* createAbsoluteJumpThunk(
            std::byte* callsite,
            const void* destination) noexcept
        {
            const std::array nextInstructions{
                reinterpret_cast<std::uintptr_t>(callsite) +
                    kRelativeCallSize };
            auto* allocation = support::near_allocation::allocateReachablePage(
                nextInstructions,
                reinterpret_cast<std::uintptr_t>(callsite),
                kAbsoluteJumpSize);
            if (!allocation) {
                return nullptr;
            }
            std::array<std::byte, kAbsoluteJumpSize> thunk{
                std::byte{ 0xFF }, std::byte{ 0x25 }, std::byte{},
                std::byte{}, std::byte{}, std::byte{} };
            const auto destinationAddress =
                reinterpret_cast<std::uintptr_t>(destination);
            std::memcpy(
                thunk.data() + 6,
                &destinationAddress,
                sizeof(destinationAddress));
            std::memcpy(allocation, thunk.data(), thunk.size());
            DWORD previousProtection{};
            const auto executable = VirtualProtect(
                                        allocation,
                                        thunk.size(),
                                        PAGE_EXECUTE_READ,
                                        &previousProtection) != FALSE;
            const auto flushed = executable &&
                FlushInstructionCache(
                    GetCurrentProcess(),
                    allocation,
                    thunk.size()) != FALSE;
            if (!flushed) {
                (void)VirtualFree(allocation, 0, MEM_RELEASE);
                return nullptr;
            }
            return allocation;
        }

        void __fastcall hookWrapper() noexcept
        {
            if (originalWrapper) {
                originalWrapper();
            }
            if (!firstCallbackLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Skylighting observed its first native exterior-render callback.");
            }
            auto* precipitation = resolveNativePrecipitationManager();
            if (!precipitation && !missingManagerLogged.exchange(
                                      true,
                                      std::memory_order_relaxed)) {
                logging::warn(
                    "Skylighting could not resolve the persistent FO4VR precipitation manager at native Sky offset +0xA0; captures remain fail-closed.");
            }
            Runtime::get().onNativePrecipitationFrame(
                precipitation,
                nativeRender,
                nativeProjection);
        }
    }

    ScopedOcclusionPassProduction::ScopedOcclusionPassProduction(
        std::uint32_t captureQuadrant) noexcept
    {
        active_ = passProducerReady.load(std::memory_order_acquire) &&
            originalGpuCullingEnabled && gpuCullingEnabledTarget &&
            originalSetViewFrustumVr && setViewFrustumVrCallsite &&
            setViewFrustumVrCallThunk &&
            privateRenderGpuCullingReturnAddress &&
            originalPass14Resolver &&
            collectAccumulatorPass &&
            resolveLightingPassList && clearPassList && emplacePass &&
            utilityShader && lightingPrecipitationPassBuilder &&
            pass14Target;
        if (active_) {
            auto expected = false;
            active_ = passProductionActive.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
            if (active_) {
                activeCaptureQuadrant.store(
                    captureQuadrant % 4u,
                    std::memory_order_release);
            }
        }
    }

    ScopedOcclusionPassProduction::~ScopedOcclusionPassProduction() noexcept
    {
        if (active_) {
            activeCaptureQuadrant.store(4u, std::memory_order_release);
            passProductionActive.store(false, std::memory_order_release);
        }
    }

    bool ScopedOcclusionPassProduction::active() const noexcept
    {
        return active_;
    }

    bool installNativeHooks() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (!image || !isReadableRange(image, sizeof(IMAGE_DOS_HEADER))) {
            logging::error(
                "Skylighting native hook rejected a missing FO4VR image.");
            return false;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
            logging::error(
                "Skylighting native hook rejected invalid DOS metadata.");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            image + dos->e_lfanew);
        if (!isReadableRange(nt, sizeof(*nt)) ||
            nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            logging::error(
                "Skylighting native hook rejected invalid PE metadata.");
            return false;
        }
        const auto imageSize =
            static_cast<std::uintptr_t>(nt->OptionalHeader.SizeOfImage);
        const auto inImage = [imageSize](
                                 std::uintptr_t rva,
                                 std::size_t size) noexcept {
            return rva <= imageSize && size <= imageSize - rva;
        };
        if (!inImage(kWrapperRva, kWrapperSignature.size() + 5)) {
            logging::error(
                "Skylighting native wrapper contract at RVA 0x00634300 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kRenderRva, kRenderSignature.size())) {
            logging::error(
                "Skylighting native render contract at RVA 0x006350C0 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kProjectionRva, kProjectionSignature.size())) {
            logging::error(
                "Skylighting native projection contract at RVA 0x00635530 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(
                kSetViewFrustumVrCallsiteRva,
                kRelativeCallSize) ||
            !inImage(
                kSetViewFrustumVrRva,
                kSetViewFrustumVrSignature.size())) {
            logging::error(
                "Skylighting native VR frustum call contract at RVA 0x00635A76 -> 0x01C2BFA0 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(
                kRenderRva + kRenderDepthTargetSetupOffset,
                kRenderDepthTargetSetupSignature.size()) ||
            !inImage(
                kRenderRva + kRenderGpuCullingQueryOffset,
                5) ||
            !inImage(
                kDepthTargetMapperRva +
                    kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.size()) ||
            !inImage(
                kRendererStateRva + kDepthTargetMapOffset,
                sizeof(std::int32_t) * 10)) {
            logging::error(
                "Skylighting native depth-target mapping contract is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kCubeSizeRva, sizeof(float))) {
            logging::error(
                "Skylighting native cube-size contract at RVA 0x05A3CFA4 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(kDirectionRva, sizeof(float) * 3)) {
            logging::error(
                "Skylighting native direction contract at RVA 0x05A3CFC8 is outside the FO4VR image.");
            return false;
        }
        if (!inImage(
                kGpuCullingEnabledRva,
                kGpuCullingEnabledSignature.size()) ||
            !inImage(
                kPass14ResolverRva,
                kPass14ResolverSignature.size()) ||
            !inImage(
                kAccumulatorPassCollectorRva,
                kAccumulatorPassCollectorSignature.size()) ||
            !inImage(
                kLightingPrecipitationBuilderRva,
                kLightingPrecipitationBuilderSignature.size()) ||
            !inImage(
                kLightingPassListResolverRva,
                kLightingPassListResolverSignature.size()) ||
            !inImage(kPassListClearRva, kPassListClearSignature.size()) ||
            !inImage(
                kPassListEmplaceRva,
                kPassListEmplaceSignature.size()) ||
            !inImage(kUtilityShaderSingletonRva, sizeof(void*)) ||
            !inImage(kUtilityShaderVtableRva, sizeof(void*)) ||
            !inImage(kUtilityShaderSecondaryVtableRva, sizeof(void*)) ||
            !inImage(kBsxFlagsVtableRva, sizeof(void*)) ||
            !inImage(kSpecialGeometryNiRttiRva, sizeof(void*))) {
            logging::error(
                "Skylighting native world-occlusion producer contract is outside the FO4VR image.");
            return false;
        }

        auto* wrapper = image + kWrapperRva;
        auto* render = image + kRenderRva;
        auto* projection = image + kProjectionRva;
        auto* setViewFrustumVrCall =
            image + kSetViewFrustumVrCallsiteRva;
        auto* setViewFrustumVr = image + kSetViewFrustumVrRva;
        auto* renderDepthTargetSetup =
            render + kRenderDepthTargetSetupOffset;
        auto* depthTargetMapper = image + kDepthTargetMapperRva;
        auto* gpuCullingEnabled = image + kGpuCullingEnabledRva;
        auto* renderGpuCullingQuery =
            render + kRenderGpuCullingQueryOffset;
        auto* pass14Resolver = image + kPass14ResolverRva;
        auto* accumulatorPassCollector =
            image + kAccumulatorPassCollectorRva;
        auto* expectedLightingPrecipitationBuilder =
            image + kLightingPrecipitationBuilderRva;
        auto* lightingPassListResolver =
            image + kLightingPassListResolverRva;
        auto* passListClear = image + kPassListClearRva;
        auto* passListEmplace = image + kPassListEmplaceRva;
        auto** utilityShaderCell = reinterpret_cast<void**>(
            image + kUtilityShaderSingletonRva);
        auto* expectedUtilityVtable = image + kUtilityShaderVtableRva;
        auto* expectedUtilitySecondaryVtable =
            image + kUtilityShaderSecondaryVtableRva;
        auto* expectedBsxVtable = image + kBsxFlagsVtableRva;
        auto* expectedSpecialGeometryNiRtti =
            reinterpret_cast<RE::NiRTTI*>(
                image + kSpecialGeometryNiRttiRva);
        if (!isExecutableRange(wrapper, kWrapperSignature.size() + 5)) {
            logging::error(
                "Skylighting native wrapper contract at RVA 0x00634300 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                wrapper,
                kWrapperSignature.data(),
                kWrapperSignature.size()) != 0) {
            logging::error(
                "Skylighting native wrapper signature mismatch at RVA 0x00634300.");
            return false;
        }
        auto* wrapperFirstCall = wrapper + kWrapperSignature.size();
        if (wrapperFirstCall[0] != std::byte{ 0xE8 }) {
            logging::error(
                "Skylighting native wrapper first-call opcode mismatch at RVA 0x00634306.");
            return false;
        }
        if (relativeTarget(wrapperFirstCall) !=
            image + kWrapperFirstCallTargetRva) {
            logging::error(
                "Skylighting native wrapper first-call target mismatch; expected RVA 0x0012FB50.");
            return false;
        }
        if (!isExecutableRange(render, kRenderSignature.size())) {
            logging::error(
                "Skylighting native render contract at RVA 0x006350C0 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                render,
                kRenderSignature.data(),
                kRenderSignature.size()) != 0) {
            logging::error(
                "Skylighting native render signature mismatch at RVA 0x006350C0.");
            return false;
        }
        if (!isExecutableRange(
                renderDepthTargetSetup,
                kRenderDepthTargetSetupSignature.size()) ||
            std::memcmp(
                renderDepthTargetSetup,
                kRenderDepthTargetSetupSignature.data(),
                kRenderDepthTargetSetupSignature.size()) != 0 ||
            ripRelativeTarget(renderDepthTargetSetup + 3) !=
                image + kRendererStateRva ||
            relativeTarget(renderDepthTargetSetup + 22) !=
                depthTargetMapper) {
            logging::error(
                "Skylighting native render depth-target setup signature mismatch at RVA 0x0063528C.");
            return false;
        }
        if (!isExecutableRange(
                depthTargetMapper + kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.size()) ||
            std::memcmp(
                depthTargetMapper + kDepthTargetMapperSignatureOffset,
                kDepthTargetMapperSignature.data(),
                kDepthTargetMapperSignature.size()) != 0) {
            logging::error(
                "Skylighting native depth-target mapper signature mismatch at RVA 0x01DB9E5A.");
            return false;
        }
        if (!isExecutableRange(projection, kProjectionSignature.size())) {
            logging::error(
                "Skylighting native projection contract at RVA 0x00635530 is not executable and readable.");
            return false;
        }
        if (std::memcmp(
                projection,
                kProjectionSignature.data(),
                kProjectionSignature.size()) != 0) {
            logging::error(
                "Skylighting native projection signature mismatch at RVA 0x00635530.");
            return false;
        }
        void* const* resolvedAnchorRoot{};
        {
            const auto* load = projection + kProjectionAnchorLoadOffset;
            constexpr std::array<std::byte, 3> movRaxRip{
                std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0x05 } };
            constexpr std::array<std::byte, 7> movRbxAnchor{
                std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0x98 },
                std::byte{ 0x80 }, std::byte{ 0x01 }, std::byte{ 0x00 },
                std::byte{ 0x00 } };
            std::int32_t displacement{};
            if (!isExecutableRange(load, kProjectionAnchorLoadSize) ||
                std::memcmp(load, movRaxRip.data(), movRaxRip.size()) != 0 ||
                std::memcmp(load + 7, movRbxAnchor.data(), movRbxAnchor.size()) != 0) {
                logging::error(
                    "Skylighting native projection anchor load mismatch at RVA 0x006357F0.");
                return false;
            }
            std::memcpy(&displacement, load + 3, sizeof(displacement));
            const auto* global = load + 7 + displacement;
            if (!isReadableRange(global, sizeof(void*))) {
                logging::error(
                    "Skylighting native projection anchor global is outside readable FO4VR memory.");
                return false;
            }
            resolvedAnchorRoot = reinterpret_cast<void* const*>(global);
        }
        DirectCallIdentity preexistingSetViewFrustumCall{};
        if (!captureDirectCallIdentity(
                setViewFrustumVrCall,
                preexistingSetViewFrustumCall) ||
            preexistingSetViewFrustumCall.destination ==
                reinterpret_cast<const void*>(&hookSetViewFrustumVr)) {
            logging::error(
                "Skylighting native precipitation VR-frustum callsite at RVA 0x00635A76 has no safe executable predecessor.");
            return false;
        }
        const auto callTargetsNativeEntry =
            preexistingSetViewFrustumCall.destination == setViewFrustumVr;
        const auto nativeSetViewFrustum = callTargetsNativeEntry &&
            isExecutableRange(
                setViewFrustumVr,
                kSetViewFrustumVrSignature.size()) &&
            std::memcmp(
                setViewFrustumVr,
                kSetViewFrustumVrSignature.data(),
                kSetViewFrustumVrSignature.size()) == 0;
        DetourIdentity preexistingSetViewFrustumDetour{};
        const auto chainedNativeEntry = callTargetsNativeEntry &&
            !nativeSetViewFrustum &&
            captureDetourIdentity(
                setViewFrustumVr,
                preexistingSetViewFrustumDetour);
        if (!callTargetsNativeEntry) {
            logging::info(
                "Skylighting found an existing precipitation VR-frustum call owner at RVA 0x00635A76 and will preserve its executable destination {}.",
                fmt::ptr(preexistingSetViewFrustumCall.destination));
        } else if (chainedNativeEntry) {
            logging::info(
                "Skylighting found an existing VR-frustum detour at RVA 0x01C2BFA0 and will preserve it through the precipitation-only callsite chain (destination={}).",
                fmt::ptr(preexistingSetViewFrustumDetour.destination));
        } else if (!nativeSetViewFrustum) {
            logging::warn(
                "Skylighting found externally modified bytes at the executable VR-frustum entry RVA 0x01C2BFA0; the exact precipitation call predecessor is retained without assuming detour encoding.");
        }
        if (!isReadableRange(image + kCubeSizeRva, sizeof(float))) {
            logging::error(
                "Skylighting native cube-size global at RVA 0x05A3CFA4 is not readable.");
            return false;
        }
        if (!isReadableRange(image + kDirectionRva, sizeof(float) * 3)) {
            logging::error(
                "Skylighting native direction global at RVA 0x05A3CFC8 is not readable.");
            return false;
        }
        if (!isExecutableRange(
                gpuCullingEnabled,
                kGpuCullingEnabledSignature.size()) ||
            std::memcmp(
                gpuCullingEnabled,
                kGpuCullingEnabledSignature.data(),
                kGpuCullingEnabledSignature.size()) != 0) {
            logging::error(
                "Skylighting native GPU-culling query signature mismatch at RVA 0x027E0D50.");
            return false;
        }
        if (!isExecutableRange(renderGpuCullingQuery, 5) ||
            renderGpuCullingQuery[0] != std::byte{ 0xE8 } ||
            relativeTarget(renderGpuCullingQuery) != gpuCullingEnabled) {
            logging::error(
                "Skylighting native precipitation GPU-culling query callsite mismatch at RVA 0x006352D2.");
            return false;
        }
        if (!isExecutableRange(
                pass14Resolver,
                kPass14ResolverSignature.size()) ||
            std::memcmp(
                pass14Resolver,
                kPass14ResolverSignature.data(),
                kPass14ResolverSignature.size()) != 0) {
            logging::error(
                "Skylighting native pass-14 resolver signature mismatch at RVA 0x0281CB50.");
            return false;
        }
        if (!isExecutableRange(
                accumulatorPassCollector,
                kAccumulatorPassCollectorSignature.size()) ||
            std::memcmp(
                accumulatorPassCollector,
                kAccumulatorPassCollectorSignature.data(),
                kAccumulatorPassCollectorSignature.size()) != 0) {
            logging::error(
                "Skylighting native accumulator pass-collector signature mismatch at RVA 0x0281E760.");
            return false;
        }
        if (!isExecutableRange(
                expectedLightingPrecipitationBuilder,
                kLightingPrecipitationBuilderSignature.size()) ||
            std::memcmp(
                expectedLightingPrecipitationBuilder,
                kLightingPrecipitationBuilderSignature.data(),
                kLightingPrecipitationBuilderSignature.size()) != 0) {
            logging::error(
                "Skylighting native lighting precipitation-builder signature mismatch at RVA 0x027A48B0.");
            return false;
        }
        if (!isExecutableRange(
                lightingPassListResolver,
                kLightingPassListResolverSignature.size()) ||
            std::memcmp(
                lightingPassListResolver,
                kLightingPassListResolverSignature.data(),
                kLightingPassListResolverSignature.size()) != 0) {
            logging::error(
                "Skylighting native precipitation pass-list resolver signature mismatch at RVA 0x027A51E0.");
            return false;
        }
        if (!isExecutableRange(passListClear, kPassListClearSignature.size()) ||
            std::memcmp(
                passListClear,
                kPassListClearSignature.data(),
                kPassListClearSignature.size()) != 0) {
            logging::error(
                "Skylighting native pass-list clear signature mismatch at RVA 0x0278E3E0.");
            return false;
        }
        const auto passListEmplaceExecutable = isExecutableRange(
            passListEmplace,
            kPassListEmplaceSignature.size());
        const auto passListEmplacePristine = passListEmplaceExecutable &&
            std::memcmp(
                passListEmplace,
                kPassListEmplaceSignature.data(),
                kPassListEmplaceSignature.size()) == 0;
        const auto nativeShadowPrefix = passListEmplacePristine ? 0 :
            native_shadows::verifiedNodeAllocatorPatchPrefix(passListEmplace);
        const auto nativeShadowOwned = passListEmplaceExecutable &&
            nativeShadowPrefix > 0 &&
            nativeShadowPrefix < kPassListEmplaceSignature.size() &&
            std::memcmp(
                passListEmplace + nativeShadowPrefix,
                kPassListEmplaceSignature.data() + nativeShadowPrefix,
                kPassListEmplaceSignature.size() - nativeShadowPrefix) == 0;
        if (!passListEmplacePristine && !nativeShadowOwned) {
            logging::error(
                "Skylighting native pass-list emplace contract mismatch at RVA 0x0278E610; live bytes are neither pristine FO4VR nor the verified Native Shadows safety owner.");
            return false;
        }
        if (nativeShadowOwned) {
            logging::info(
                "Skylighting accepted the verified Native Shadows pass-list safety owner at RVA 0x0278E610 ({} displaced bytes); both four-cascade shadows and world-occlusion capture remain active.",
                nativeShadowPrefix);
        }
        if (!isReadableRange(
                expectedSpecialGeometryNiRtti,
                sizeof(void*))) {
            logging::error(
                "Skylighting native special-geometry RTTI at RVA 0x0689B458 is not readable.");
            return false;
        }
        if (!isReadableRange(utilityShaderCell, sizeof(*utilityShaderCell))) {
            logging::error(
                "Skylighting native utility-shader singleton cell at RVA 0x0689B4F0 is not readable.");
            return false;
        }
        auto* resolvedUtilityShader = readPointerCell(utilityShaderCell);
        if (!resolvedUtilityShader) {
            if (!utilityShaderDeferredLogged.exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::info(
                    "Skylighting native hook installation is deferred until FO4VR constructs the utility-shader singleton.");
            }
            return false;
        }
        const auto utilityIdentity = inspectUtilityShader(
            resolvedUtilityShader,
            expectedUtilityVtable,
            expectedUtilitySecondaryVtable);
        if (!utilityIdentity.valid()) {
            logging::error(
                "Skylighting rejected the FO4VR utility-shader singleton (primary={}, expectedPrimary={}, callable={}, secondary={}, expectedSecondary={}, kind={}).",
                fmt::ptr(utilityIdentity.primaryVtable),
                fmt::ptr(expectedUtilityVtable),
                utilityIdentity.primaryCallable,
                fmt::ptr(utilityIdentity.secondaryVtable),
                fmt::ptr(expectedUtilitySecondaryVtable),
                utilityIdentity.kind);
            return false;
        }
        if (!utilityIdentity.primaryExact) {
            logging::warn(
                "Skylighting accepted a runtime-replaced utility-shader primary vtable {} through the exact secondary vtable RVA 0x030BD9F8 and shader-kind contract.",
                fmt::ptr(utilityIdentity.primaryVtable));
        }

        try {
            bsxKey.emplace("BSX");
        } catch (...) {
            logging::error(
                "Skylighting could not acquire the BSX extra-data key; the world-occlusion producer remains disabled.");
            return false;
        }
        collectAccumulatorPass =
            reinterpret_cast<AccumulatorPassCollector>(
                accumulatorPassCollector);
        resolveLightingPassList =
            reinterpret_cast<LightingPassListResolver>(
                lightingPassListResolver);
        clearPassList = reinterpret_cast<PassListClear>(passListClear);
        emplacePass = reinterpret_cast<PassListEmplace>(passListEmplace);
        utilityShader = resolvedUtilityShader;
        utilityShaderVtable = expectedUtilityVtable;
        utilityShaderSecondaryVtable = expectedUtilitySecondaryVtable;
        bsxFlagsVtable = expectedBsxVtable;
        specialGeometryNiRtti = expectedSpecialGeometryNiRtti;
        lightingPrecipitationPassBuilder =
            expectedLightingPrecipitationBuilder;

        const auto resetResolvedContracts = []() noexcept {
            originalWrapper = nullptr;
            originalGpuCullingEnabled = nullptr;
            originalSetViewFrustumVr = nullptr;
            originalPass14Resolver = nullptr;
            nativeSkySingleton = nullptr;
            nativeRender = nullptr;
            nativeProjection = nullptr;
            projectionAnchorRoot = nullptr;
            collectAccumulatorPass = nullptr;
            resolveLightingPassList = nullptr;
            clearPassList = nullptr;
            emplacePass = nullptr;
            utilityShader = nullptr;
            utilityShaderVtable = nullptr;
            utilityShaderSecondaryVtable = nullptr;
            bsxFlagsVtable = nullptr;
            specialGeometryNiRtti = nullptr;
            lightingPrecipitationPassBuilder = nullptr;
            wrapperTarget = nullptr;
            gpuCullingEnabledTarget = nullptr;
            setViewFrustumVrCallsite = nullptr;
            if (setViewFrustumVrCallThunk) {
                (void)VirtualFree(
                    setViewFrustumVrCallThunk,
                    0,
                    MEM_RELEASE);
                setViewFrustumVrCallThunk = nullptr;
            }
            privateRenderGpuCullingReturnAddress = nullptr;
            pass14Target = nullptr;
            installedWrapperIdentity = {};
            installedGpuCullingEnabledIdentity = {};
            installedSetViewFrustumVrCallIdentity = {};
            installedPass14Identity = {};
            passProducerReady.store(false, std::memory_order_release);
            bsxKey.reset();
            activeCaptureQuadrant.store(4u, std::memory_order_release);
        };

        void* wrapperTrampoline{};
        auto status = MH_CreateHook(
            wrapper,
            reinterpret_cast<void*>(&hookWrapper),
            &wrapperTrampoline);
        if (status != MH_OK ||
            !isExecutableRange(wrapperTrampoline, 1)) {
            if (status == MH_OK) {
                (void)MH_RemoveHook(wrapper);
            }
            logging::error(
                "Skylighting native wrapper detour creation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        void* gpuCullingEnabledTrampoline{};
        status = MH_CreateHook(
            gpuCullingEnabled,
            reinterpret_cast<void*>(&hookGpuCullingEnabled),
            &gpuCullingEnabledTrampoline);
        if (status != MH_OK ||
            !isExecutableRange(gpuCullingEnabledTrampoline, 1)) {
            if (status == MH_OK) {
                (void)MH_RemoveHook(gpuCullingEnabled);
            }
            (void)MH_RemoveHook(wrapper);
            logging::error(
                "Skylighting native GPU-culling query detour creation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        void* pass14Trampoline{};
        status = MH_CreateHook(
            pass14Resolver,
            reinterpret_cast<void*>(&hookPass14Resolver),
            &pass14Trampoline);
        if (status != MH_OK || !isExecutableRange(pass14Trampoline, 1)) {
            if (status == MH_OK) {
                (void)MH_RemoveHook(pass14Resolver);
            }
            (void)MH_RemoveHook(gpuCullingEnabled);
            (void)MH_RemoveHook(wrapper);
            logging::error(
                "Skylighting native pass-14 detour creation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        originalWrapper = reinterpret_cast<WrapperFunction>(
            wrapperTrampoline);
        originalGpuCullingEnabled =
            reinterpret_cast<NativeGpuCullingEnabled>(
                gpuCullingEnabledTrampoline);
        originalSetViewFrustumVr = reinterpret_cast<SetViewFrustumVr>(
            preexistingSetViewFrustumCall.destination);
        originalPass14Resolver = reinterpret_cast<Pass14Resolver>(
            pass14Trampoline);
        nativeSkySingleton =
            reinterpret_cast<NativeSkySingleton>(
                image + kWrapperFirstCallTargetRva);
        nativeRender = reinterpret_cast<NativePrecipitationRender>(render);
        nativeProjection = reinterpret_cast<NativeProjectionSetup>(projection);
        projectionAnchorRoot = resolvedAnchorRoot;
        privateRenderGpuCullingReturnAddress =
            render + kRenderGpuCullingReturnOffset;

        status = MH_EnableHook(gpuCullingEnabled);
        DetourIdentity gpuCullingEnabledIdentity{};
        if (status != MH_OK ||
            !captureDetourIdentity(
                gpuCullingEnabled,
                gpuCullingEnabledIdentity)) {
            (void)MH_DisableHook(gpuCullingEnabled);
            (void)MH_RemoveHook(gpuCullingEnabled);
            (void)MH_RemoveHook(pass14Resolver);
            (void)MH_RemoveHook(wrapper);
            logging::error(
                "Skylighting native GPU-culling query detour activation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        status = MH_EnableHook(pass14Resolver);
        DetourIdentity pass14Identity{};
        if (status != MH_OK ||
            !captureDetourIdentity(pass14Resolver, pass14Identity)) {
            (void)MH_DisableHook(pass14Resolver);
            (void)MH_DisableHook(gpuCullingEnabled);
            (void)MH_RemoveHook(pass14Resolver);
            (void)MH_RemoveHook(gpuCullingEnabled);
            (void)MH_RemoveHook(wrapper);
            logging::error(
                "Skylighting native pass-14 detour activation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        status = MH_EnableHook(wrapper);
        DetourIdentity wrapperIdentity{};
        if (status != MH_OK ||
            !captureDetourIdentity(wrapper, wrapperIdentity)) {
            (void)MH_DisableHook(wrapper);
            (void)MH_DisableHook(pass14Resolver);
            (void)MH_DisableHook(gpuCullingEnabled);
            (void)MH_RemoveHook(wrapper);
            (void)MH_RemoveHook(pass14Resolver);
            (void)MH_RemoveHook(gpuCullingEnabled);
            logging::error(
                "Skylighting native wrapper detour activation failed: {} ({}).",
                MH_StatusToString(status),
                static_cast<int>(status));
            resetResolvedContracts();
            return false;
        }

        std::array<std::byte, kRelativeCallSize> originalFrustumCall{};
        std::memcpy(
            originalFrustumCall.data(),
            setViewFrustumVrCall,
            originalFrustumCall.size());
        auto* frustumCallThunk = createAbsoluteJumpThunk(
            setViewFrustumVrCall,
            reinterpret_cast<const void*>(&hookSetViewFrustumVr));
        std::array<std::byte, kRelativeCallSize> replacementFrustumCall{};
        DirectCallIdentity frustumCallIdentity{};
        const auto callEncoded = frustumCallThunk && encodeRelativeCall(
            setViewFrustumVrCall,
            frustumCallThunk,
            replacementFrustumCall);
        const auto callWritten = callEncoded && writeCallBytes(
            setViewFrustumVrCall,
            replacementFrustumCall);
        const auto callOwned = callWritten && captureDirectCallIdentity(
            setViewFrustumVrCall,
            frustumCallIdentity) &&
            frustumCallIdentity.destination == frustumCallThunk;
        if (!callOwned) {
            (void)writeCallBytes(
                setViewFrustumVrCall,
                originalFrustumCall);
            if (frustumCallThunk) {
                (void)VirtualFree(frustumCallThunk, 0, MEM_RELEASE);
            }
            (void)MH_DisableHook(wrapper);
            (void)MH_DisableHook(pass14Resolver);
            (void)MH_DisableHook(gpuCullingEnabled);
            (void)MH_RemoveHook(wrapper);
            (void)MH_RemoveHook(pass14Resolver);
            (void)MH_RemoveHook(gpuCullingEnabled);
            logging::error(
                "Skylighting could not own the precipitation-only VR-frustum callsite at RVA 0x00635A76; all native capture hooks were rolled back.");
            resetResolvedContracts();
            return false;
        }

        wrapperTarget = wrapper;
        gpuCullingEnabledTarget = gpuCullingEnabled;
        setViewFrustumVrCallsite = setViewFrustumVrCall;
        setViewFrustumVrCallThunk = frustumCallThunk;
        pass14Target = pass14Resolver;
        installedWrapperIdentity = wrapperIdentity;
        installedGpuCullingEnabledIdentity = gpuCullingEnabledIdentity;
        installedSetViewFrustumVrCallIdentity = frustumCallIdentity;
        installedPass14Identity = pass14Identity;
        passProducerReady.store(true, std::memory_order_release);
        installed.store(true, std::memory_order_release);
        Runtime::get().setNativeHookOwned(true);
        logging::info(
            "Installed verified FO4VR Skylighting quadrant capture and world-occlusion producer (wrapper RVA 0x00634300, precipitation VR-frustum callsite RVA 0x00635A76 preserving target RVA 0x01C2BFA0, scoped GPU-culling query RVA 0x027E0D50, pass-14 resolver RVA 0x0281CB50, accumulator collector RVA 0x0281E760, pass-list resolver RVA 0x027A51E0, utility shader RVA 0x0689B4F0).");
        return true;
    }

    bool validateNativeHooks(const char* trigger) noexcept
    {
        DetourIdentity currentWrapper{};
        const auto wrapperOwned = installed.load(std::memory_order_acquire) &&
            wrapperTarget && installedWrapperIdentity.patch &&
            installedWrapperIdentity.destination &&
            captureDetourIdentity(wrapperTarget, currentWrapper) &&
            currentWrapper.patch == installedWrapperIdentity.patch &&
            currentWrapper.destination ==
                installedWrapperIdentity.destination;
        DetourIdentity currentGpuCullingEnabled{};
        const auto gpuCullingEnabledOwned =
            installed.load(std::memory_order_acquire) &&
            gpuCullingEnabledTarget &&
            installedGpuCullingEnabledIdentity.patch &&
            installedGpuCullingEnabledIdentity.destination &&
            captureDetourIdentity(
                gpuCullingEnabledTarget,
                currentGpuCullingEnabled) &&
            currentGpuCullingEnabled.patch ==
                installedGpuCullingEnabledIdentity.patch &&
            currentGpuCullingEnabled.destination ==
                installedGpuCullingEnabledIdentity.destination;
        DirectCallIdentity currentSetViewFrustumVrCall{};
        const auto setViewFrustumVrOwned =
            installed.load(std::memory_order_acquire) &&
            setViewFrustumVrCallsite && setViewFrustumVrCallThunk &&
            installedSetViewFrustumVrCallIdentity.callsite &&
            installedSetViewFrustumVrCallIdentity.destination &&
            captureDirectCallIdentity(
                setViewFrustumVrCallsite,
                currentSetViewFrustumVrCall) &&
            currentSetViewFrustumVrCall.callsite ==
                installedSetViewFrustumVrCallIdentity.callsite &&
            currentSetViewFrustumVrCall.destination ==
                installedSetViewFrustumVrCallIdentity.destination &&
            currentSetViewFrustumVrCall.destination ==
                setViewFrustumVrCallThunk;
        DetourIdentity currentPass14{};
        const auto pass14Owned = installed.load(std::memory_order_acquire) &&
            pass14Target && installedPass14Identity.patch &&
            installedPass14Identity.destination &&
            captureDetourIdentity(pass14Target, currentPass14) &&
            currentPass14.patch == installedPass14Identity.patch &&
            currentPass14.destination == installedPass14Identity.destination;
        const auto utilityIdentity = inspectUtilityShader(
            utilityShader,
            utilityShaderVtable,
            utilityShaderSecondaryVtable);
        const auto utilityShaderOwned = utilityIdentity.valid();
        const auto owned = wrapperOwned && setViewFrustumVrOwned &&
            gpuCullingEnabledOwned && pass14Owned && utilityShaderOwned;
        passProducerReady.store(owned, std::memory_order_release);
        Runtime::get().setNativeHookOwned(owned);
        if (!owned && installed.load(std::memory_order_acquire)) {
            logging::error(
                "Skylighting native ownership validation failed at '{}' (wrapper={}, vrFrustumCall={}, gpuCullingQuery={}, pass14={}, utilityShader={}); ambient consumption and private capture are disabled.",
                trigger ? trigger : "unknown",
                wrapperOwned,
                setViewFrustumVrOwned,
                gpuCullingEnabledOwned,
                pass14Owned,
                utilityShaderOwned);
        }
        return owned;
    }

    bool readProjectionAnchor(float (&translate)[3]) noexcept
    {
        // The native render just dereferenced this node, so no page probe
        // is needed; only null links fail closed.
        auto* const* root = projectionAnchorRoot;
        auto* owner = root ? static_cast<std::byte*>(*root) : nullptr;
        auto* node = owner ?
            *reinterpret_cast<std::byte**>(owner + kAnchorNodeOffset) :
            nullptr;
        if (!node) {
            return false;
        }
        std::memcpy(
            translate,
            node + kNiAVObjectWorldTranslateOffset,
            sizeof(translate));
        return std::isfinite(translate[0]) && std::isfinite(translate[1]) &&
            std::isfinite(translate[2]);
    }
}
