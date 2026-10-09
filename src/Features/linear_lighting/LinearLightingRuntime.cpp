#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/lod_blending/LodBlendingRuntime.h"

#include "Features/complex_materials/ComplexParallaxModel.h"
#include "Features/complex_materials/ComplexEnvironmentMaterialModel.h"
#include "Features/complex_materials/ComplexMaterialProducerModel.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/linear_lighting/DFLightAmbientShaderPatch.h"
#include "Features/linear_lighting/DFTiledPointLightHook.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"

#include "render/BSLightingGeometryHook.h"
#include "render/BSDFPrePassShaderHook.h"
#include "resources.h"
#include "support/Logger.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "ComplexParallaxLandscapeBase.h"
#include "ComplexParallaxLandscapeInstancedLod.h"
#include "ComplexParallaxLandscapeLod.h"
#include "SurfaceClassComplexParallaxLandscapeBase.h"
#include "SurfaceClassComplexParallaxLandscapeInstancedLod.h"
#include "SurfaceClassComplexParallaxLandscapeLod.h"
#include "SkylightingAmbient.h"

namespace csah::linear_lighting
{
    namespace
    {
        struct DxbcIdentity
        {
            std::size_t size{};
            std::array<std::byte, 16> checksum{};
        };

        struct ShaderContractDefinition
        {
            const char* name{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct DFLightAmbientContractDefinition
        {
            const char* name{};
            DxbcIdentity original{};
            DFLightAmbientGammaOffsets gammaOffsets{};
        };

        struct SkyShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct DistantTreeShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct ParticleShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct WaterShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct VLSCompositeShaderContractDefinition
        {
            const char* name{};
            std::uint32_t imageSpaceIndex{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct EffectShaderContractDefinition
        {
            const char* name{};
            std::uint32_t descriptor{};
            int resourceId{};
            DxbcIdentity original{};
            DxbcIdentity replacement{};
        };

        struct DFLightAmbientDescriptorContract
        {
            std::uint32_t descriptor{};
            std::uint32_t contractIndex{};
        };

        struct SurfaceClassContractDefinition
        {
            int resourceId{};
            std::size_t replacementSize{};
            std::array<std::byte, 16> replacementChecksum{};
        };

        struct SpecializedSurfaceClassContractDefinition
        {
            std::uint32_t contractIndex{};
            std::uint32_t classCode{};
            int resourceId{};
            std::size_t replacementSize{};
            std::array<std::byte, 16> replacementChecksum{};
        };

        struct SurfaceClassDescriptorContract
        {
            std::uint32_t descriptor{};
            std::uint32_t contractIndex{};
            std::uint32_t classCode{};
            std::uint32_t variantIndexPlusOne{};
        };

        struct SurfaceClassMaterialContract
        {
            std::uint32_t classCode{};
            std::uint32_t variantIndexPlusOne{};
            std::uint32_t grassVariantIndexPlusOne{};
        };

        constexpr auto kAmbiguousSurfaceClassCode =
            (std::numeric_limits<std::uint32_t>::max)();

        #include "Features/linear_lighting/GeneratedLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedSkyLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedDistantTreeLinearLightingContract.inl"
        #include "Features/linear_lighting/GeneratedParticleLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedWaterLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedVLSCompositeLinearLightingContract.inl"
        #include "Features/linear_lighting/GeneratedEffectLinearLightingContracts.inl"
        #include "Features/linear_lighting/GeneratedDFLightAmbientContracts.inl"
        #include "Features/surface_classification/GeneratedSurfaceClassContracts.inl"

        static_assert(
            csah_skylighting_ambient_contracts.size() ==
            kDFLightAmbientContracts.size());

        [[nodiscard]] const SurfaceClassDescriptorContract*
            surfaceClassDescriptorContract(
            std::uint32_t descriptor) noexcept
        {
            const auto found = std::lower_bound(
                kSurfaceClassDescriptorContracts.begin(),
                kSurfaceClassDescriptorContracts.end(),
                descriptor,
                [](const SurfaceClassDescriptorContract& contract,
                    std::uint32_t value) {
                    return contract.descriptor < value;
                });
            if (found == kSurfaceClassDescriptorContracts.end() ||
                found->descriptor != descriptor) {
                return nullptr;
            }
            return &*found;
        }

        [[nodiscard]] const SurfaceClassDescriptorContract*
            surfaceClassDescriptorContract(
            std::uint32_t descriptor,
            std::size_t contractIndex) noexcept
        {
            const auto* found = surfaceClassDescriptorContract(descriptor);
            return found && found->contractIndex == contractIndex ?
                found : nullptr;
        }

        [[nodiscard]] std::size_t specializedSurfaceClassSlot(
            std::size_t contractIndex,
            std::uint32_t classCode,
            std::uint32_t variantIndexPlusOne) noexcept
        {
            if (variantIndexPlusOne == 0) {
                return kSpecializedSurfaceClassContracts.size();
            }
            const auto slot = static_cast<std::size_t>(
                variantIndexPlusOne - 1);
            if (slot >= kSpecializedSurfaceClassContracts.size()) {
                return kSpecializedSurfaceClassContracts.size();
            }
            const auto& variant = kSpecializedSurfaceClassContracts[slot];
            return variant.contractIndex == contractIndex &&
                    variant.classCode == classCode ?
                slot : kSpecializedSurfaceClassContracts.size();
        }

        constexpr std::array<std::byte, 16> kLandscapeBaseChecksum{
            std::byte{ 0x60 }, std::byte{ 0x48 }, std::byte{ 0x32 },
            std::byte{ 0x66 }, std::byte{ 0x00 }, std::byte{ 0xE5 },
            std::byte{ 0xA2 }, std::byte{ 0x6D }, std::byte{ 0xDB },
            std::byte{ 0xFA }, std::byte{ 0x65 }, std::byte{ 0x16 },
            std::byte{ 0x6D }, std::byte{ 0x0C }, std::byte{ 0x08 },
            std::byte{ 0x54 },
        };
        constexpr std::array<std::byte, 16> kLandscapeLodChecksum{
            std::byte{ 0x95 }, std::byte{ 0xB7 }, std::byte{ 0x4A },
            std::byte{ 0xBA }, std::byte{ 0x9C }, std::byte{ 0x1B },
            std::byte{ 0xEC }, std::byte{ 0xD8 }, std::byte{ 0xF7 },
            std::byte{ 0x2E }, std::byte{ 0xA6 }, std::byte{ 0xAD },
            std::byte{ 0xEF }, std::byte{ 0x4C }, std::byte{ 0xD4 },
            std::byte{ 0x33 },
        };
        constexpr std::array<std::byte, 16> kLandscapeInstancedLodChecksum{
            std::byte{ 0x61 }, std::byte{ 0xCC }, std::byte{ 0x59 },
            std::byte{ 0xEB }, std::byte{ 0x16 }, std::byte{ 0x52 },
            std::byte{ 0x8E }, std::byte{ 0xA2 }, std::byte{ 0x9E },
            std::byte{ 0xE6 }, std::byte{ 0x14 }, std::byte{ 0x9F },
            std::byte{ 0xC7 }, std::byte{ 0x8D }, std::byte{ 0xD0 },
            std::byte{ 0x46 },
        };

        static_assert(kShaderContracts[273].original.size == 5632u);
        static_assert(kShaderContracts[273].original.checksum ==
                      kLandscapeBaseChecksum);
        static_assert(kShaderContracts[274].original.size == 7020u);
        static_assert(kShaderContracts[274].original.checksum ==
                      kLandscapeLodChecksum);
        static_assert(kShaderContracts[275].original.size == 7204u);
        static_assert(kShaderContracts[275].original.checksum ==
                      kLandscapeInstancedLodChecksum);

        struct EmbeddedShader
        {
            const void* data{};
            std::size_t size{};
        };

        constexpr std::array<EmbeddedShader, 3> kComplexParallaxShaders{
            EmbeddedShader{
                csah_complex_parallax_landscape_base,
                sizeof(csah_complex_parallax_landscape_base),
            },
            EmbeddedShader{
                csah_complex_parallax_landscape_lod,
                sizeof(csah_complex_parallax_landscape_lod),
            },
            EmbeddedShader{
                csah_complex_parallax_landscape_instanced_lod,
                sizeof(csah_complex_parallax_landscape_instanced_lod),
            },
        };

        constexpr std::array<EmbeddedShader, 3>
            kSurfaceClassComplexParallaxShaders{
                EmbeddedShader{
                    csah_surface_class_complex_parallax_landscape_base,
                    sizeof(
                        csah_surface_class_complex_parallax_landscape_base),
                },
                EmbeddedShader{
                    csah_surface_class_complex_parallax_landscape_lod,
                    sizeof(
                        csah_surface_class_complex_parallax_landscape_lod),
                },
                EmbeddedShader{
                    csah_surface_class_complex_parallax_landscape_instanced_lod,
                    sizeof(
                        csah_surface_class_complex_parallax_landscape_instanced_lod),
                },
            };

        [[nodiscard]] EmbeddedShader loadEmbeddedShader(int resourceId) noexcept
        {
            HMODULE module{};
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&loadEmbeddedShader),
                    &module)) {
                return {};
            }

            const auto resource = FindResourceW(
                module,
                MAKEINTRESOURCEW(resourceId),
                RT_RCDATA);
            if (!resource) {
                return {};
            }
            const auto loaded = LoadResource(module, resource);
            if (!loaded) {
                return {};
            }
            const auto size = SizeofResource(module, resource);
            const auto* data = LockResource(loaded);
            return { data, static_cast<std::size_t>(size) };
        }

        [[nodiscard]] bool matchesDxbcIdentity(
            const void* bytecode,
            std::size_t bytecodeLength,
            std::size_t expectedLength,
            const std::array<std::byte, 16>& expectedChecksum) noexcept
        {
            return bytecode && bytecodeLength == expectedLength &&
                bytecodeLength >= 20 &&
                std::memcmp(bytecode, "DXBC", 4) == 0 &&
                std::memcmp(
                    static_cast<const std::byte*>(bytecode) + 4,
                    expectedChecksum.data(),
                    expectedChecksum.size()) == 0;
        }

        [[nodiscard]] constexpr std::uint64_t encodeShaderBinding(
            ReplacementShaderBinding binding) noexcept
        {
            return static_cast<std::uint64_t>(binding.family) |
                (static_cast<std::uint64_t>(binding.constantFlags) << 8) |
                (static_cast<std::uint64_t>(binding.contractPlusOne) << 16);
        }

