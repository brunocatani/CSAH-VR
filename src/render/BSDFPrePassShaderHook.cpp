#include "render/BSDFPrePassShaderHook.h"

#include "render/D3D11Hooks.h"
#include "Features/lod_blending/LodBlendingRuntime.h"

#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace csah::render
{
    namespace
    {
        // Independently verified in Fallout4VR.exe 1.2.72. The constructor at
        // 0x142878930 publishes vtable 0x1430B8C68. Ghidra verifies slot 4 as
        // SetupTechnique(descriptor, techniqueState), slot 5 as the matching
        // RestoreTechnique(descriptor), and slot 9 as per-draw
        // SetupGeometry(pass, geometryState). SetupTechnique decodes the exact
        // descriptor through 0x1429379F0/0x142937A20/0x142937A50/
        // 0x142937A80. SetupGeometry receives the pass in RDX and the
        // geometry state in R8. At 0x14287D1AB it loads the exact per-draw
        // descriptor from geometryState + 0x40 (MOV EDX,[RDI+0x40], after
        // preserving R8 in RDI). The scoped technique owner covers retained
        // shaders; SetupGeometry publishes a one-draw reinforcement. Slot 3
        // merely constructs programs and is not a draw boundary.
        constexpr std::uintptr_t kVtableRva = 0x030B8C68;
        constexpr std::size_t kTechniqueSetupSlot = 4;
        constexpr std::uintptr_t kTechniqueSetupFunctionRva = 0x0287B720;
        constexpr std::size_t kTechniqueRestoreSlot = 5;
        constexpr std::uintptr_t kTechniqueRestoreFunctionRva = 0x028789B0;
        constexpr std::size_t kGeometrySetupSlot = 9;
        // 142891AF0 and 142891BB0 invoke slots 9 and 10 around 1428909C0;
        // that draw routine also dispatches cached records at pass+0x20.
        constexpr std::size_t kGeometryRestoreSlot = 10;
        constexpr std::uintptr_t kGeometryRestoreFunctionRva = 0x0287FC80;
        constexpr std::uintptr_t kGeometrySetupFunctionRva = 0x0287CF60;
        constexpr std::uintptr_t kGeometryDescriptorLoadRva = 0x0287D1AB;
        constexpr std::size_t kGeometryStateDescriptorOffset = 0x40;
        // Two independent raw-disassembly witnesses establish the first
        // three hops: BSDFPrePass SetupGeometry at 0x14287CF60 and general
        // BSLighting SetupGeometry at 0x1428B6B70 both read pass+0x18,
        // owner+0x178, and shader-property+0x58. The VR lighting-material
        // constructor 0x14280B8B0, destructor 0x14280B9F0, GetTextures slot
        // 0x14280C5F0, and texture-set loaders 0x142811E40/0x142811F10
        // independently identify material+0x38 as texture slot zero.
        constexpr std::size_t kPassGeometryOwnerOffset = 0x18;
        constexpr std::size_t kGeometryShaderPropertyOffset = 0x178;
        constexpr std::size_t kShaderPropertyMaterialOffset = 0x58;
        constexpr std::size_t kLightingMaterialBaseTextureOffset = 0x38;
        constexpr std::array<std::byte, 26> kTechniqueSetupSignature{
            std::byte{ 0x40 }, std::byte{ 0x53 }, std::byte{ 0x55 },
            std::byte{ 0x56 }, std::byte{ 0x57 }, std::byte{ 0x41 },
            std::byte{ 0x55 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x41 }, std::byte{ 0x57 }, std::byte{ 0x48 },
            std::byte{ 0x83 }, std::byte{ 0xEC }, std::byte{ 0x70 },
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xE9 },
            std::byte{ 0x8B }, std::byte{ 0xCA }, std::byte{ 0x4D },
            std::byte{ 0x8B }, std::byte{ 0xF8 }, std::byte{ 0x44 },
            std::byte{ 0x8B }, std::byte{ 0xF2 },
        };
        constexpr std::array<std::byte, 16> kTechniqueRestoreSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0x0D },
            std::byte{ 0x11 }, std::byte{ 0xD1 }, std::byte{ 0x9B },
            std::byte{ 0x03 }, std::byte{ 0x45 }, std::byte{ 0x33 },
            std::byte{ 0xC0 }, std::byte{ 0x0F }, std::byte{ 0xBA },
            std::byte{ 0xE2 }, std::byte{ 0x0C }, std::byte{ 0x73 },
            std::byte{ 0x35 },
        };
        constexpr std::array<std::byte, 55> kGeometrySetupSignature{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0xC4 },
            std::byte{ 0x48 }, std::byte{ 0x89 }, std::byte{ 0x50 },
            std::byte{ 0x10 }, std::byte{ 0x48 }, std::byte{ 0x89 },
            std::byte{ 0x48 }, std::byte{ 0x08 }, std::byte{ 0x55 },
            std::byte{ 0x53 }, std::byte{ 0x56 }, std::byte{ 0x57 },
            std::byte{ 0x41 }, std::byte{ 0x54 }, std::byte{ 0x41 },
            std::byte{ 0x55 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x41 }, std::byte{ 0x57 }, std::byte{ 0x48 },
            std::byte{ 0x8D }, std::byte{ 0xA8 }, std::byte{ 0x18 },
            std::byte{ 0xFE }, std::byte{ 0xFF }, std::byte{ 0xFF },
            std::byte{ 0x48 }, std::byte{ 0x81 }, std::byte{ 0xEC },
            std::byte{ 0xA8 }, std::byte{ 0x02 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x4C }, std::byte{ 0x8B },
            std::byte{ 0x72 }, std::byte{ 0x18 }, std::byte{ 0x0F },
            std::byte{ 0x29 }, std::byte{ 0x78 }, std::byte{ 0x98 },
            std::byte{ 0x48 }, std::byte{ 0xB8 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x10 }, std::byte{ 0x00 }, std::byte{ 0x00 },
            std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 3> kGeometryDescriptorLoadSignature{
            std::byte{ 0x8B }, std::byte{ 0x57 }, std::byte{ 0x40 },
        };

        using SetupTechniqueFunction = bool(__fastcall*)(
            void* receiver,
            std::uint32_t descriptor,
            void* techniqueState);
        using RestoreTechniqueFunction = void(__fastcall*)(
            void* receiver,
            std::uint32_t descriptor);
        using SetupGeometryFunction = void(__fastcall*)(
            void* receiver,
            void* pass,
            void* geometryState);

        SetupTechniqueFunction originalSetupTechnique{};
        RestoreTechniqueFunction originalRestoreTechnique{};
        SetupGeometryFunction originalSetupGeometry{};
        using RestoreGeometryFunction=void(__fastcall*)(void*,void*);
        RestoreGeometryFunction originalRestoreGeometry{};
        void** techniqueSetupCell{};
        void** techniqueRestoreCell{};
        void** geometrySetupCell{};
        void** geometryRestoreCell{};
        std::atomic_bool installed{};
        std::atomic_uint64_t techniqueSetupCalls{};
        std::atomic_uint64_t techniqueRestoreCalls{};
        std::atomic_uint64_t geometrySetupCalls{};
        std::atomic_uint64_t validationFailures{};
        std::atomic_uint32_t lastDescriptor{};

        constexpr std::uint32_t kLinearLightingConsumer = 1u << 0;
        constexpr std::uint32_t kComplexEnvironmentConsumer = 1u << 1;
        constexpr std::uint32_t kIblConsumer = 1u << 2;
        constexpr std::uint32_t kSurfaceClassificationConsumer = 1u << 3;
        constexpr std::uint32_t kAuthoredPbrConsumer = 1u << 4;
        constexpr std::uint32_t kLodBlendingConsumer = 1u << 5;
        constexpr std::uint32_t kTerrainVariationConsumer = 1u << 6;
        std::atomic_uint32_t descriptorConsumerMask{};
        std::atomic_bool authoredPbrEnabled{};
        std::atomic_uint32_t authoredPbrPointerFailureMask{};

        [[nodiscard]] bool plausibleEnginePointer(
            const void* pointer) noexcept
        {
            const auto value = reinterpret_cast<std::uintptr_t>(pointer);
            constexpr auto minimum = std::uintptr_t{ 0x10000 };
            constexpr auto maximum = std::uintptr_t{ 0x00007FFFFFFFFFFF };
            return value >= minimum && value <= maximum &&
                (value & (alignof(void*) - 1)) == 0;
        }

        void recordAuthoredPbrPointerFailure(
            std::uint32_t stage,
            const char* name) noexcept
        {
            const auto previous = authoredPbrPointerFailureMask.fetch_or(
                stage,
                std::memory_order_relaxed);
            if ((previous & stage) == 0) {
                logging::warn(
                    "Authored PBR base-texture publication failed closed at the verified '{}' pointer hop.",
                    name);
            }
        }

        [[nodiscard]] bool readable(
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
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                    sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto regionStart =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            return start >= regionStart &&
                start + size <= regionStart + information.RegionSize;
        }

        [[nodiscard]] bool executable(
            const void* address,
            std::size_t size) noexcept
        {
            if (!readable(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            constexpr DWORD mask = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & mask) != 0;
        }

        [[nodiscard]] bool patchPointer(
            void** cell,
            void* expected,
            void* replacement) noexcept
        {
            if (!readable(cell, sizeof(*cell)) || *cell != expected) {
                return false;
            }
            DWORD oldProtection{};
            if (!VirtualProtect(
                    cell,
                    sizeof(*cell),
                    PAGE_READWRITE,
                    &oldProtection)) {
                return false;
            }
            *cell = replacement;
            DWORD discarded{};
            if (!VirtualProtect(
                cell,
                sizeof(*cell),
                oldProtection,
                &discarded)) {
                // The page is still writable, so restore the engine target
                // before reporting failure. Never leave a replacement live
                // unless its original remains callable and complete ownership
                // is published.
                if (*cell == replacement) {
                    *cell = expected;
                }
                DWORD rollbackProtection{};
                (void)VirtualProtect(
                    cell,
                    sizeof(*cell),
                    oldProtection,
                    &rollbackProtection);
                FlushInstructionCache(
                    GetCurrentProcess(), cell, sizeof(*cell));
                return false;
            }
            FlushInstructionCache(GetCurrentProcess(), cell, sizeof(*cell));
            return *cell == replacement;
        }

        void setDescriptorConsumer(
            std::uint32_t consumer,
            bool enabled) noexcept
        {
            if (enabled) {
                descriptorConsumerMask.fetch_or(
                    consumer,
                    std::memory_order_release);
            } else {
                descriptorConsumerMask.fetch_and(
                    ~consumer,
                    std::memory_order_release);
            }
        }

        bool __fastcall hookSetupTechnique(
            void* receiver,
            std::uint32_t descriptor,
            void* techniqueState) noexcept
        {
            const auto original = originalSetupTechnique;
            if (!original) {
                return false;
            }
            if (!installed.load(std::memory_order_acquire) ||
                descriptorConsumerMask.load(std::memory_order_acquire) == 0) {
                return original(receiver, descriptor, techniqueState);
            }

            beginDFPrePassTechnique(descriptor);
            const auto accepted = original(
                receiver,
                descriptor,
                techniqueState);
            if (!accepted) {
                endDFPrePassTechnique(descriptor);
                return false;
            }
            const auto previousCalls = techniqueSetupCalls.fetch_add(
                1,
                std::memory_order_relaxed);
            lastDescriptor.store(descriptor, std::memory_order_relaxed);
            if (previousCalls == 0) {
                logging::info(
                    "BSDFPrePass SetupTechnique opened its first exact descriptor lifetime 0x{:08X}.",
                    descriptor);
            }
            return true;
        }

        void __fastcall hookRestoreTechnique(
            void* receiver,
            std::uint32_t descriptor) noexcept
        {
            const auto original = originalRestoreTechnique;
            if (!original) {
                return;
            }
            if (!installed.load(std::memory_order_acquire)) {
                original(receiver, descriptor);
                return;
            }
            endDFPrePassTechnique(descriptor);
            techniqueRestoreCalls.fetch_add(1, std::memory_order_relaxed);
            original(receiver, descriptor);
        }

        void __fastcall hookSetupGeometry(
            void* receiver,
            void* pass,
            void* geometryState) noexcept
        {
            const auto original = originalSetupGeometry;
            if (!original) {
                return;
            }

            if (!installed.load(std::memory_order_acquire) ||
                descriptorConsumerMask.load(std::memory_order_acquire) == 0 ||
                !pass || !geometryState) {
                original(receiver, pass, geometryState);
                return;
            }

            // Ghidra verification of Fallout4VR.exe 1.2.72 at 0x14287CF60
            // confirms that native SetupGeometry performs unchecked reads
            // from the mandatory pass and geometry-state arguments.
            // A per-draw VirtualQuery adds no safety and is prohibitively
            // expensive in this renderer hot path.
            std::uint32_t descriptor{};
            std::memcpy(
                &descriptor,
                static_cast<const std::byte*>(geometryState) +
                    kGeometryStateDescriptorOffset,
                sizeof(descriptor));
            const auto previousCalls = geometrySetupCalls.fetch_add(
                1,
                std::memory_order_relaxed);
            lastDescriptor.store(descriptor, std::memory_order_relaxed);
            if (previousCalls == 0) {
                logging::info(
                    "BSDFPrePass SetupGeometry produced its first exact draw descriptor 0x{:08X}.",
                    descriptor);
            }
            RE::NiTexture* baseTexture{};
            if (authoredPbrEnabled.load(std::memory_order_acquire)) {
                void* geometryOwner{};
                if (!plausibleEnginePointer(pass)) {
                    recordAuthoredPbrPointerFailure(1u << 0, "pass owner");
                } else {
                    std::memcpy(
                        &geometryOwner,
                        static_cast<const std::byte*>(pass) +
                            kPassGeometryOwnerOffset,
                        sizeof(geometryOwner));
                    if (!plausibleEnginePointer(geometryOwner)) {
                        recordAuthoredPbrPointerFailure(
                            1u << 0, "pass owner");
                    }
                }
                if (plausibleEnginePointer(geometryOwner)) {
                    void* shaderProperty{};
                    std::memcpy(
                        &shaderProperty,
                        static_cast<const std::byte*>(geometryOwner) +
                            kGeometryShaderPropertyOffset,
                        sizeof(shaderProperty));
                    if (!plausibleEnginePointer(shaderProperty)) {
                        recordAuthoredPbrPointerFailure(
                            1u << 1, "shader property");
                    } else {
                        void* material{};
                        std::memcpy(
                            &material,
                            static_cast<const std::byte*>(shaderProperty) +
                                kShaderPropertyMaterialOffset,
                            sizeof(material));
                        if (!plausibleEnginePointer(material)) {
                            recordAuthoredPbrPointerFailure(
                                1u << 2, "lighting material");
                        } else {
                            std::memcpy(
                                &baseTexture,
                                static_cast<const std::byte*>(material) +
                                    kLightingMaterialBaseTextureOffset,
                                sizeof(baseTexture));
                            if (!plausibleEnginePointer(baseTexture)) {
                                recordAuthoredPbrPointerFailure(
                                    1u << 3, "base texture");
                                baseTexture = nullptr;
                            }
                        }
                    }
                }
            }
            lod_blending::Runtime::get().beginGeometry(pass);
            publishDFPrePassGeometry(descriptor, baseTexture);
            original(receiver, pass, geometryState);
        }
        void __fastcall hookRestoreGeometry(void* receiver,void* pass) noexcept
        {
            lod_blending::Runtime::get().endGeometry();
            if(originalRestoreGeometry) originalRestoreGeometry(receiver,pass);
        }
    }

    bool installBSDFPrePassShaderHook() noexcept
    {
        if (installed.load(std::memory_order_acquire)) {
            return true;
        }
        auto* image = reinterpret_cast<std::byte*>(
            GetModuleHandleW(nullptr));
        if (!image) {
            logging::error(
                "BSDFPrePass descriptor hook could not resolve Fallout4VR.exe.");
            return false;
        }
        auto** nextTechniqueSetupCell = reinterpret_cast<void**>(
            image + kVtableRva + kTechniqueSetupSlot * sizeof(void*));
        auto** nextTechniqueRestoreCell = reinterpret_cast<void**>(
            image + kVtableRva + kTechniqueRestoreSlot * sizeof(void*));
        auto** nextGeometrySetupCell = reinterpret_cast<void**>(
            image + kVtableRva + kGeometrySetupSlot * sizeof(void*));
        auto** nextGeometryRestoreCell = reinterpret_cast<void**>(
            image+kVtableRva+kGeometryRestoreSlot*sizeof(void*));
        auto* expectedGeometryRestore=image+kGeometryRestoreFunctionRva;
        auto* expectedTechniqueSetup = image + kTechniqueSetupFunctionRva;
        auto* expectedTechniqueRestore = image + kTechniqueRestoreFunctionRva;
        auto* expectedGeometrySetup = image + kGeometrySetupFunctionRva;
        auto* expectedGeometryDescriptorLoad =
            image + kGeometryDescriptorLoadRva;
        const auto techniqueSetupCellMatches =
            readable(nextTechniqueSetupCell, sizeof(*nextTechniqueSetupCell)) &&
            *nextTechniqueSetupCell == expectedTechniqueSetup;
        const auto techniqueRestoreCellMatches =
            readable(
                nextTechniqueRestoreCell,
                sizeof(*nextTechniqueRestoreCell)) &&
            *nextTechniqueRestoreCell == expectedTechniqueRestore;
        const auto geometrySetupCellMatches =
            readable(nextGeometrySetupCell, sizeof(*nextGeometrySetupCell)) &&
            *nextGeometrySetupCell == expectedGeometrySetup;
        const auto techniqueSetupCodeMatches =
            executable(
                expectedTechniqueSetup,
                kTechniqueSetupSignature.size()) &&
            std::memcmp(
                expectedTechniqueSetup,
                kTechniqueSetupSignature.data(),
                kTechniqueSetupSignature.size()) == 0;
        const auto techniqueRestoreCodeMatches =
            executable(
                expectedTechniqueRestore,
                kTechniqueRestoreSignature.size()) &&
            std::memcmp(
                expectedTechniqueRestore,
                kTechniqueRestoreSignature.data(),
                kTechniqueRestoreSignature.size()) == 0;
        const auto geometrySetupCodeMatches =
            executable(
                expectedGeometrySetup,
                kGeometrySetupSignature.size()) &&
            std::memcmp(
                expectedGeometrySetup,
                kGeometrySetupSignature.data(),
                kGeometrySetupSignature.size()) == 0;
        const auto geometryDescriptorLoadMatches =
            executable(
                expectedGeometryDescriptorLoad,
                kGeometryDescriptorLoadSignature.size()) &&
            std::memcmp(
                expectedGeometryDescriptorLoad,
                kGeometryDescriptorLoadSignature.data(),
                kGeometryDescriptorLoadSignature.size()) == 0;
        if (!readable(nextGeometryRestoreCell,sizeof(*nextGeometryRestoreCell)) ||
            *nextGeometryRestoreCell!=expectedGeometryRestore || !executable(expectedGeometryRestore,16) ||
            !techniqueSetupCellMatches || !techniqueRestoreCellMatches ||
            !geometrySetupCellMatches || !techniqueSetupCodeMatches ||
            !techniqueRestoreCodeMatches || !geometrySetupCodeMatches ||
            !geometryDescriptorLoadMatches) {
            logging::error(
                "BSDFPrePass descriptor hook live identity gate failed (setupCell={} setupCode={} restoreCell={} restoreCode={} geometryCell={} geometryCode={} geometryDescriptorLoad={}); complex environment materials remain fail-closed.",
                techniqueSetupCellMatches,
                techniqueSetupCodeMatches,
                techniqueRestoreCellMatches,
                techniqueRestoreCodeMatches,
                geometrySetupCellMatches,
                geometrySetupCodeMatches,
                geometryDescriptorLoadMatches);
            return false;
        }

        originalSetupTechnique = reinterpret_cast<SetupTechniqueFunction>(
            expectedTechniqueSetup);
        originalRestoreTechnique = reinterpret_cast<RestoreTechniqueFunction>(
            expectedTechniqueRestore);
        originalSetupGeometry = reinterpret_cast<SetupGeometryFunction>(
            expectedGeometrySetup);
        originalRestoreGeometry=reinterpret_cast<RestoreGeometryFunction>(expectedGeometryRestore);
        const auto techniqueSetupPatched = patchPointer(
            nextTechniqueSetupCell,
            expectedTechniqueSetup,
            reinterpret_cast<void*>(&hookSetupTechnique));
        const auto techniqueRestorePatched = techniqueSetupPatched &&
            patchPointer(
                nextTechniqueRestoreCell,
                expectedTechniqueRestore,
                reinterpret_cast<void*>(&hookRestoreTechnique));
        const auto geometrySetupPatched = techniqueRestorePatched &&
            patchPointer(
                nextGeometrySetupCell,
                expectedGeometrySetup,
                reinterpret_cast<void*>(&hookSetupGeometry));
        const auto geometryRestorePatched=geometrySetupPatched && patchPointer(
            nextGeometryRestoreCell,expectedGeometryRestore,reinterpret_cast<void*>(&hookRestoreGeometry));
        if (!geometryRestorePatched) {
            auto rollbackComplete = true;
            if(geometrySetupPatched) rollbackComplete=patchPointer(nextGeometrySetupCell,
                reinterpret_cast<void*>(&hookSetupGeometry),expectedGeometrySetup);
            if (techniqueRestorePatched) {
                rollbackComplete = patchPointer(
                    nextTechniqueRestoreCell,
                    reinterpret_cast<void*>(&hookRestoreTechnique),
                    expectedTechniqueRestore) && rollbackComplete;
            }
            if (techniqueSetupPatched) {
                rollbackComplete = patchPointer(
                    nextTechniqueSetupCell,
                    reinterpret_cast<void*>(&hookSetupTechnique),
                    expectedTechniqueSetup) && rollbackComplete;
            }
            logging::error(
                "BSDFPrePass descriptor vtable transaction failed (rollbackComplete={}); all descriptor consumers remain fail-closed.",
                rollbackComplete);
            return false;
        }
        techniqueSetupCell = nextTechniqueSetupCell;
        techniqueRestoreCell = nextTechniqueRestoreCell;
        geometrySetupCell = nextGeometrySetupCell;
        geometryRestoreCell = nextGeometryRestoreCell;
        installed.store(true, std::memory_order_release);
        logging::info(
            "Installed verified Fallout4VR BSDFPrePass descriptor lifetime hooks (vtable 0x030B8C68: SetupTechnique slot 4 RVA 0x0287B720, RestoreTechnique slot 5 RVA 0x028789B0, SetupGeometry slot 9 RVA 0x0287CF60, geometry-state descriptor +0x40 verified at RVA 0x0287D1AB).");
        return true;
    }

    bool validateBSDFPrePassShaderHook(const char* trigger) noexcept
    {
        const auto hookInstalled =
            installed.load(std::memory_order_acquire);
        const auto owned = hookInstalled &&
            readable(techniqueSetupCell, sizeof(*techniqueSetupCell)) &&
            readable(techniqueRestoreCell, sizeof(*techniqueRestoreCell)) &&
            readable(geometrySetupCell, sizeof(*geometrySetupCell)) &&
            *techniqueSetupCell ==
                reinterpret_cast<void*>(&hookSetupTechnique) &&
            *techniqueRestoreCell ==
                reinterpret_cast<void*>(&hookRestoreTechnique) &&
            *geometrySetupCell ==
                reinterpret_cast<void*>(&hookSetupGeometry) &&
            readable(geometryRestoreCell,sizeof(*geometryRestoreCell)) &&
            *geometryRestoreCell==reinterpret_cast<void*>(&hookRestoreGeometry);
        if (!owned && hookInstalled) {
            installed.store(false, std::memory_order_release);
            const auto failures = validationFailures.fetch_add(
                                      1,
                                      std::memory_order_relaxed) +
                1;
            if ((failures & (failures - 1)) == 0) {
                logging::error(
                    "BSDFPrePass descriptor hook ownership failed at '{}' (failures={}); complex environment materials remain fail-closed.",
                    trigger ? trigger : "unknown",
                    failures);
            }
        }
        return owned;
    }

    void setDFPrePassLinearLightingEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kLinearLightingConsumer, enabled);
    }

    void setDFPrePassComplexEnvironmentEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kComplexEnvironmentConsumer, enabled);
    }

    void setDFPrePassIblEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kIblConsumer, enabled);
    }

    void setDFPrePassSurfaceClassificationEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kSurfaceClassificationConsumer, enabled);
    }

    void setDFPrePassTerrainVariationEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kTerrainVariationConsumer,enabled);
    }

    void setDFPrePassLodBlendingEnabled(bool enabled) noexcept
    {
        setDescriptorConsumer(kLodBlendingConsumer,enabled);
    }

    void setDFPrePassAuthoredPbrEnabled(bool enabled) noexcept
    {
        authoredPbrEnabled.store(enabled, std::memory_order_release);
        setDescriptorConsumer(kAuthoredPbrConsumer, enabled);
    }

    DFPrePassHookSnapshot dFPrePassHookSnapshot() noexcept
    {
        const auto hookInstalled =
            installed.load(std::memory_order_acquire);
        return {
            .installed = hookInstalled,
            .vtableCellOwned = hookInstalled &&
                readable(techniqueSetupCell, sizeof(*techniqueSetupCell)) &&
                readable(
                    techniqueRestoreCell,
                    sizeof(*techniqueRestoreCell)) &&
                readable(geometrySetupCell, sizeof(*geometrySetupCell)) &&
                *techniqueSetupCell ==
                    reinterpret_cast<void*>(&hookSetupTechnique) &&
                *techniqueRestoreCell ==
                    reinterpret_cast<void*>(&hookRestoreTechnique) &&
                *geometrySetupCell ==
                    reinterpret_cast<void*>(&hookSetupGeometry) &&
                readable(geometryRestoreCell,sizeof(*geometryRestoreCell)) &&
                *geometryRestoreCell==reinterpret_cast<void*>(&hookRestoreGeometry),
            .setupCalls = geometrySetupCalls.load(std::memory_order_relaxed),
            .validationFailures =
                validationFailures.load(std::memory_order_relaxed),
            .lastDescriptor =
                lastDescriptor.load(std::memory_order_relaxed),
        };
    }
}