        [[nodiscard]] constexpr ReplacementShaderBinding decodeShaderBinding(
            std::uint64_t encoded) noexcept
        {
            return {
                static_cast<ReplacementShaderFamily>(encoded & 0xFFu),
                static_cast<std::uint32_t>(encoded >> 16),
                static_cast<std::uint8_t>((encoded >> 8) & 0xFFu),
            };
        }

        [[nodiscard]] D3D11_BUFFER_DESC makeConstantBufferDescription(
            std::uint32_t byteWidth) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = byteWidth;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            return description;
        }
    }

    ScopedReplacementPixelConstants::ScopedReplacementPixelConstants(
        ID3D11DeviceContext* context,
        ID3D11Buffer* frameBuffer,
        ID3D11Buffer* geometryBuffer,
        ID3D11Buffer* complexEnvironmentBuffer,
        std::uint8_t constantFlags,
        std::atomic_uint64_t* restoreCounter) noexcept :
        context_(context),
        constantFlags_(constantFlags),
        restoreCounter_(restoreCounter)
    {
        if ((constantFlags_ & ReplacementPixelConstants_Frame) != 0) {
            ID3D11Buffer* previousFrameBuffer{};
            context_->PSGetConstantBuffers(5, 1, &previousFrameBuffer);
            previousFrameBuffer_.Attach(previousFrameBuffer);
            context_->PSSetConstantBuffers(5, 1, &frameBuffer);
        }
        if ((constantFlags_ & ReplacementPixelConstants_Geometry) != 0) {
            ID3D11Buffer* previousGeometryBuffer{};
            context_->PSGetConstantBuffers(8, 1, &previousGeometryBuffer);
            previousGeometryBuffer_.Attach(previousGeometryBuffer);
            context_->PSSetConstantBuffers(8, 1, &geometryBuffer);
        }
        if ((constantFlags_ &
                ReplacementPixelConstants_ComplexEnvironment) != 0) {
            ID3D11Buffer* previousComplexEnvironmentBuffer{};
            context_->PSGetConstantBuffers(
                11,
                1,
                &previousComplexEnvironmentBuffer);
            previousComplexEnvironmentBuffer_.Attach(
                previousComplexEnvironmentBuffer);
            context_->PSSetConstantBuffers(
                11,
                1,
                &complexEnvironmentBuffer);
        }
    }

    ScopedReplacementPixelConstants::~ScopedReplacementPixelConstants() noexcept
    {
        if (!context_) {
            return;
        }

        if ((constantFlags_ & ReplacementPixelConstants_Frame) != 0) {
            auto* previousFrameBuffer = previousFrameBuffer_.Get();
            context_->PSSetConstantBuffers(5, 1, &previousFrameBuffer);
        }
        if ((constantFlags_ & ReplacementPixelConstants_Geometry) != 0) {
            auto* previousGeometryBuffer = previousGeometryBuffer_.Get();
            context_->PSSetConstantBuffers(8, 1, &previousGeometryBuffer);
        }
        if ((constantFlags_ &
                ReplacementPixelConstants_ComplexEnvironment) != 0) {
            auto* previousComplexEnvironmentBuffer =
                previousComplexEnvironmentBuffer_.Get();
            context_->PSSetConstantBuffers(
                11,
                1,
                &previousComplexEnvironmentBuffer);
        }
        restoreCounter_->fetch_add(1, std::memory_order_relaxed);
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::onDeviceCreated(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        try {
            if (!device || !context || !createPixelShader) {
                logging::error(
                    "Linear Lighting rejected incomplete D3D11 device capture.");
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
            context->GetDevice(contextDevice.GetAddressOf());
            if (contextDevice.Get() != device) {
                logging::error(
                    "Linear Lighting rejected mismatched D3D11 device/context identity.");
                return;
            }

            device_ = device;
            context_ = context;
            createPixelShader_ = createPixelShader;
            if (!createResources(device, createPixelShader)) {
                createPixelShader_ = nullptr;
                device_.Reset();
                context_.Reset();
                return;
            }

            gpuResourcesReady_.store(true, std::memory_order_release);
            logging::info(
                "Linear Lighting GPU resources ready (materialContracts={}, skyContracts={}, distantTreeContracts={}, particleContracts={}, waterContracts={}, vlsCompositeContracts={}, effectContracts={}, complexParallaxReady={}); active shader replacement remains {}.",
                kShaderContracts.size(),
                kSkyShaderContracts.size(),
                kDistantTreeShaderContractCount,
                kParticleShaderContracts.size(),
                kWaterShaderContracts.size(),
                kVLSCompositeShaderContractCount,
                kEffectShaderContracts.size(),
                complexParallaxResourcesReady_.load(
                    std::memory_order_relaxed),
                enabled_.load(std::memory_order_relaxed) ? "enabled" : "disabled");
        } catch (const std::exception& error) {
            logging::error(
                "Linear Lighting D3D11 initialization failed: {}",
                error.what());
        } catch (...) {
            logging::error(
                "Linear Lighting D3D11 initialization failed with an unknown exception.");
        }
    }

    bool Runtime::createResources(
        ID3D11Device* device,
        HRESULT(STDMETHODCALLTYPE* createPixelShader)(
            ID3D11Device*,
            const void*,
            SIZE_T,
            ID3D11ClassLinkage*,
            ID3D11PixelShader**)) noexcept
    {
        static_assert(kShaderContracts.size() == kShaderContractCount);
        static_assert(kSkyShaderContracts.size() == kSkyShaderContractCount);
        static_assert(kDistantTreeShaderContractCount == 1);
        static_assert(
            kParticleShaderContracts.size() == kParticleShaderContractCount);
        static_assert(
            kWaterShaderContracts.size() == kWaterShaderContractCount);
        static_assert(kVLSCompositeShaderContractCount == 1);
        static_assert(
            kEffectShaderContracts.size() == kEffectShaderContractCount);
        static_assert(kSurfaceClassContracts.size() == kShaderContractCount);
        static_assert(
            kSurfaceClassMaterialContracts.size() == kShaderContractCount);
        static_assert(
            kGrassVertexShaderIdentities.size() ==
            kGrassVertexShaderIdentityCount);
        static_assert(
            kSpecializedSurfaceClassContracts.size() ==
            kSpecializedSurfaceClassContractCount);
        static_assert(
            kAuthoredPbrSurfaceClassContracts.size() ==
            kShaderContractCount);
        static_assert(
            kAuthoredPbrSpecializedSurfaceClassContracts.size() ==
            kSpecializedSurfaceClassContractCount);
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kShaderContracts.size()>
            replacements{};
        for (std::size_t index = 0; index < kShaderContracts.size(); ++index) {
            const auto& contract = kShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Linear Lighting embedded replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                replacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Linear Lighting replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSurfaceClassContracts.size()>
            surfaceClassReplacements{};
        for (std::size_t index = 0;
             index < kSurfaceClassContracts.size();
             ++index) {
            const auto& contract = kSurfaceClassContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Surface Classification class-zero material replacement {} is missing or invalid.",
                    index);
                return false;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                surfaceClassReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Surface Classification class-zero material replacement {} CreatePixelShader failed (HRESULT 0x{:08X}).",
                    index,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSpecializedSurfaceClassContracts.size()>
            specializedSurfaceClassReplacements{};
        for (std::size_t index = 0;
             index < kSpecializedSurfaceClassContracts.size();
             ++index) {
            const auto& contract = kSpecializedSurfaceClassContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Surface Classification specialized replacement {} (class {}) is missing or invalid.",
                    index,
                    contract.classCode);
                return false;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                specializedSurfaceClassReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Surface Classification specialized replacement {} (class {}) CreatePixelShader failed (HRESULT 0x{:08X}).",
                    index,
                    contract.classCode,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kAuthoredPbrSurfaceClassContracts.size()>
            authoredPbrSurfaceClassReplacements{};
        for (std::size_t index = 0;
             index < kAuthoredPbrSurfaceClassContracts.size();
             ++index) {
            const auto& contract =
                kAuthoredPbrSurfaceClassContracts[index];
            if (contract.resourceId == 0) {
                continue;
            }
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Authored PBR class-zero material replacement {} is missing or invalid.",
                    index);
                return false;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                authoredPbrSurfaceClassReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Authored PBR class-zero material replacement {} CreatePixelShader failed (HRESULT 0x{:08X}).",
                    index,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kAuthoredPbrSpecializedSurfaceClassContracts.size()>
            authoredPbrSpecializedSurfaceClassReplacements{};
        for (std::size_t index = 0;
             index < kAuthoredPbrSpecializedSurfaceClassContracts.size();
             ++index) {
            const auto& contract =
                kAuthoredPbrSpecializedSurfaceClassContracts[index];
            if (contract.resourceId == 0) {
                continue;
            }
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Authored PBR specialized material replacement {} (class {}) is missing or invalid.",
                    index,
                    contract.classCode);
                return false;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                authoredPbrSpecializedSurfaceClassReplacements[index]
                    .GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Authored PBR specialized material replacement {} (class {}) CreatePixelShader failed (HRESULT 0x{:08X}).",
                    index,
                    contract.classCode,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kComplexParallaxShaders.size()>
            complexParallaxReplacements{};
        auto complexParallaxReady = true;
        for (std::size_t index = 0;
             index < kComplexParallaxShaders.size();
             ++index) {
            const auto& embedded = kComplexParallaxShaders[index];
            if (!embedded.data || embedded.size < 20 ||
                std::memcmp(embedded.data, "DXBC", 4) != 0) {
                logging::error(
                    "Complex Parallax generated replacement {} is missing or invalid; parallax remains fail-closed.",
                    index);
                complexParallaxReady = false;
                break;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                complexParallaxReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Complex Parallax replacement {} CreatePixelShader failed (HRESULT 0x{:08X}); parallax remains fail-closed.",
                    index,
                    static_cast<std::uint32_t>(result));
                complexParallaxReady = false;
                break;
            }
        }
        if (!complexParallaxReady) {
            for (auto& replacement : complexParallaxReplacements) {
                replacement.Reset();
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSurfaceClassComplexParallaxShaders.size()>
            surfaceClassComplexParallaxReplacements{};
        auto surfaceClassComplexParallaxReady = true;
        for (std::size_t index = 0;
             index < kSurfaceClassComplexParallaxShaders.size();
             ++index) {
            const auto& embedded = kSurfaceClassComplexParallaxShaders[index];
            if (!embedded.data || embedded.size < 20 ||
                std::memcmp(embedded.data, "DXBC", 4) != 0) {
                surfaceClassComplexParallaxReady = false;
                break;
            }
            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                surfaceClassComplexParallaxReplacements[index]
                    .GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Surface Classification complex-parallax replacement {} CreatePixelShader failed (HRESULT 0x{:08X}).",
                    index,
                    static_cast<std::uint32_t>(result));
                surfaceClassComplexParallaxReady = false;
                break;
            }
        }
        if (!surfaceClassComplexParallaxReady) {
            logging::error(
                "Surface Classification complex-parallax variants are incomplete; classification remains fail-closed while Complex Parallax is active.");
            for (auto& replacement :
                 surfaceClassComplexParallaxReplacements) {
                replacement.Reset();
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kSkyShaderContracts.size()>
            skyReplacements{};
        for (std::size_t index = 0;
             index < kSkyShaderContracts.size();
             ++index) {
            const auto& contract = kSkyShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Linear Lighting embedded Sky replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto result = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                skyReplacements[index].GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "Linear Lighting Sky replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kVLSCompositeShaderContractCount>
            vlsCompositeReplacements{};
        const auto vlsCompositeEmbedded =
            loadEmbeddedShader(kVLSCompositeShaderContract.resourceId);
        if (!vlsCompositeEmbedded.data || vlsCompositeEmbedded.size == 0) {
            logging::error(
                "Linear Lighting embedded VLS composite replacement '{}' is missing or invalid.",
                kVLSCompositeShaderContract.name);
            return false;
        }
        const auto vlsCompositeResult = createPixelShader(
            device,
            vlsCompositeEmbedded.data,
            vlsCompositeEmbedded.size,
            nullptr,
            vlsCompositeReplacements[0].GetAddressOf());
        if (FAILED(vlsCompositeResult)) {
            logging::error(
                "Linear Lighting VLS composite replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                kVLSCompositeShaderContract.name,
                static_cast<std::uint32_t>(vlsCompositeResult));
            return false;
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDistantTreeShaderContractCount>
            distantTreeReplacements{};
        const auto distantTreeEmbedded =
            loadEmbeddedShader(kDistantTreeShaderContract.resourceId);
        if (!distantTreeEmbedded.data || distantTreeEmbedded.size == 0) {
            logging::error(
                "Linear Lighting embedded DistantTree replacement '{}' is missing or invalid.",
                kDistantTreeShaderContract.name);
            return false;
        }
        const auto distantTreeResult = createPixelShader(
            device,
            distantTreeEmbedded.data,
            distantTreeEmbedded.size,
            nullptr,
            distantTreeReplacements[0].GetAddressOf());
        if (FAILED(distantTreeResult)) {
            logging::error(
                "Linear Lighting DistantTree replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                kDistantTreeShaderContract.name,
                static_cast<std::uint32_t>(distantTreeResult));
            return false;
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kParticleShaderContracts.size()>
            particleReplacements{};
        for (std::size_t index = 0;
             index < kParticleShaderContracts.size();
             ++index) {
            const auto& contract = kParticleShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Linear Lighting embedded Particle replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto particleResult = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                particleReplacements[index].GetAddressOf());
            if (FAILED(particleResult)) {
                logging::error(
                    "Linear Lighting Particle replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(particleResult));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kWaterShaderContracts.size()>
            waterReplacements{};
        for (std::size_t index = 0;
             index < kWaterShaderContracts.size();
             ++index) {
            const auto& contract = kWaterShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Linear Lighting embedded Water replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto waterResult = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                waterReplacements[index].GetAddressOf());
            if (FAILED(waterResult)) {
                logging::error(
                    "Linear Lighting Water replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(waterResult));
                return false;
            }
        }

        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kEffectShaderContracts.size()>
            effectReplacements{};
        for (std::size_t index = 0;
             index < kEffectShaderContracts.size();
             ++index) {
            const auto& contract = kEffectShaderContracts[index];
            const auto embedded = loadEmbeddedShader(contract.resourceId);
            if (!embedded.data || embedded.size == 0) {
                logging::error(
                    "Linear Lighting embedded Effect replacement '{}' is missing or invalid.",
                    contract.name);
                return false;
            }

            const auto effectResult = createPixelShader(
                device,
                embedded.data,
                embedded.size,
                nullptr,
                effectReplacements[index].GetAddressOf());
            if (FAILED(effectResult)) {
                logging::error(
                    "Linear Lighting Effect replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(effectResult));
                return false;
            }
        }

        const auto safeSettings = sanitize(settings_);
        const auto producerState = dFTiledPointLightProducerFrameState();
        const auto frameData = makeFrameData(
            safeSettings,
            true,
            false,
            1.0f,
            producerState.gamma,
            complexParallaxSettings_);
        const GeometryData geometryData{};
        const D3D11_SUBRESOURCE_DATA frameInitial{ &frameData, 0, 0 };
        const D3D11_SUBRESOURCE_DATA geometryInitial{ &geometryData, 0, 0 };

        auto frameDescription = makeConstantBufferDescription(sizeof(FrameData));
        Microsoft::WRL::ComPtr<ID3D11Buffer> frameBuffer;
        auto result = device->CreateBuffer(
            &frameDescription,
            &frameInitial,
            frameBuffer.GetAddressOf());
        if (FAILED(result)) {
            logging::error(
                "Linear Lighting frame-buffer creation failed (HRESULT 0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return false;
        }

        auto geometryDescription = makeConstantBufferDescription(sizeof(GeometryData));
        Microsoft::WRL::ComPtr<ID3D11Buffer> geometryBuffer;
        result = device->CreateBuffer(
            &geometryDescription,
            &geometryInitial,
            geometryBuffer.GetAddressOf());
        if (FAILED(result)) {
            logging::error(
                "Linear Lighting geometry-buffer creation failed (HRESULT 0x{:08X}).",
                static_cast<std::uint32_t>(result));
            return false;
        }

        D3D11_BUFFER_DESC complexEnvironmentDescription{};
        complexEnvironmentDescription.ByteWidth = 16;
        complexEnvironmentDescription.Usage = D3D11_USAGE_IMMUTABLE;
        complexEnvironmentDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constexpr std::array<std::uint32_t, 4>
            complexEnvironmentDisabled{};
        constexpr std::array<std::uint32_t, 4>
            complexEnvironmentEnabled{ 1u, 0u, 0u, 0u };
        const D3D11_SUBRESOURCE_DATA complexEnvironmentDisabledData{
            complexEnvironmentDisabled.data(), 0, 0
        };
        const D3D11_SUBRESOURCE_DATA complexEnvironmentEnabledData{
            complexEnvironmentEnabled.data(), 0, 0
        };
        Microsoft::WRL::ComPtr<ID3D11Buffer>
            complexEnvironmentDisabledBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer>
            complexEnvironmentEnabledBuffer;
        if (FAILED(device->CreateBuffer(
                &complexEnvironmentDescription,
                &complexEnvironmentDisabledData,
                complexEnvironmentDisabledBuffer.GetAddressOf())) ||
            FAILED(device->CreateBuffer(
                &complexEnvironmentDescription,
                &complexEnvironmentEnabledData,
                complexEnvironmentEnabledBuffer.GetAddressOf())) ||
            !complexEnvironmentDisabledBuffer ||
            !complexEnvironmentEnabledBuffer) {
            logging::error(
                "Complex Environment immutable b11 enable/disable buffers could not be created.");
            return false;
        }

        settings_ = safeSettings;
        replacementShaders_ = std::move(replacements);
        surfaceClassReplacementShaders_ =
            std::move(surfaceClassReplacements);
        specializedSurfaceClassReplacementShaders_ =
            std::move(specializedSurfaceClassReplacements);
        authoredPbrSurfaceClassReplacementShaders_ =
            std::move(authoredPbrSurfaceClassReplacements);
        authoredPbrSpecializedSurfaceClassReplacementShaders_ =
            std::move(authoredPbrSpecializedSurfaceClassReplacements);
        complexParallaxReplacementShaders_ =
            std::move(complexParallaxReplacements);
        surfaceClassComplexParallaxReplacementShaders_ =
            std::move(surfaceClassComplexParallaxReplacements);
        skyReplacementShaders_ = std::move(skyReplacements);
        distantTreeReplacementShaders_ =
            std::move(distantTreeReplacements);
        particleReplacementShaders_ = std::move(particleReplacements);
        waterReplacementShaders_ = std::move(waterReplacements);
        vlsCompositeReplacementShaders_ = std::move(vlsCompositeReplacements);
        effectReplacementShaders_ = std::move(effectReplacements);
        frameBuffer_ = std::move(frameBuffer);
        geometryBuffer_ = std::move(geometryBuffer);
        complexEnvironmentDisabledBuffer_ =
            std::move(complexEnvironmentDisabledBuffer);
        complexEnvironmentEnabledBuffer_ =
            std::move(complexEnvironmentEnabledBuffer);
        publishedLightProducerRevision_ = producerState.revision;
        enabled_.store(settings_.enabled, std::memory_order_release);
        complexParallaxResourcesReady_.store(
            complexParallaxReady,
            std::memory_order_release);
        complexParallaxEnabled_.store(
            complexParallaxSettings_.parallaxEnabled,
            std::memory_order_release);
        complexEnvironmentEnabled_.store(
            complexParallaxSettings_.environmentResponseEnabled,
            std::memory_order_release);
        complexParallaxQuality_.store(
            complexParallaxSettings_.parallaxQuality,
            std::memory_order_release);
        frameDataUploads_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::createDFLightAmbientReplacement(
        std::span<const std::byte> originalBytecode,
        std::size_t contractIndex,
        float ambientGamma,
        Microsoft::WRL::ComPtr<ID3D11PixelShader>& replacement) noexcept
    {
        if (contractIndex >= kDFLightAmbientContracts.size() ||
            !device_ || !createPixelShader_ || originalBytecode.empty()) {
            return false;
        }

        const auto& contract = kDFLightAmbientContracts[contractIndex];
        if (!matchesDxbcIdentity(
                originalBytecode.data(),
                originalBytecode.size(),
                contract.original.size,
                contract.original.checksum)) {
            return false;
        }

        try {
            const auto& skylightingContract =
                csah_skylighting_ambient_contracts[contractIndex];
            const auto* replacementBegin = reinterpret_cast<const std::byte*>(
                skylightingContract.replacementBytecode);
            std::vector<std::byte> patched(
                replacementBegin,
                replacementBegin +
                    skylightingContract.replacementBytecodeLength);
            if (!patchDFLightAmbientGamma(
                    patched,
                    skylightingContract.gammaOffsets,
                    ambientGamma)) {
                logging::error(
                    "DFLight ambient/Skylighting replacement '{}' failed its transformed six-offset gamma identity gate.",
                    contract.name);
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D11PixelShader> candidate;
            const auto result = createPixelShader_(
                device_.Get(),
                patched.data(),
                patched.size(),
                nullptr,
                candidate.GetAddressOf());
            if (FAILED(result)) {
                logging::error(
                    "DFLight ambient replacement '{}' CreatePixelShader failed (HRESULT 0x{:08X}).",
                    contract.name,
                    static_cast<std::uint32_t>(result));
                return false;
            }
            replacement = std::move(candidate);
            dFLightAmbientReplacementBuilds_.fetch_add(
                1, std::memory_order_relaxed);
            return true;
        } catch (const std::exception& error) {
            logging::error(
                "DFLight ambient replacement '{}' failed while copying bytecode: {}",
                contract.name,
                error.what());
        } catch (...) {
            logging::error(
                "DFLight ambient replacement '{}' failed while copying bytecode with an unknown exception.",
                contract.name);
        }
        return false;
    }

    bool Runtime::rebuildDFLightAmbientReplacements(
        float ambientGamma) noexcept
    {
        std::scoped_lock lock(shaderRegistryMutex_);
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kDFLightAmbientShaderContractCount>
            replacements{};
        std::uint64_t rebuiltMask{};
        for (std::size_t index = 0;
             index < dFLightAmbientOriginalBytecode_.size();
             ++index) {
            const auto& original = dFLightAmbientOriginalBytecode_[index];
            if (original.empty()) {
                continue;
            }
            if (!createDFLightAmbientReplacement(
                    original,
                    index,
                    ambientGamma,
                    replacements[index])) {
                dFLightAmbientReplacementFailures_.fetch_add(
                    1, std::memory_order_relaxed);
                return false;
            }
            rebuiltMask |= 1ull << index;
        }

        for (std::size_t index = 0; index < replacements.size(); ++index) {
            if (replacements[index]) {
                dFLightAmbientReplacementShaders_[index] =
                    std::move(replacements[index]);
            }
        }
        dFLightAmbientGammaBits_.store(
            std::bit_cast<std::uint32_t>(ambientGamma),
            std::memory_order_release);
        readyDFLightAmbientContractMask_.fetch_or(
            rebuiltMask, std::memory_order_release);
        dFLightAmbientGammaRebuilds_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool Runtime::registerShaderBinding(
        ID3D11PixelShader* shader,
        ReplacementShaderBinding binding) noexcept
    {
        if (shaderBindingLookup_.insert(shader, encodeShaderBinding(binding))) {
            return true;
        }

        shaderBindingLookupFailures_.fetch_add(1, std::memory_order_relaxed);
        if (!shaderBindingLookupCapacityWarningLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::error(
                "Linear Lighting fixed shader-binding lookup rejected an original shader; the affected instance remains vanilla.");
        }
        return false;
    }

    void Runtime::onVertexShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11VertexShader* shader) noexcept
    {
        if (!shader) {
            return;
        }

        std::size_t identityIndex = kGrassVertexShaderIdentities.size();
        for (std::size_t index = 0;
             index < kGrassVertexShaderIdentities.size();
             ++index) {
            const auto& identity = kGrassVertexShaderIdentities[index];
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                identityIndex = index;
                break;
            }
        }
        if (identityIndex == kGrassVertexShaderIdentities.size()) {
            return;
        }

        matchingGrassVertexShaderIdentityMask_.fetch_or(
            static_cast<std::uint8_t>(1u << identityIndex),
            std::memory_order_relaxed);
        matchingGrassVertexShadersCreated_.fetch_add(
            1,
            std::memory_order_relaxed);
        std::scoped_lock lock(shaderRegistryMutex_);
        for (auto& owner : grassVertexShaderOwners_) {
            if (owner.Get() == shader) {
                return;
            }
            if (!owner) {
                owner = shader;
                if (!grassVertexShaderLookup_.insert(shader, 1)) {
                    owner.Reset();
                    break;
                }
                const auto tracked = trackedGrassVertexShaders_.fetch_add(
                    1,
                    std::memory_order_relaxed) + 1;
                if (tracked == 1) {
                    logging::info(
                        "Surface classification tracked its first exact grass-only DFPrepass vertex shader (identity {}/{}).",
                        identityIndex + 1,
                        kGrassVertexShaderIdentities.size());
                }
                return;
            }
        }
        if (!grassVertexShaderCapacityWarningLogged_.exchange(
                true,
                std::memory_order_relaxed)) {
            logging::error(
                "Surface classification exhausted its fixed grass vertex-shader registry; additional instances remain ordinary-classified.");
        }
    }

    bool Runtime::isGrassVertexShader(
        ID3D11VertexShader* shader) const noexcept
    {
        return grassVertexShaderLookup_.find(shader) == 1;
    }

    void Runtime::onPixelShaderCreated(
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11PixelShader* shader) noexcept
    {
        if (!shader) {
            return;
        }

        std::size_t contractIndex = kShaderContracts.size();
        for (std::size_t index = 0; index < kShaderContracts.size(); ++index) {
            const auto& identity = kShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                contractIndex = index;
                break;
            }
        }
        if (contractIndex != kShaderContracts.size()) {
            matchingShaderContractMask_.set(
                contractIndex,
                std::memory_order_relaxed);
            matchingShadersCreated_.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalShaderOwners_[contractIndex];
            auto& slots = originalShaders_[contractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::material,
                                static_cast<std::uint32_t>(contractIndex + 1),
                                ReplacementPixelConstants_Frame |
                                    ReplacementPixelConstants_Geometry,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalShaders_.fetch_add(
                        1, std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalCapacityWarningLogged_[contractIndex].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kShaderContracts[contractIndex].name);
            }
            return;
        }

        std::size_t skyContractIndex = kSkyShaderContracts.size();
        for (std::size_t index = 0;
             index < kSkyShaderContracts.size();
             ++index) {
            const auto& identity = kSkyShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                skyContractIndex = index;
                break;
            }
        }
        if (skyContractIndex != kSkyShaderContracts.size()) {
            matchingSkyShaderContractMask_.fetch_or(
                static_cast<std::uint16_t>(1u << skyContractIndex),
                std::memory_order_relaxed);
            matchingSkyShadersCreated_.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalSkyShaderOwners_[skyContractIndex];
            auto& slots = originalSkyShaders_[skyContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::sky,
                                static_cast<std::uint32_t>(skyContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalSkyShaders_.fetch_add(
                        1, std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalSkyCapacityWarningLogged_[skyContractIndex].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Sky-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kSkyShaderContracts[skyContractIndex].name);
            }
            return;
        }

        if (matchesDxbcIdentity(
                bytecode,
                bytecodeLength,
                kDistantTreeShaderContract.original.size,
                kDistantTreeShaderContract.original.checksum)) {
            matchingDistantTreeShaderContractMask_.fetch_or(
                1u,
                std::memory_order_relaxed);
            matchingDistantTreeShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalDistantTreeShaderOwners_[0];
            auto& slots = originalDistantTreeShaders_[0];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::distantTree,
                                1u,
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalDistantTreeShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalDistantTreeCapacityWarningLogged_[0].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original DistantTree-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kDistantTreeShaderContract.name);
            }
            return;
        }

        std::size_t particleContractIndex = kParticleShaderContracts.size();
        for (std::size_t index = 0;
             index < kParticleShaderContracts.size();
             ++index) {
            const auto& identity = kParticleShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                particleContractIndex = index;
                break;
            }
        }
        if (particleContractIndex != kParticleShaderContracts.size()) {
            matchingParticleShaderContractMask_.fetch_or(
                static_cast<std::uint8_t>(1u << particleContractIndex),
                std::memory_order_relaxed);
            matchingParticleShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalParticleShaderOwners_[particleContractIndex];
            auto& slots = originalParticleShaders_[particleContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::particle,
                                static_cast<std::uint32_t>(
                                    particleContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalParticleShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalParticleCapacityWarningLogged_[particleContractIndex]
                     .exchange(true, std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Particle-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kParticleShaderContracts[particleContractIndex].name);
            }
            return;
        }

        std::size_t waterContractIndex = kWaterShaderContracts.size();
        for (std::size_t index = 0;
             index < kWaterShaderContracts.size();
             ++index) {
            const auto& identity = kWaterShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                waterContractIndex = index;
                break;
            }
        }
        if (waterContractIndex != kWaterShaderContracts.size()) {
            matchingWaterShaderContractMask_.fetch_or(
                1u << waterContractIndex,
                std::memory_order_relaxed);
            matchingWaterShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalWaterShaderOwners_[waterContractIndex];
            auto& slots = originalWaterShaders_[waterContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::water,
                                static_cast<std::uint32_t>(
                                    waterContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalWaterShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalWaterCapacityWarningLogged_[waterContractIndex]
                     .exchange(true, std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Water-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kWaterShaderContracts[waterContractIndex].name);
            }
            return;
        }

        if (matchesDxbcIdentity(
                bytecode,
                bytecodeLength,
                kVLSCompositeShaderContract.original.size,
                kVLSCompositeShaderContract.original.checksum)) {
            matchingVLSCompositeShaderContractMask_.fetch_or(
                1u,
                std::memory_order_relaxed);
            matchingVLSCompositeShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalVLSCompositeShaderOwners_[0];
            auto& slots = originalVLSCompositeShaders_[0];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::vlsComposite,
                                1u,
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalVLSCompositeShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalVLSCompositeCapacityWarningLogged_[0].exchange(
                    true,
                    std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original VLS composite-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kVLSCompositeShaderContract.name);
            }
            return;
        }

        std::size_t effectContractIndex = kEffectShaderContracts.size();
        for (std::size_t index = 0;
             index < kEffectShaderContracts.size();
             ++index) {
            const auto& identity = kEffectShaderContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                effectContractIndex = index;
                break;
            }
        }
        if (effectContractIndex != kEffectShaderContracts.size()) {
            matchingEffectShaderContractMask_.set(
                effectContractIndex,
                std::memory_order_relaxed);
            matchingEffectShadersCreated_.fetch_add(
                1,
                std::memory_order_relaxed);
            std::scoped_lock lock(shaderRegistryMutex_);
            auto& owners = originalEffectShaderOwners_[effectContractIndex];
            auto& slots = originalEffectShaders_[effectContractIndex];
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (slots[index].load(std::memory_order_relaxed) == shader) {
                    return;
                }
                if (!owners[index]) {
                    owners[index] = shader;
                    if (!registerShaderBinding(
                            shader,
                            {
                                ReplacementShaderFamily::effect,
                                static_cast<std::uint32_t>(
                                    effectContractIndex + 1),
                                ReplacementPixelConstants_Frame,
                            })) {
                        owners[index].Reset();
                        return;
                    }
                    slots[index].store(shader, std::memory_order_release);
                    trackedOriginalEffectShaders_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    return;
                }
            }
            if (!originalEffectCapacityWarningLogged_[effectContractIndex]
                     .exchange(true, std::memory_order_relaxed)) {
                logging::warn(
                    "Linear Lighting original Effect-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                    kEffectShaderContracts[effectContractIndex].name);
            }
            return;
        }

        std::size_t ambientContractIndex = kDFLightAmbientContracts.size();
        for (std::size_t index = 0;
             index < kDFLightAmbientContracts.size();
             ++index) {
            const auto& identity = kDFLightAmbientContracts[index].original;
            if (matchesDxbcIdentity(
                    bytecode,
                    bytecodeLength,
                    identity.size,
                    identity.checksum)) {
                ambientContractIndex = index;
                break;
            }
        }
        if (ambientContractIndex == kDFLightAmbientContracts.size()) {
            return;
        }

        matchingDFLightAmbientContractMask_.fetch_or(
            1ull << ambientContractIndex,
            std::memory_order_relaxed);
        matchingDFLightAmbientShaders_.fetch_add(
            1, std::memory_order_relaxed);
        std::scoped_lock lock(shaderRegistryMutex_);
        auto& owners =
            dFLightAmbientOriginalShaderOwners_[ambientContractIndex];
        auto& slots = dFLightAmbientOriginalShaders_[ambientContractIndex];
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index].load(std::memory_order_relaxed) == shader) {
                return;
            }
            if (!owners[index]) {
                if (dFLightAmbientOriginalBytecode_[ambientContractIndex].empty()) {
                    const auto* begin = static_cast<const std::byte*>(bytecode);
                    try {
                        dFLightAmbientOriginalBytecode_[ambientContractIndex]
                            .assign(begin, begin + bytecodeLength);
                    } catch (const std::exception& error) {
                        logging::error(
                            "DFLight ambient bytecode capture '{}' failed: {}",
                            kDFLightAmbientContracts[ambientContractIndex].name,
                            error.what());
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        return;
                    } catch (...) {
                        logging::error(
                            "DFLight ambient bytecode capture '{}' failed with an unknown exception.",
                            kDFLightAmbientContracts[ambientContractIndex].name);
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                        return;
                    }
                }
                if (!dFLightAmbientReplacementShaders_[ambientContractIndex]) {
                    if (!createDFLightAmbientReplacement(
                            dFLightAmbientOriginalBytecode_[ambientContractIndex],
                            ambientContractIndex,
                            std::bit_cast<float>(
                                dFLightAmbientGammaBits_.load(
                                    std::memory_order_acquire)),
                            dFLightAmbientReplacementShaders_[ambientContractIndex])) {
                        dFLightAmbientReplacementFailures_.fetch_add(
                            1, std::memory_order_relaxed);
                    } else {
                        readyDFLightAmbientContractMask_.fetch_or(
                            1ull << ambientContractIndex,
                            std::memory_order_release);
                    }
                }
                owners[index] = shader;
                if (!registerShaderBinding(
                        shader,
                        {
                            ReplacementShaderFamily::dFLightAmbient,
                            static_cast<std::uint32_t>(
                                ambientContractIndex + 1),
                            ReplacementPixelConstants_None,
                        })) {
                    owners[index].Reset();
                    return;
                }
                slots[index].store(shader, std::memory_order_release);
                trackedDFLightAmbientShaders_.fetch_add(
                    1, std::memory_order_relaxed);
                return;
            }
        }
        if (!dFLightAmbientCapacityWarningLogged_[ambientContractIndex].exchange(
                true,
                std::memory_order_relaxed)) {
            logging::warn(
                "DFLight ambient original-shader capacity for '{}' was exhausted; extra instances remain vanilla.",
                kDFLightAmbientContracts[ambientContractIndex].name);
        }
    }

    PixelShaderSelection Runtime::selectDFLightAmbientShader(
        ID3D11PixelShader* requested,
        std::size_t contractIndex) noexcept
    {
        if (contractIndex >= dFLightAmbientReplacementShaders_.size()) {
            return { requested, {}, false };
        }

        std::scoped_lock lock(shaderRegistryMutex_);
        auto* replacement =
            dFLightAmbientReplacementShaders_[contractIndex].Get();
        if (!replacement) {
            return { requested, {}, false };
        }
        replacement->AddRef();
        dFLightAmbientReplacementBinds_.fetch_add(
            1, std::memory_order_relaxed);
        return {
            replacement,
            {
                ReplacementShaderFamily::dFLightAmbient,
                static_cast<std::uint32_t>(contractIndex + 1),
                ReplacementPixelConstants_None,
            },
            true,
        };
    }

    PixelShaderSelection Runtime::selectPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        return selectPixelShaderImpl(context, requested, 0, false, false);
    }

    PixelShaderSelection Runtime::selectDFLightAmbientPixelShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        if (!requested || context != context_.Get() ||
            !gpuResourcesReady_.load(std::memory_order_acquire)) {
            return { requested, {} };
        }
        const auto binding = decodeShaderBinding(
            shaderBindingLookup_.find(requested));
        if (binding.family != ReplacementShaderFamily::dFLightAmbient ||
            binding.contractPlusOne == 0) {
            return { requested, {} };
        }
        return selectDFLightAmbientShader(
            requested,
            binding.contractPlusOne - 1);
    }

    PixelShaderSelection Runtime::selectPixelShaderForDFPrePassDescriptor(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested,
        std::uint32_t descriptor) noexcept
    {
        return selectPixelShaderImpl(
            context,
            requested,
            descriptor,
            true,
            false);
    }

    PixelShaderSelection Runtime::selectPixelShaderForGrassVertex(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested) noexcept
    {
        return selectPixelShaderImpl(context, requested, 0, false, true);
    }

    ID3D11PixelShader* Runtime::authoredPbrSurfaceShader(
        ReplacementShaderBinding binding,
        std::uint32_t surfaceClassCode,
        ID3D11PixelShader* currentShader) const noexcept
    {
        if (!currentShader ||
            binding.family != ReplacementShaderFamily::material ||
            binding.contractPlusOne == 0 ||
            binding.contractPlusOne > kShaderContractCount) {
            return nullptr;
        }
        const auto contractIndex = static_cast<std::size_t>(
            binding.contractPlusOne - 1);
        const auto& material =
            kSurfaceClassMaterialContracts[contractIndex];
        auto variantPlusOne = 0u;
        if (surfaceClassCode == static_cast<std::uint32_t>(
                surface_classification::SurfaceClassCode::grass)) {
            variantPlusOne = material.grassVariantIndexPlusOne;
        } else if (material.classCode == surfaceClassCode) {
            variantPlusOne = material.variantIndexPlusOne;
        }
        const auto specializedIndex = specializedSurfaceClassSlot(
            contractIndex,
            surfaceClassCode,
            variantPlusOne);
        if (specializedIndex <
                specializedSurfaceClassReplacementShaders_.size()) {
            if (currentShader !=
                specializedSurfaceClassReplacementShaders_[specializedIndex]
                    .Get()) {
                return nullptr;
            }
            return authoredPbrSpecializedSurfaceClassReplacementShaders_[
                       specializedIndex]
                .Get();
        }
        if (currentShader !=
            surfaceClassReplacementShaders_[contractIndex].Get()) {
            return nullptr;
        }
        return authoredPbrSurfaceClassReplacementShaders_[contractIndex]
            .Get();
    }

    void Runtime::observeDFPrePassDescriptor(
        std::uint32_t descriptor) noexcept
    {
        auto& classification = surface_classification::Runtime::get();
        if (!classification.required()) {
            return;
        }
        const auto* contract = surfaceClassDescriptorContract(descriptor);
        if (!contract) {
            classification.recordDescriptorObservationMiss(descriptor);
            return;
        }
        classification.recordDescriptorObservation(
            contract->classCode,
            descriptor);
    }

    PixelShaderSelection Runtime::selectPixelShaderImpl(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* requested,
        std::uint32_t descriptor,
        bool descriptorActive,
        bool grassVertexActive) noexcept
    {
        shaderSelectionCalls_.fetch_add(1, std::memory_order_relaxed);
        const auto isCapturedContext = context == context_.Get();
        if (!isCapturedContext) {
            rejectedShaderContexts_.fetch_add(1, std::memory_order_relaxed);
            return { requested, {} };
        }

        if (currentlyRequestedShader_.Get() != requested) {
            currentlyRequestedShader_ = requested;
        }
        applyQueuedSettingsForRenderBoundary();

        const auto linearLightingEnabled =
            enabled_.load(std::memory_order_acquire);
        const auto surfaceClassificationActive =
            linearLightingEnabled &&
            surface_classification::Runtime::get().required();
        const auto complexParallaxActive =
            complexParallaxEnabled_.load(std::memory_order_acquire) &&
            complexParallaxResourcesReady_.load(std::memory_order_acquire);
        const auto complexEnvironmentRequested =
            complexEnvironmentEnabled_.load(std::memory_order_acquire) ||
            pbrMaterialsEnabled_.load(std::memory_order_acquire);
        const auto complexEnvironmentMayRun =
            linearLightingEnabled && complexEnvironmentRequested;
        if (!complexEnvironmentMayRun) {
            complexEnvironmentConsumerReady_.store(
                false,
                std::memory_order_release);
        }
        if (!requested ||
            (!linearLightingEnabled && !complexParallaxActive && !lod_blending::Runtime::get().requested()) ||
            !gpuResourcesReady_.load(std::memory_order_acquire)) {
            inactiveShaderSelections_.fetch_add(1, std::memory_order_relaxed);
            return { requested, {} };
        }

        auto binding = decodeShaderBinding(
            shaderBindingLookup_.find(requested));
        if (binding.family == ReplacementShaderFamily::material) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > replacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }

            const auto contractIndex =
                static_cast<std::size_t>(binding.contractPlusOne - 1);
            const auto parallaxSlot =
                complex_materials::landscapeParallaxSlot(contractIndex);
            const auto useComplexParallax = complexParallaxActive &&
                parallaxSlot < complexParallaxReplacementShaders_.size();
            const auto* surfaceClassDescriptor =
                surfaceClassificationActive && descriptorActive ?
                surfaceClassDescriptorContract(
                    descriptor, contractIndex) :
                nullptr;
            const auto& surfaceClassMaterial =
                kSurfaceClassMaterialContracts[contractIndex];
            const auto materialClassUnambiguous =
                surfaceClassMaterial.classCode !=
                kAmbiguousSurfaceClassCode;
            const auto grassVertexClassAvailable =
                surfaceClassificationActive && grassVertexActive &&
                surfaceClassMaterial.grassVariantIndexPlusOne != 0;
            const auto selectedSurfaceClassCode =
                grassVertexClassAvailable ?
                static_cast<std::uint32_t>(
                    surface_classification::SurfaceClassCode::grass) :
                (surfaceClassDescriptor ?
                        surfaceClassDescriptor->classCode :
                        (materialClassUnambiguous ?
                                surfaceClassMaterial.classCode :
                                static_cast<std::uint32_t>(
                                    surface_classification::SurfaceClassCode::
                                        ordinary)));
            const auto selectedSurfaceClassVariantPlusOne =
                grassVertexClassAvailable ?
                surfaceClassMaterial.grassVariantIndexPlusOne :
                (surfaceClassDescriptor ?
                        surfaceClassDescriptor->variantIndexPlusOne :
                        (materialClassUnambiguous ?
                                surfaceClassMaterial.variantIndexPlusOne :
                                0u));
            const auto specializedSurfaceClassIndex =
                specializedSurfaceClassSlot(
                    contractIndex,
                    selectedSurfaceClassCode,
                    selectedSurfaceClassVariantPlusOne);
            const auto complexEnvironmentProducer =
                complex_materials::
                    kComplexEnvironmentProducerByLinearContract[
                        contractIndex];
            const auto complexEnvironmentCandidate =
                complexEnvironmentProducer != 0;
            const auto contractComplexEnvironmentRequested =
                complexEnvironmentCandidate &&
                complexEnvironmentMayRun;
            const auto complexEnvironmentConsumerReady =
                contractComplexEnvironmentRequested &&
                ibl::Runtime::get().complexMaterialConsumptionReady();
            complexEnvironmentConsumerReady_.store(
                complexEnvironmentConsumerReady,
                std::memory_order_release);

            auto useComplexEnvironment = false;
            if (complexEnvironmentCandidate) {
                binding.constantFlags = static_cast<std::uint8_t>(
                    binding.constantFlags |
                    ReplacementPixelConstants_ComplexEnvironment);
                const auto* alias = descriptorActive ?
                    complex_materials::findComplexMaterialProducerAlias(
                        descriptor) :
                    nullptr;
                useComplexEnvironment =
                    complexEnvironmentConsumerReady && alias &&
                    alias->family == complex_materials::
                        ComplexMaterialProducerFamily::kEnvironmentMap &&
                    alias->contractPlusOne == complexEnvironmentProducer;
                if (useComplexEnvironment) {
                    binding.constantFlags = static_cast<std::uint8_t>(
                        binding.constantFlags |
                        ReplacementPixelConstants_ComplexEnvironmentEnabled);
                } else if (complexEnvironmentConsumerReady) {
                    complexEnvironmentDescriptorRejects_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
            }

            if (!linearLightingEnabled && !useComplexParallax &&
                !useComplexEnvironment && !lod_blending::Runtime::get().requested()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            if (linearLightingEnabled &&
                !geometryProviderReady_.load(std::memory_order_acquire)) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }

            ID3D11PixelShader* replacement{};
            if (surfaceClassificationActive) {
                if (useComplexParallax) {
                    replacement =
                        surfaceClassComplexParallaxReplacementShaders_[
                            parallaxSlot]
                            .Get();
                } else if (
                    specializedSurfaceClassIndex <
                    specializedSurfaceClassReplacementShaders_.size()) {
                    replacement = specializedSurfaceClassReplacementShaders_[
                        specializedSurfaceClassIndex]
                                      .Get();
                } else {
                    replacement =
                        surfaceClassReplacementShaders_[contractIndex].Get();
                }
            } else {
                replacement = useComplexParallax ?
                    complexParallaxReplacementShaders_[parallaxSlot].Get() :
                    replacementShaders_[contractIndex].Get();
            }
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }

            if (surfaceClassificationActive) {
                auto& classification =
                    surface_classification::Runtime::get();
                if (descriptorActive && !surfaceClassDescriptor) {
                    classification.recordDescriptorContractMiss(
                        descriptor);
                }
                if (grassVertexClassAvailable) {
                    classification.recordProducerSelection(
                        selectedSurfaceClassCode,
                        static_cast<std::uint32_t>(contractIndex),
                        surface_classification::ProducerEvidence::
                            grassVertexShaderIdentity,
                        0);
                    grassVertexClassSelections_.fetch_add(
                        1,
                        std::memory_order_relaxed);
                } else if (surfaceClassDescriptor) {
                    classification.recordProducerSelection(
                        surfaceClassDescriptor->classCode,
                        static_cast<std::uint32_t>(contractIndex),
                        surface_classification::ProducerEvidence::descriptor,
                        descriptor);
                } else if (materialClassUnambiguous) {
                    classification.recordProducerSelection(
                        surfaceClassMaterial.classCode,
                        static_cast<std::uint32_t>(contractIndex),
                        surface_classification::ProducerEvidence::
                            materialIdentity,
                        0);
                }
            }

            auto noContractObserved = 0u;
            firstReplacementContractPlusOne_.compare_exchange_strong(
                noContractObserved,
                binding.contractPlusOne,
                std::memory_order_release,
                std::memory_order_relaxed);
            replacementBinds_.fetch_add(1, std::memory_order_relaxed);
            if (useComplexParallax) {
                complexParallaxReplacementBinds_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            if (useComplexEnvironment) {
                complexEnvironmentReplacementBinds_.fetch_add(
                    1,
                    std::memory_order_relaxed);
            }
            return {
                replacement,
                binding,
                false,
                selectedSurfaceClassCode,
            };
        }

        if (!linearLightingEnabled) {
            inactiveShaderSelections_.fetch_add(1, std::memory_order_relaxed);
            return { requested, {} };
        }

        if (binding.family == ReplacementShaderFamily::sky) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > skyReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                skyReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            skyReplacementBinds_.fetch_add(1, std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::distantTree) {
            if (binding.contractPlusOne != 1) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement = distantTreeReplacementShaders_[0].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            distantTreeReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::particle) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > particleReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                particleReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            particleReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::water) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > waterReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                waterReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1, std::memory_order_relaxed);
                return { requested, {} };
            }
            waterReplacementBinds_.fetch_add(1, std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::vlsComposite) {
            if (binding.contractPlusOne != 1) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement = vlsCompositeReplacementShaders_[0].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            vlsCompositeReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::effect) {
            if (binding.contractPlusOne == 0 ||
                binding.contractPlusOne > effectReplacementShaders_.size()) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            auto* replacement =
                effectReplacementShaders_[binding.contractPlusOne - 1].Get();
            if (!replacement) {
                inactiveShaderSelections_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return { requested, {} };
            }
            effectReplacementBinds_.fetch_add(
                1,
                std::memory_order_relaxed);
            return { replacement, binding, false };
        }

        if (binding.family == ReplacementShaderFamily::dFLightAmbient &&
            binding.contractPlusOne > 0) {
            return selectDFLightAmbientShader(
                requested,
                binding.contractPlusOne - 1);
        }
        unmatchedShaderSelections_.fetch_add(1, std::memory_order_relaxed);
        return { requested, {}, false };
    }

    bool Runtime::replacementFeaturesEnabled() noexcept
    {
        applyQueuedSettingsForRenderBoundary();
        return lod_blending::Runtime::get().requested() || enabled_.load(std::memory_order_acquire) ||
            pbrMaterialsEnabled_.load(std::memory_order_acquire) ||
            (complexParallaxEnabled_.load(std::memory_order_acquire) &&
                complexParallaxResourcesReady_.load(
                    std::memory_order_acquire));
    }

    void Runtime::setPbrMaterialsEnabled(bool enabled) noexcept
    {
        pbrMaterialsEnabled_.store(enabled, std::memory_order_release);
        render::setDFPrePassComplexEnvironmentEnabled(
            enabled || complexEnvironmentEnabled_.load(
                std::memory_order_acquire));
    }

    ScopedReplacementPixelConstants Runtime::scopeReplacementPixelConstants(
        ID3D11DeviceContext* context,
        ReplacementShaderBinding binding) noexcept
    {
        const auto frameAndGeometry = static_cast<std::uint8_t>(
            ReplacementPixelConstants_Frame |
            ReplacementPixelConstants_Geometry);
        const auto complexEnvironmentFlags = static_cast<std::uint8_t>(
            ReplacementPixelConstants_ComplexEnvironment |
            ReplacementPixelConstants_ComplexEnvironmentEnabled);
        const auto materialFlags = static_cast<std::uint8_t>(
            binding.constantFlags & ~complexEnvironmentFlags);
        const auto ownsComplexEnvironment =
            (binding.constantFlags &
                ReplacementPixelConstants_ComplexEnvironment) != 0;
        const auto enablesComplexEnvironment =
            (binding.constantFlags &
                ReplacementPixelConstants_ComplexEnvironmentEnabled) != 0;
        const auto validMaterial =
            binding.family == ReplacementShaderFamily::material &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= replacementShaders_.size() &&
            materialFlags == frameAndGeometry &&
            (!enablesComplexEnvironment || ownsComplexEnvironment);
        const auto validSky =
            binding.family == ReplacementShaderFamily::sky &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= skyReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validDistantTree =
            binding.family == ReplacementShaderFamily::distantTree &&
            binding.contractPlusOne == 1 &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validParticle =
            binding.family == ReplacementShaderFamily::particle &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= particleReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validWater =
            binding.family == ReplacementShaderFamily::water &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= waterReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validVLSComposite =
            binding.family == ReplacementShaderFamily::vlsComposite &&
            binding.contractPlusOne == 1 &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        const auto validEffect =
            binding.family == ReplacementShaderFamily::effect &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= effectReplacementShaders_.size() &&
            binding.constantFlags == ReplacementPixelConstants_Frame;
        if (!context || context != context_.Get() ||
            (!validMaterial && !validSky && !validDistantTree &&
                !validParticle && !validWater && !validVLSComposite &&
                !validEffect) ||
            !frameBuffer_ ||
            (validMaterial && !geometryBuffer_) ||
            (ownsComplexEnvironment &&
                (!complexEnvironmentDisabledBuffer_ ||
                    !complexEnvironmentEnabledBuffer_))) {
            return ScopedReplacementPixelConstants{};
        }

        synchronizeLightProducerFrameState();
        replacementConstantScopes_.fetch_add(1, std::memory_order_relaxed);
        return ScopedReplacementPixelConstants(
            context,
            frameBuffer_.Get(),
            validMaterial ? geometryBuffer_.Get() : nullptr,
            ownsComplexEnvironment ?
                (enablesComplexEnvironment ?
                        complexEnvironmentEnabledBuffer_.Get() :
                        complexEnvironmentDisabledBuffer_.Get()) :
                nullptr,
            binding.constantFlags,
            &replacementConstantRestores_);
    }

    std::uint32_t Runtime::inspectReplacementPipelineState(
        ID3D11DeviceContext* context,
        ReplacementShaderBinding binding,
        std::uint32_t surfaceClassCode) const noexcept
    {
        ID3D11PixelShader* expectedShader{};
        if (binding.family == ReplacementShaderFamily::material &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= replacementShaders_.size()) {
            const auto contractIndex =
                static_cast<std::size_t>(binding.contractPlusOne - 1);
            const auto parallaxSlot =
                complex_materials::landscapeParallaxSlot(contractIndex);
            const auto useComplexParallax =
                complexParallaxEnabled_.load(std::memory_order_acquire) &&
                complexParallaxResourcesReady_.load(
                    std::memory_order_acquire) &&
                parallaxSlot < complexParallaxReplacementShaders_.size();
            const auto surfaceClassificationActive =
                enabled_.load(std::memory_order_acquire) &&
                surface_classification::Runtime::get().required();
            const auto specializedSurfaceClass = std::find_if(
                kSpecializedSurfaceClassContracts.begin(),
                kSpecializedSurfaceClassContracts.end(),
                [contractIndex, surfaceClassCode](const auto& contract) {
                    return contract.contractIndex == contractIndex &&
                        contract.classCode == surfaceClassCode;
                });
            const auto specializedSurfaceClassIndex =
                static_cast<std::size_t>(std::distance(
                    kSpecializedSurfaceClassContracts.begin(),
                    specializedSurfaceClass));
            if (surfaceClassificationActive) {
                expectedShader = useComplexParallax ?
                    surfaceClassComplexParallaxReplacementShaders_[
                        parallaxSlot]
                        .Get() :
                    (specializedSurfaceClassIndex <
                            specializedSurfaceClassReplacementShaders_.size() ?
                            specializedSurfaceClassReplacementShaders_[
                                specializedSurfaceClassIndex]
                                .Get() :
                            surfaceClassReplacementShaders_[contractIndex]
                                .Get());
            } else {
                expectedShader = useComplexParallax ?
                    complexParallaxReplacementShaders_[parallaxSlot].Get() :
                    replacementShaders_[contractIndex].Get();
            }
        } else if (binding.family == ReplacementShaderFamily::sky &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= skyReplacementShaders_.size()) {
            expectedShader =
                skyReplacementShaders_[binding.contractPlusOne - 1].Get();
        } else if (
            binding.family == ReplacementShaderFamily::distantTree &&
            binding.contractPlusOne == 1) {
            expectedShader = distantTreeReplacementShaders_[0].Get();
        } else if (binding.family == ReplacementShaderFamily::particle &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= particleReplacementShaders_.size()) {
            expectedShader =
                particleReplacementShaders_[binding.contractPlusOne - 1].Get();
        } else if (binding.family == ReplacementShaderFamily::water &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= waterReplacementShaders_.size()) {
            expectedShader =
                waterReplacementShaders_[binding.contractPlusOne - 1].Get();
        } else if (
            binding.family == ReplacementShaderFamily::vlsComposite &&
            binding.contractPlusOne == 1) {
            expectedShader = vlsCompositeReplacementShaders_[0].Get();
        } else if (binding.family == ReplacementShaderFamily::effect &&
            binding.contractPlusOne > 0 &&
            binding.contractPlusOne <= effectReplacementShaders_.size()) {
            expectedShader =
                effectReplacementShaders_[binding.contractPlusOne - 1].Get();
        }
        if (!context || context != context_.Get() || !expectedShader) {
            return 0;
        }

        ID3D11PixelShader* observedShader{};
        ID3D11Buffer* observedFrame{};
        ID3D11Buffer* observedGeometry{};
        context->PSGetShader(&observedShader, nullptr, nullptr);
        if ((binding.constantFlags & ReplacementPixelConstants_Frame) != 0) {
            context->PSGetConstantBuffers(5, 1, &observedFrame);
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Geometry) != 0) {
            context->PSGetConstantBuffers(8, 1, &observedGeometry);
        }

        std::uint32_t state{};
        if (observedShader == expectedShader) {
            state |= PipelineBinding_SelectedReplacement;
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Frame) != 0 &&
            observedFrame == frameBuffer_.Get()) {
            state |= PipelineBinding_FrameBuffer;
        }
        if ((binding.constantFlags & ReplacementPixelConstants_Geometry) != 0 &&
            observedGeometry == geometryBuffer_.Get()) {
            state |= PipelineBinding_GeometryBuffer;
        }

        if (observedGeometry) {
            observedGeometry->Release();
        }
        if (observedFrame) {
            observedFrame->Release();
        }
        if (observedShader) {
            observedShader->Release();
        }
        return state;
    }

    std::uint64_t Runtime::geometryUpdateGeneration() const noexcept
    {
        return geometryUpdates_.load(std::memory_order_acquire);
    }

    bool Runtime::dFLightAmbientDescriptorReady(
        std::uint32_t descriptor) const noexcept
    {
        const auto found = std::lower_bound(
            kDFLightAmbientDescriptorContracts.begin(),
            kDFLightAmbientDescriptorContracts.end(),
            descriptor,
            [](const DFLightAmbientDescriptorContract& contract,
                std::uint32_t value) {
                return contract.descriptor < value;
            });
        if (found == kDFLightAmbientDescriptorContracts.end() ||
            found->descriptor != descriptor ||
            found->contractIndex >= kDFLightAmbientShaderContractCount) {
            return false;
        }
        return (readyDFLightAmbientContractMask_.load(
                    std::memory_order_acquire) &
                   (1ull << found->contractIndex)) != 0;
    }

    const char* Runtime::shaderContractName(
        std::size_t contractIndex) noexcept
    {
        return contractIndex < kShaderContracts.size() ?
            kShaderContracts[contractIndex].name :
            "<invalid>";
    }

    void Runtime::queueSettings(const Settings& settings) noexcept
    {
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            queuedSettings_ = sanitize(settings);
        }
        queuedSettingsRevision_.fetch_add(1, std::memory_order_release);
    }

    void Runtime::queueComplexParallaxSettings(
        const complex_materials::Settings& settings) noexcept
    {
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            queuedComplexParallaxSettings_ =
                complex_materials::sanitize(settings);
        }
        queuedComplexParallaxSettingsRevision_.fetch_add(
            1,
            std::memory_order_release);
    }

    void Runtime::applyQueuedSettingsForRenderBoundary() noexcept
    {
        const auto linearRevision =
            queuedSettingsRevision_.load(std::memory_order_acquire);
        const auto complexRevision =
            queuedComplexParallaxSettingsRevision_.load(
                std::memory_order_acquire);
        const auto applyLinear = linearRevision !=
            appliedSettingsRevision_.load(std::memory_order_acquire);
        const auto applyComplex = complexRevision !=
            appliedComplexParallaxSettingsRevision_.load(
                std::memory_order_acquire);
        if (!applyLinear && !applyComplex) {
            return;
        }

        Settings nextLinear{};
        complex_materials::Settings nextComplex{};
        {
            std::scoped_lock lock(queuedSettingsMutex_);
            nextLinear = queuedSettings_;
            nextComplex = queuedComplexParallaxSettings_;
        }

        if (applyLinear) {
            applySettings(nextLinear);
            appliedSettingsRevision_.store(
                linearRevision,
                std::memory_order_release);
        }
        if (applyComplex) {
            applyComplexParallaxSettings(nextComplex);
            appliedComplexParallaxSettingsRevision_.store(
                complexRevision,
                std::memory_order_release);
        }
    }

    bool Runtime::updateGeometryEmissive(float emissiveMultiplier) noexcept
    {
        const auto reject = [this](std::atomic_uint64_t& reason) noexcept {
            reason.fetch_add(1, std::memory_order_relaxed);
            rejectedGeometryUpdates_.fetch_add(1, std::memory_order_relaxed);
            return false;
        };

        if (!std::isfinite(emissiveMultiplier) ||
            emissiveMultiplier < 0.0f ||
            emissiveMultiplier > 1.0e6f) {
            return reject(geometryInvalidSourceRejects_);
        }

        auto* context = context_.Get();
        if (!context ||
            !gpuResourcesReady_.load(std::memory_order_acquire) ||
            !geometryBuffer_) {
            return reject(geometryResourceRejects_);
        }
        if (!enabled_.load(std::memory_order_acquire)) {
            return reject(geometryDisabledRejects_);
        }

        // FO4VR performs geometry setup while a previous or vanilla pixel
        // shader can still be active. Update the private b8 resource here,
        // but bind it only inside a replacement draw so vanilla stereo state
        // is never overwritten between draws.
        GeometryData data{};
        data.emissiveMultiplier = emissiveMultiplier;
        context->UpdateSubresource(geometryBuffer_.Get(), 0, nullptr, &data, 0, 0);
        geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Runtime::setGeometryProviderReady(bool ready) noexcept
    {
        const auto previous =
            geometryProviderReady_.exchange(ready, std::memory_order_acq_rel);
        if (previous == ready) {
            return;
        }
        logging::info(
            "Linear Lighting verified geometry provider is {}.",
            ready ? "ready" : "unavailable");
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        auto next = sanitize(settings);
        const auto previousAmbientGamma = calibratedLightingResponseGamma(
            settings_.preserveNativeDarkness,
            settings_.ambientGamma);
        const auto nextAmbientGamma = calibratedLightingResponseGamma(
            next.preserveNativeDarkness,
            next.ambientGamma);
        if (nextAmbientGamma != previousAmbientGamma && device_ &&
            !rebuildDFLightAmbientReplacements(nextAmbientGamma)) {
            logging::error(
                "DFLight ambient gamma update failed to rebuild every observed replacement; retaining gamma {} while applying the remaining settings.",
                previousAmbientGamma);
            next.ambientGamma = settings_.ambientGamma;
            next.preserveNativeDarkness = settings_.preserveNativeDarkness;
        }
        settings_ = next;
        dFLightAmbientGammaBits_.store(
            std::bit_cast<std::uint32_t>(
                calibratedLightingResponseGamma(
                    settings_.preserveNativeDarkness,
                    settings_.ambientGamma)),
            std::memory_order_release);
        enabled_.store(settings_.enabled, std::memory_order_release);
        render::setDFPrePassLinearLightingEnabled(settings_.enabled);
        publishDFTiledPointLightSettings(settings_);
        render::publishDFLightProducerSettings(settings_);
        publishFrameData();
    }

    void Runtime::applyComplexParallaxSettings(
        const complex_materials::Settings& settings) noexcept
    {
        complexParallaxSettings_ = complex_materials::sanitize(settings);
        complexParallaxEnabled_.store(
            complexParallaxSettings_.parallaxEnabled,
            std::memory_order_release);
        complexParallaxQuality_.store(
            complexParallaxSettings_.parallaxQuality,
            std::memory_order_release);
        complexEnvironmentEnabled_.store(
            complexParallaxSettings_.environmentResponseEnabled,
            std::memory_order_release);
        render::setDFPrePassComplexEnvironmentEnabled(
            complexParallaxSettings_.environmentResponseEnabled ||
                pbrMaterialsEnabled_.load(std::memory_order_acquire));
        publishFrameData();
    }

    void Runtime::synchronizeLightProducerFrameState() noexcept
    {
        const auto producerState = dFTiledPointLightProducerFrameState();
        if (producerState.revision != publishedLightProducerRevision_) {
            publishFrameData();
        }
    }

    void Runtime::publishFrameData() noexcept
    {
        if (!context_ || !frameBuffer_) {
            return;
        }
        const auto producerState = dFTiledPointLightProducerFrameState();
        const auto data = makeFrameData(
            settings_,
            true,
            false,
            1.0f,
            producerState.gamma,
            complexParallaxSettings_);
        context_->UpdateSubresource(frameBuffer_.Get(), 0, nullptr, &data, 0, 0);
        publishedLightProducerRevision_ = producerState.revision;
        frameDataUploads_.fetch_add(1, std::memory_order_relaxed);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .enabled = enabled_.load(std::memory_order_acquire),
            .complexParallaxEnabled =
                complexParallaxEnabled_.load(std::memory_order_acquire),
            .complexParallaxResourcesReady =
                complexParallaxResourcesReady_.load(
                    std::memory_order_acquire),
            .complexParallaxQuality =
                complexParallaxQuality_.load(std::memory_order_acquire),
            .complexParallaxReplacementBinds =
                complexParallaxReplacementBinds_.load(
                    std::memory_order_relaxed),
            .complexEnvironmentEnabled =
                complexEnvironmentEnabled_.load(
                    std::memory_order_acquire),
            .complexEnvironmentConsumerReady =
                complexEnvironmentConsumerReady_.load(
                    std::memory_order_acquire),
            .complexEnvironmentReplacementBinds =
                complexEnvironmentReplacementBinds_.load(
                    std::memory_order_relaxed),
            .complexEnvironmentDescriptorRejects =
                complexEnvironmentDescriptorRejects_.load(
                    std::memory_order_relaxed),
            .gpuResourcesReady = gpuResourcesReady_.load(std::memory_order_acquire),
            .geometryProviderReady =
                geometryProviderReady_.load(std::memory_order_acquire),
            .verifiedShaderContracts =
                static_cast<std::uint32_t>(kShaderContracts.size()),
            .matchingShaderContractMask =
                matchingShaderContractMask_.load(std::memory_order_relaxed),
            .matchingShadersCreated = matchingShadersCreated_.load(std::memory_order_relaxed),
            .trackedOriginalShaders = trackedOriginalShaders_.load(std::memory_order_relaxed),
            .matchingGrassVertexShaderIdentityMask =
                matchingGrassVertexShaderIdentityMask_.load(
                    std::memory_order_relaxed),
            .matchingGrassVertexShadersCreated =
                matchingGrassVertexShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedGrassVertexShaders =
                trackedGrassVertexShaders_.load(std::memory_order_relaxed),
            .grassVertexClassSelections =
                grassVertexClassSelections_.load(std::memory_order_relaxed),
            .firstReplacementContractPlusOne =
                firstReplacementContractPlusOne_.load(
                    std::memory_order_acquire),
            .verifiedSkyShaderContracts =
                static_cast<std::uint32_t>(kSkyShaderContracts.size()),
            .matchingSkyShaderContractMask =
                matchingSkyShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingSkyShadersCreated =
                matchingSkyShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalSkyShaders =
                trackedOriginalSkyShaders_.load(
                    std::memory_order_relaxed),
            .skyReplacementBinds =
                skyReplacementBinds_.load(std::memory_order_relaxed),
            .verifiedDistantTreeShaderContracts =
                static_cast<std::uint32_t>(
                    kDistantTreeShaderContractCount),
            .matchingDistantTreeShaderContractMask =
                matchingDistantTreeShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingDistantTreeShadersCreated =
                matchingDistantTreeShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalDistantTreeShaders =
                trackedOriginalDistantTreeShaders_.load(
                    std::memory_order_relaxed),
            .distantTreeReplacementBinds =
                distantTreeReplacementBinds_.load(
                    std::memory_order_relaxed),
            .verifiedParticleShaderContracts =
                static_cast<std::uint32_t>(kParticleShaderContracts.size()),
            .matchingParticleShaderContractMask =
                matchingParticleShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingParticleShadersCreated =
                matchingParticleShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalParticleShaders =
                trackedOriginalParticleShaders_.load(
                    std::memory_order_relaxed),
            .particleReplacementBinds =
                particleReplacementBinds_.load(std::memory_order_relaxed),
            .verifiedWaterShaderContracts =
                static_cast<std::uint32_t>(kWaterShaderContracts.size()),
            .matchingWaterShaderContractMask =
                matchingWaterShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingWaterShadersCreated =
                matchingWaterShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalWaterShaders =
                trackedOriginalWaterShaders_.load(
                    std::memory_order_relaxed),
            .waterReplacementBinds =
                waterReplacementBinds_.load(std::memory_order_relaxed),
            .verifiedVLSCompositeShaderContracts =
                static_cast<std::uint32_t>(kVLSCompositeShaderContractCount),
            .matchingVLSCompositeShaderContractMask =
                matchingVLSCompositeShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingVLSCompositeShadersCreated =
                matchingVLSCompositeShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalVLSCompositeShaders =
                trackedOriginalVLSCompositeShaders_.load(
                    std::memory_order_relaxed),
            .vlsCompositeReplacementBinds =
                vlsCompositeReplacementBinds_.load(
                    std::memory_order_relaxed),
            .verifiedEffectShaderContracts =
                static_cast<std::uint32_t>(kEffectShaderContracts.size()),
            .matchingEffectShaderContractMask =
                matchingEffectShaderContractMask_.load(
                    std::memory_order_relaxed),
            .matchingEffectShadersCreated =
                matchingEffectShadersCreated_.load(
                    std::memory_order_relaxed),
            .trackedOriginalEffectShaders =
                trackedOriginalEffectShaders_.load(
                    std::memory_order_relaxed),
            .effectReplacementBinds =
                effectReplacementBinds_.load(std::memory_order_relaxed),
            .shaderSelectionCalls =
                shaderSelectionCalls_.load(std::memory_order_relaxed),
            .rejectedShaderContexts =
                rejectedShaderContexts_.load(std::memory_order_relaxed),
            .inactiveShaderSelections =
                inactiveShaderSelections_.load(std::memory_order_relaxed),
            .unmatchedShaderSelections =
                unmatchedShaderSelections_.load(std::memory_order_relaxed),
            .replacementBinds = replacementBinds_.load(std::memory_order_relaxed),
            .replacementConstantScopes =
                replacementConstantScopes_.load(std::memory_order_relaxed),
            .replacementConstantRestores =
                replacementConstantRestores_.load(std::memory_order_relaxed),
            .shaderBindingLookupFailures =
                shaderBindingLookupFailures_.load(std::memory_order_relaxed),
            .verifiedDFLightAmbientShaderContracts =
                static_cast<std::uint32_t>(
                    kDFLightAmbientContracts.size()),
            .matchingDFLightAmbientContractMask =
                matchingDFLightAmbientContractMask_.load(
                    std::memory_order_relaxed),
            .readyDFLightAmbientContractMask =
                readyDFLightAmbientContractMask_.load(
                    std::memory_order_acquire),
            .matchingDFLightAmbientShaders =
                matchingDFLightAmbientShaders_.load(
                    std::memory_order_relaxed),
            .trackedDFLightAmbientShaders =
                trackedDFLightAmbientShaders_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementBinds =
                dFLightAmbientReplacementBinds_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementBuilds =
                dFLightAmbientReplacementBuilds_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientReplacementFailures =
                dFLightAmbientReplacementFailures_.load(
                    std::memory_order_relaxed),
            .dFLightAmbientGammaRebuilds =
                dFLightAmbientGammaRebuilds_.load(
                    std::memory_order_relaxed),
            .geometryUpdates = geometryUpdates_.load(std::memory_order_relaxed),
            .rejectedGeometryUpdates = rejectedGeometryUpdates_.load(std::memory_order_relaxed),
            .geometryResourceRejects =
                geometryResourceRejects_.load(std::memory_order_relaxed),
            .geometryDisabledRejects =
                geometryDisabledRejects_.load(std::memory_order_relaxed),
            .geometryInvalidSourceRejects =
                geometryInvalidSourceRejects_.load(std::memory_order_relaxed),
            .queuedSettingsRevision =
                queuedSettingsRevision_.load(std::memory_order_acquire),
            .appliedSettingsRevision =
                appliedSettingsRevision_.load(std::memory_order_acquire),
            .frameDataUploads =
                frameDataUploads_.load(std::memory_order_relaxed),
        };
    }
}
