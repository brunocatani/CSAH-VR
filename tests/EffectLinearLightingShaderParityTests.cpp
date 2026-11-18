#include "Features/linear_lighting/LinearLightingSettings.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::linear_lighting::FrameData;
    using csah::linear_lighting::Settings;
    using csah::linear_lighting::makeFrameData;

    using Pixel = std::array<float, 4>;

    constexpr float kTolerance = 1.0e-4F;
    constexpr Pixel kBaseColor{ 0.42F, 0.63F, 0.31F, 0.77F };
    constexpr Pixel kPropertyColor{ 0.8F, 0.45F, 0.7F, 0.9F };
    constexpr Pixel kFogParam{ 0.65F, 0.8F, 0.45F, 0.7F };
    constexpr Pixel kTextureColor{ 0.2F, 0.4F, 0.6F, 0.3F };
    constexpr Pixel kDepthTexture{ 1.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kDepthTestTexturePass{ 1.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kDepthTestTextureFail{ 0.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kPipboyTexture{ 0.25F, 0.5F, 0.75F, 0.65F };
    constexpr Pixel kPipboyControlsConverted{ 1.0F, 0.0F, 0.8F, 0.45F };
    constexpr Pixel kPipboyControlsRaw{ 0.0F, 1.0F, 0.6F, 0.4F };
    constexpr std::array<Pixel, 16> kGrayscaleTexture{ {
        { 0.11F, 0.21F, 0.31F, 0.17F },
        { 0.14F, 0.24F, 0.34F, 0.27F },
        { 0.18F, 0.28F, 0.38F, 0.37F },
        { 0.22F, 0.32F, 0.42F, 0.47F },
        { 0.26F, 0.36F, 0.46F, 0.23F },
        { 0.30F, 0.40F, 0.50F, 0.33F },
        { 0.34F, 0.44F, 0.54F, 0.43F },
        { 0.38F, 0.48F, 0.58F, 0.53F },
        { 0.42F, 0.52F, 0.62F, 0.29F },
        { 0.46F, 0.56F, 0.66F, 0.39F },
        { 0.50F, 0.60F, 0.70F, 0.49F },
        { 0.54F, 0.64F, 0.74F, 0.59F },
        { 0.58F, 0.68F, 0.78F, 0.35F },
        { 0.62F, 0.72F, 0.82F, 0.45F },
        { 0.66F, 0.76F, 0.86F, 0.55F },
        { 0.70F, 0.80F, 0.90F, 0.65F },
    } };
    constexpr Pixel kDepthParameters{ 0.0F, 1.0F, 1.0F, 0.0F };
    constexpr Pixel kUIMaskControls{ 1.0F, 4.0F, 0.0F, 0.75F };
    constexpr Pixel kUIMaskRectangle{ -0.6F, 0.4F, 0.8F, 0.8F };
    constexpr Pixel kUIMaskColor{ 0.25F, 0.5F, 0.75F, 0.0F };
    constexpr Pixel kModelPosition{ 0.2F, -0.1F, 0.3F, 0.0F };
    constexpr std::array<Pixel, 2> kPointLightPositionX{ {
        { 1.0F, -0.8F, 0.4F, 1.5F },
        { -0.5F, 0.9F, -1.2F, 0.1F },
    } };
    constexpr std::array<Pixel, 2> kPointLightPositionY{ {
        { 0.3F, -1.0F, 0.7F, -0.6F },
        { 1.1F, -0.4F, 0.2F, -1.3F },
    } };
    constexpr std::array<Pixel, 2> kPointLightPositionZ{ {
        { -0.2F, 0.8F, 1.4F, 0.1F },
        { 0.6F, -0.9F, 1.0F, 1.7F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionX{ {
        { -0.8F, 0.6F, -0.2F, -0.4F },
        { 0.4F, -0.7F, 0.8F, 0.1F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionY{ {
        { 0.1F, -0.7F, 0.5F, 0.8F },
        { -0.6F, 0.2F, -0.1F, 0.9F },
    } };
    constexpr std::array<Pixel, 2> kSpotLightDirectionZ{ {
        { 0.6F, 0.4F, -0.84F, -0.45F },
        { 0.69F, 0.68F, 0.59F, -0.42F },
    } };
    constexpr Pixel kSpotLightExponent{ 1.5F, 0.0F, 2.0F, 0.5F };
    constexpr Pixel kSpotLightCosHalfAngle{ 0.2F, 0.0F, 0.5F, -0.2F };
    constexpr Pixel kPointLightInverseRadius{ 0.4F, 0.5F, 0.35F, 0.25F };
    constexpr Pixel kPointLightColorR{ 0.35F, 0.15F, 0.55F, 0.25F };
    constexpr Pixel kPointLightColorG{ 0.1F, 0.45F, 0.2F, 0.6F };
    constexpr Pixel kPointLightColorB{ 0.5F, 0.3F, 0.12F, 0.4F };
    constexpr Pixel kDirectionalLightColor{ 0.12F, 0.08F, 0.18F, 0.0F };
    constexpr Pixel kVertexColor{ 0.55F, 0.75F, 0.35F, 0.6F };
    constexpr Pixel kMembraneNormal{ 0.1F, 0.2F, 0.8F, 0.65F };
    constexpr Pixel kMembraneViewVector{ 0.2F, -0.1F, 0.6F, 0.0F };
    constexpr Pixel kMembraneNormalMapTexture{ 0.65F, 0.9F, 0.25F, 1.0F };
    constexpr Pixel kMembraneNormalMapViewVector{ 0.2F, -0.1F, 0.6F, 0.72F };
    constexpr Pixel kMembraneTangent0{ 0.7F, 0.2F, -0.1F, 0.0F };
    constexpr Pixel kMembraneTangent1{ -0.3F, 0.8F, 0.4F, 0.0F };
    constexpr Pixel kMembraneTangent2{ 0.5F, -0.6F, 0.9F, 0.0F };
    constexpr Pixel kMembraneRimColor{ 0.3F, 0.5F, 0.7F, 0.4F };
    constexpr Pixel kMembraneVariables{ 1.3F, 0.0F, 0.75F, 0.0F };
    constexpr Pixel kEnvironmentNormalTexture{ 0.75F, 0.25F, 0.0F, 0.6F };
    constexpr Pixel kEnvironmentMaskTexture{ 0.4F, 0.0F, 0.0F, 0.0F };
    constexpr std::array<Pixel, 4> kDistortionFieldTexture{ {
        { 0.15F, 0.85F, 0.0F, 1.0F },
        { 0.75F, 0.75F, 0.0F, 1.0F },
        { 0.35F, 0.65F, 0.0F, 1.0F },
        { 0.55F, 0.45F, 0.0F, 1.0F },
    } };
    constexpr std::array<Pixel, 4> kDistortionMaskTexture{ {
        { 0.1F, 0.2F, 0.3F, 0.4F },
        { 0.2F, 0.3F, 0.4F, 0.5F },
        { 0.3F, 0.4F, 0.5F, 0.6F },
        { 0.7F, 0.6F, 0.5F, 0.4F },
    } };
    constexpr Pixel kDistortedTextureColor{
        kTextureColor[0] * kDistortionMaskTexture[3][0],
        kTextureColor[1] * kDistortionMaskTexture[3][1],
        kTextureColor[2] * kDistortionMaskTexture[3][2],
        kTextureColor[3] * kDistortionMaskTexture[3][3],
    };
    constexpr Pixel kEnvironmentTangent0{ 0.0F, 1.0F, 0.0F, 0.0F };
    constexpr Pixel kEnvironmentTangent1{ 0.0F, 0.0F, 1.0F, 0.0F };
    constexpr Pixel kEnvironmentTangent2{ 1.0F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kEnvironmentViewVectorLeft{ 0.0F, -0.6F, 0.8F, 0.0F };
    constexpr Pixel kEnvironmentViewVectorRight{ -0.8F, 0.6F, 0.0F, 0.0F };
    constexpr std::array<Pixel, 6> kEnvironmentCubeFaces{ {
        { 0.17F, 0.29F, 0.41F, 1.0F },
        { 0.53F, 0.11F, 0.23F, 1.0F },
        { 0.31F, 0.47F, 0.19F, 1.0F },
        { 0.67F, 0.37F, 0.13F, 1.0F },
        { 0.07F, 0.59F, 0.71F, 1.0F },
        { 0.79F, 0.61F, 0.43F, 1.0F },
    } };
    constexpr Pixel kAlphaMaskTextureFail{};
    constexpr float kEnvironmentMapScale = 0.75F;
    constexpr float kLightingInfluence = 0.35F;
    constexpr float kSoftDepthScale = 1.0F;
    constexpr float kSoftParticleDepth = 0.75F;
    constexpr float kDepthTestValue = 0.25F;
    constexpr float kGrayscaleInput = 0.65F;
    constexpr float kBaseColorScale = 0.8F;
    static_assert(kDepthTestTexturePass[0] >= kDepthTestValue);
    static_assert(kDepthTestTextureFail[0] < kDepthTestValue);

    struct EffectContract
    {
        const char* name;
        std::uint32_t descriptor;

        [[nodiscard]] constexpr bool vertexColored() const noexcept
        {
            return (descriptor & 0x1U) != 0;
        }

        [[nodiscard]] constexpr bool textured() const noexcept
        {
            return (descriptor & 0x4U) != 0;
        }

        [[nodiscard]] constexpr bool additive() const noexcept
        {
            return (descriptor & 0x20U) != 0;
        }

        [[nodiscard]] constexpr bool multiplyBlend() const noexcept
        {
            return (descriptor & 0x40U) != 0;
        }

        [[nodiscard]] constexpr bool particle() const noexcept
        {
            return (descriptor & 0x80U) != 0;
        }

        [[nodiscard]] constexpr bool soft() const noexcept
        {
            return (descriptor & 0x1000U) != 0;
        }

        [[nodiscard]] constexpr bool grayscaleColor() const noexcept
        {
            return (descriptor & 0x2000U) != 0;
        }

        [[nodiscard]] constexpr bool grayscaleAlpha() const noexcept
        {
            return (descriptor & 0x4000U) != 0;
        }

        [[nodiscard]] constexpr bool falloff() const noexcept
        {
            return (descriptor & 0x10U) != 0;
        }

        [[nodiscard]] constexpr bool rgbFalloff() const noexcept
        {
            return (descriptor & 0x00200000U) != 0;
        }

        [[nodiscard]] constexpr bool membrane() const noexcept
        {
            return (descriptor & 0x00000200U) != 0;
        }

        [[nodiscard]] constexpr bool environmentMap() const noexcept
        {
            return (descriptor & 0x00080000U) != 0;
        }

        [[nodiscard]] constexpr bool particleDistortion() const noexcept
        {
            return (descriptor & 0x00400000U) != 0;
        }

        [[nodiscard]] constexpr bool normalMappedMembrane() const noexcept
        {
            return membrane() && (descriptor & 0x00800000U) == 0;
        }

        [[nodiscard]] constexpr bool membraneTangentBasis() const noexcept
        {
            return normalMappedMembrane() && (descriptor & 0x2U) != 0;
        }

        [[nodiscard]] constexpr bool alphaMaskTested() const noexcept
        {
            return (descriptor & 0x00020000U) != 0;
        }

        [[nodiscard]] constexpr bool ignoresTextureAlpha() const noexcept
        {
            return (descriptor & 0x00008000U) != 0;
        }

        [[nodiscard]] constexpr bool usesBaseTextureAlpha() const noexcept
        {
            return textured() && !grayscaleAlpha();
        }

        [[nodiscard]] constexpr bool depthTested() const noexcept
        {
            return (descriptor & 0x03000000U) != 0;
        }

        [[nodiscard]] constexpr bool pipboy() const noexcept
        {
            return (descriptor & 0x00100000U) != 0;
        }

        [[nodiscard]] constexpr bool uiMaskRects() const noexcept
        {
            return (descriptor & 0x08000000U) != 0;
        }

        [[nodiscard]] constexpr bool lighting() const noexcept
        {
            return (descriptor & 0x00000400U) != 0;
        }

        [[nodiscard]] constexpr bool premultipliedAlpha() const noexcept
        {
            return (descriptor & 0x40000000U) != 0;
        }

        [[nodiscard]] constexpr bool needsParticleData() const noexcept
        {
            return particle() || soft();
        }
    };

    constexpr std::array<EffectContract, 631> kEffectContracts{ {
        { "EffectDefault_00000000", 0x00000000U },
        { "EffectVertexColor_00000001", 0x00000001U },
        { "EffectTextured_00000004", 0x00000004U },
        { "EffectVertexColorTextured_00000005", 0x00000005U },
        { "EffectAdditive_00000020", 0x00000020U },
        { "EffectVertexColorAdditive_00000021", 0x00000021U },
        { "EffectTexturedAdditive_00000024", 0x00000024U },
        { "EffectVertexColorTexturedAdditive_00000025", 0x00000025U },
        { "EffectMultiplyBlend_00000040", 0x00000040U },
        { "EffectVertexColorMultiplyBlend_00000041", 0x00000041U },
        { "EffectTexturedMultiplyBlend_00000044", 0x00000044U },
        { "EffectVertexColorTexturedMultiplyBlend_00000045", 0x00000045U },
        { "EffectVertexColorParticle_00000081", 0x00000081U },
        { "EffectVertexColorTexturedParticle_00000085", 0x00000085U },
        { "EffectTexturedParticle_0000008C", 0x0000008CU },
        { "EffectVertexColorTexturedAdditiveParticle_000000A5", 0x000000A5U },
        { "EffectSoft_00001000", 0x00001000U },
        { "EffectVertexColorSoft_00001001", 0x00001001U },
        { "EffectTexturedSoft_00001004", 0x00001004U },
        { "EffectVertexColorTexturedSoft_00001005", 0x00001005U },
        { "EffectAdditiveSoft_00001020", 0x00001020U },
        { "EffectVertexColorAdditiveSoft_00001021", 0x00001021U },
        { "EffectTexturedAdditiveSoft_00001024", 0x00001024U },
        { "EffectVertexColorTexturedAdditiveSoft_00001025", 0x00001025U },
        { "EffectVertexColorMultiplyBlendSoft_00001041", 0x00001041U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoft_000010CD", 0x000010CDU },
        { "EffectPipboy_00100000", 0x00100000U },
        { "EffectVertexColorPipboy_00100001", 0x00100001U },
        { "EffectTexturedPipboy_00100004", 0x00100004U },
        { "EffectVertexColorTexturedPipboy_00100005", 0x00100005U },
        { "EffectTexturedAdditivePipboy_00100024", 0x00100024U },
        { "EffectVertexColorTexturedAdditivePipboy_00100025", 0x00100025U },
        { "EffectDepthTest_01000000", 0x01000000U },
        { "EffectTexturedAdditiveDepthTest_01000024", 0x01000024U },
        { "EffectPremultipliedAlpha_40000000", 0x40000000U },
        { "EffectVertexColorPremultipliedAlpha_40000001", 0x40000001U },
        { "EffectTexturedPremultipliedAlpha_40000004", 0x40000004U },
        { "EffectVertexColorTexturedPremultipliedAlpha_40000005", 0x40000005U },
        { "EffectAdditivePremultipliedAlpha_40000020", 0x40000020U },
        { "EffectVertexColorAdditivePremultipliedAlpha_40000021", 0x40000021U },
        { "EffectTexturedAdditivePremultipliedAlpha_40000024", 0x40000024U },
        { "EffectVertexColorTexturedAdditivePremultipliedAlpha_40000025", 0x40000025U },
        { "EffectVertexColorTexturedMultiplyBlendPremultipliedAlpha_40000045", 0x40000045U },
        { "EffectVertexColorTexturedParticlePremultipliedAlpha_40000085", 0x40000085U },
        { "EffectVertexColorTexturedAdditiveParticlePremultipliedAlpha_400000A5", 0x400000A5U },
        { "EffectSoftPremultipliedAlpha_40001000", 0x40001000U },
        { "EffectTexturedSoftPremultipliedAlpha_40001004", 0x40001004U },
        { "EffectVertexColorTexturedSoftPremultipliedAlpha_40001005", 0x40001005U },
        { "EffectTexturedAdditiveSoftPremultipliedAlpha_40001024", 0x40001024U },
        { "EffectVertexColorTexturedAdditiveSoftPremultipliedAlpha_40001025", 0x40001025U },
        { "EffectTexturedPipboyPremultipliedAlpha_40100004", 0x40100004U },
        { "EffectVertexColorTexturedPipboyPremultipliedAlpha_40100005", 0x40100005U },
        { "EffectTexturedMultiplyBlendPremultipliedAlpha_50000044", 0x50000044U },
        { "EffectTexturedGrayscaleColor_00002004", 0x00002004U },
        { "EffectVertexColorTexturedGrayscaleColor_00002005", 0x00002005U },
        { "EffectTexturedAdditiveGrayscaleColor_00002024", 0x00002024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColor_00002025", 0x00002025U },
        { "EffectTexturedMultiplyBlendGrayscaleColor_00002044", 0x00002044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColor_00002045", 0x00002045U },
        { "EffectVertexColorTexturedParticleGrayscaleColor_00002085", 0x00002085U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColor_000020A5", 0x000020A5U },
        { "EffectTexturedSoftGrayscaleColor_00003004", 0x00003004U },
        { "EffectVertexColorTexturedSoftGrayscaleColor_00003005", 0x00003005U },
        { "EffectTexturedAdditiveSoftGrayscaleColor_00003024", 0x00003024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColor_00003025", 0x00003025U },
        { "EffectTexturedGrayscaleAlpha_00004004", 0x00004004U },
        { "EffectVertexColorTexturedGrayscaleAlpha_00004005", 0x00004005U },
        { "EffectTexturedAdditiveGrayscaleAlpha_00004024", 0x00004024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlpha_00004025", 0x00004025U },
        { "EffectTexturedMultiplyBlendGrayscaleAlpha_00004044", 0x00004044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleAlpha_00004045", 0x00004045U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlpha_000040A5", 0x000040A5U },
        { "EffectTexturedSoftGrayscaleAlpha_00005004", 0x00005004U },
        { "EffectVertexColorTexturedSoftGrayscaleAlpha_00005005", 0x00005005U },
        { "EffectVertexColorAdditiveSoftGrayscaleAlpha_00005021", 0x00005021U },
        { "EffectTexturedAdditiveSoftGrayscaleAlpha_00005024", 0x00005024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlpha_00005025", 0x00005025U },
        { "EffectTexturedGrayscaleColorAlpha_00006004", 0x00006004U },
        { "EffectVertexColorTexturedGrayscaleColorAlpha_00006005", 0x00006005U },
        { "EffectTexturedAdditiveGrayscaleColorAlpha_00006024", 0x00006024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlpha_00006025", 0x00006025U },
        { "EffectTexturedMultiplyBlendGrayscaleColorAlpha_00006044", 0x00006044U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColorAlpha_00006045", 0x00006045U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlpha_00006085", 0x00006085U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlpha_000060A5", 0x000060A5U },
        { "EffectTexturedSoftGrayscaleColorAlpha_00007004", 0x00007004U },
        { "EffectVertexColorTexturedSoftGrayscaleColorAlpha_00007005", 0x00007005U },
        { "EffectVertexColorAdditiveSoftGrayscaleColorAlpha_00007021", 0x00007021U },
        { "EffectTexturedAdditiveSoftGrayscaleColorAlpha_00007024", 0x00007024U },
        { "EffectVertexColorTexturedGrayscaleColorPremultipliedAlpha_40002005", 0x40002005U },
        { "EffectTexturedAdditiveGrayscaleColorPremultipliedAlpha_40002024", 0x40002024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorPremultipliedAlpha_40002025", 0x40002025U },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorPremultipliedAlpha_400020A5", 0x400020A5U },
        { "EffectVertexColorTexturedSoftGrayscaleColorPremultipliedAlpha_40003005", 0x40003005U },
        { "EffectTexturedAdditiveSoftGrayscaleColorPremultipliedAlpha_40003024", 0x40003024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorPremultipliedAlpha_40003025", 0x40003025U },
        { "EffectTexturedGrayscaleAlphaPremultipliedAlpha_40004004", 0x40004004U },
        { "EffectVertexColorTexturedGrayscaleAlphaPremultipliedAlpha_40004005", 0x40004005U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlphaPremultipliedAlpha_40004025", 0x40004025U },
        { "EffectTexturedSoftGrayscaleAlphaPremultipliedAlpha_40005004", 0x40005004U },
        { "EffectTexturedAdditiveSoftGrayscaleAlphaPremultipliedAlpha_40005024", 0x40005024U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaPremultipliedAlpha_40005025", 0x40005025U },
        { "EffectTexturedAdditiveGrayscaleColorAlphaPremultipliedAlpha_40006024", 0x40006024U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlphaPremultipliedAlpha_40006025", 0x40006025U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleColorAlphaPremultipliedAlpha_40006045", 0x40006045U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorAlphaPremultipliedAlpha_40007025", 0x40007025U },
        { "EffectVertexColorTexturedMultiplyBlendGrayscaleAlphaPremultipliedAlpha_50004045", 0x50004045U },
        { "EffectVertexColorTexturedMultiplyBlendParticle_000000CD", 0x000000CDU },
        { "EffectVertexColorTexturedMultiplyBlendParticlePremultipliedAlpha_400000CD", 0x400000CDU },
        { "EffectTexturedUIMaskRects_08000004", 0x08000004U },
        { "EffectTexturedPipboyUIMaskRects_08100004", 0x08100004U },
        { "EffectTexturedUIMaskRectsPremultipliedAlpha_48000004", 0x48000004U },
        { "EffectLighting_00000400", 0x00000400U },
        { "EffectVertexColorLighting_00000401", 0x00000401U },
        { "EffectTexturedLighting_00000404", 0x00000404U },
        { "EffectVertexColorTexturedLighting_00000405", 0x00000405U },
        { "EffectAdditiveLighting_00000420", 0x00000420U },
        { "EffectVertexColorAdditiveLighting_00000421", 0x00000421U },
        { "EffectTexturedAdditiveLighting_00000424", 0x00000424U },
        { "EffectVertexColorTexturedAdditiveLighting_00000425", 0x00000425U },
        { "EffectMultiplyBlendLighting_00000440", 0x00000440U },
        { "EffectVertexColorMultiplyBlendLighting_00000441", 0x00000441U },
        { "EffectTexturedMultiplyBlendLighting_00000444", 0x00000444U },
        { "EffectVertexColorTexturedMultiplyBlendLighting_00000445", 0x00000445U },
        { "EffectVertexColorTexturedParticleLighting_00000485", 0x00000485U },
        { "EffectVertexColorTexturedAdditiveParticleLighting_000004A5", 0x000004A5U },
        { "EffectVertexColorTexturedMultiplyBlendParticleLighting_000004CD", 0x000004CDU },
        { "EffectTexturedLightingDepthTest_01000404", 0x01000404U },
        { "EffectTexturedAdditiveLightingDepthTest_01000424", 0x01000424U },
        { "EffectLightingPremultipliedAlpha_40000400", 0x40000400U },
        { "EffectTexturedLightingPremultipliedAlpha_40000404", 0x40000404U },
        { "EffectVertexColorTexturedLightingPremultipliedAlpha_40000405", 0x40000405U },
        { "EffectTexturedAdditiveLightingPremultipliedAlpha_40000424", 0x40000424U },
        { "EffectVertexColorTexturedAdditiveLightingPremultipliedAlpha_40000425", 0x40000425U },
        { "EffectVertexColorTexturedParticleLightingPremultipliedAlpha_4000048D", 0x4000048DU },
        { "EffectVertexColorTexturedAdditiveParticleLightingPremultipliedAlpha_400004A5", 0x400004A5U },
        { "EffectTexturedAdditiveLightingDepthTestPremultipliedAlpha_41000424", 0x41000424U },
        { "EffectSoftLighting_00001400", 0x00001400U },
        { "EffectVertexColorSoftLighting_00001401", 0x00001401U },
        { "EffectTexturedSoftLighting_00001404", 0x00001404U },
        { "EffectVertexColorTexturedSoftLighting_00001405", 0x00001405U },
        { "EffectVertexColorAdditiveSoftLighting_00001421", 0x00001421U },
        { "EffectTexturedAdditiveSoftLighting_00001424", 0x00001424U },
        { "EffectVertexColorTexturedAdditiveSoftLighting_00001425", 0x00001425U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoftLighting_000014CD", 0x000014CDU },
        { "EffectSoftLightingPremultipliedAlpha_40001400", 0x40001400U },
        { "EffectVertexColorTexturedSoftLightingPremultipliedAlpha_40001405", 0x40001405U },
        { "EffectVertexColorTexturedAdditiveSoftLightingPremultipliedAlpha_40001425", 0x40001425U },
        { "EffectVertexColorTexturedMultiplyBlendParticleSoftLightingPremultipliedAlpha_400014CD", 0x400014CDU },
        { "EffectTexturedGrayscaleColorLighting_00002404", 0x00002404U },
        { "EffectVertexColorTexturedGrayscaleColorLighting_00002405", 0x00002405U },
        { "EffectAdditiveGrayscaleColorLighting_00002420", 0x00002420U },
        { "EffectTexturedAdditiveGrayscaleColorLighting_00002424", 0x00002424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorLighting_00002425", 0x00002425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorLighting_0000248D", 0x0000248DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorLighting_000024AD", 0x000024ADU },
        { "EffectTexturedGrayscaleColorLightingPremultipliedAlpha_40002404", 0x40002404U },
        { "EffectVertexColorTexturedGrayscaleColorLightingPremultipliedAlpha_40002405", 0x40002405U },
        { "EffectTexturedAdditiveGrayscaleColorLightingPremultipliedAlpha_40002424", 0x40002424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorLightingPremultipliedAlpha_40002425", 0x40002425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorLightingPremultipliedAlpha_4000248D", 0x4000248DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorLightingPremultipliedAlpha_400024AD", 0x400024ADU },
        { "EffectTexturedSoftGrayscaleColorLighting_00003404", 0x00003404U },
        { "EffectVertexColorTexturedSoftGrayscaleColorLighting_00003405", 0x00003405U },
        { "EffectTexturedAdditiveSoftGrayscaleColorLighting_00003424", 0x00003424U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorLighting_00003425", 0x00003425U },
        { "EffectVertexColorTexturedSoftGrayscaleColorLightingPremultipliedAlpha_40003405", 0x40003405U },
        { "EffectVertexColorTexturedAdditiveParticleSoftGrayscaleColorLightingPremultipliedAlpha_400034A5", 0x400034A5U },
        { "EffectTexturedGrayscaleAlphaLighting_00004404", 0x00004404U },
        { "EffectVertexColorTexturedGrayscaleAlphaLighting_00004405", 0x00004405U },
        { "EffectVertexColorTexturedAdditiveGrayscaleAlphaLighting_00004425", 0x00004425U },
        { "EffectVertexColorTexturedParticleGrayscaleAlphaLighting_0000448D", 0x0000448DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaLighting_000044AD", 0x000044ADU },
        { "EffectVertexColorTexturedGrayscaleAlphaLightingPremultipliedAlpha_40004405", 0x40004405U },
        { "EffectVertexColorTexturedParticleGrayscaleAlphaLightingPremultipliedAlpha_4000448D", 0x4000448DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaLightingPremultipliedAlpha_400044AD", 0x400044ADU },
        { "EffectTexturedSoftGrayscaleAlphaLighting_00005404", 0x00005404U },
        { "EffectVertexColorTexturedSoftGrayscaleAlphaLighting_00005405", 0x00005405U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaLighting_00005425", 0x00005425U },
        { "EffectTexturedSoftGrayscaleAlphaLightingPremultipliedAlpha_40005404", 0x40005404U },
        { "EffectVertexColorTexturedSoftGrayscaleAlphaLightingPremultipliedAlpha_40005405", 0x40005405U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleAlphaLightingPremultipliedAlpha_40005425", 0x40005425U },
        { "EffectTexturedGrayscaleColorAlphaLighting_00006404", 0x00006404U },
        { "EffectVertexColorTexturedGrayscaleColorAlphaLighting_00006405", 0x00006405U },
        { "EffectTexturedAdditiveGrayscaleColorAlphaLighting_00006424", 0x00006424U },
        { "EffectVertexColorTexturedAdditiveGrayscaleColorAlphaLighting_00006425", 0x00006425U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaLighting_0000648D", 0x0000648DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaLighting_000064AD", 0x000064ADU },
        { "EffectTexturedGrayscaleColorAlphaLightingPremultipliedAlpha_40006404", 0x40006404U },
        { "EffectVertexColorTexturedGrayscaleColorAlphaLightingPremultipliedAlpha_40006405", 0x40006405U },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaLightingPremultipliedAlpha_4000648D", 0x4000648DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaLightingPremultipliedAlpha_400064AD", 0x400064ADU },
        { "EffectVertexColorSoftGrayscaleColorAlphaLighting_00007401", 0x00007401U },
        { "EffectTexturedSoftGrayscaleColorAlphaLighting_00007404", 0x00007404U },
        { "EffectTexturedAdditiveSoftGrayscaleColorAlphaLighting_00007424", 0x00007424U },
        { "EffectVertexColorTexturedAdditiveSoftGrayscaleColorAlphaLighting_00007425", 0x00007425U },
        { "EffectVertexColorTexturedSoftGrayscaleColorAlphaLightingPremultipliedAlpha_40007405", 0x40007405U },
        { "EffectVertexColorTexturedAdditiveParticleSoftGrayscaleColorAlphaLightingPremultipliedAlpha_400074AD", 0x400074ADU },
        { "EffectVertexColorTexturedParticleGrayscaleAlpha_0000408D", 0x0000408DU },
        { "EffectVertexColorTexturedParticleGrayscaleColorPremultipliedAlpha_4000208D", 0x4000208DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleAlphaPremultipliedAlpha_400040AD", 0x400040ADU },
        { "EffectVertexColorTexturedParticleSoftGrayscaleAlphaPremultipliedAlpha_4000508D", 0x4000508DU },
        { "EffectVertexColorTexturedParticleGrayscaleColorAlphaPremultipliedAlpha_4000608D", 0x4000608DU },
        { "EffectVertexColorTexturedAdditiveParticleGrayscaleColorAlphaPremultipliedAlpha_400060AD", 0x400060ADU },
        { "EffectVertexColorTexturedParticleSoftGrayscaleColorAlphaPremultipliedAlpha_4000708D", 0x4000708DU },
        { "EffectFalloffFamily_00800010", 0x00800010U },
        { "EffectFalloffFamily_00800011", 0x00800011U },
        { "EffectFalloffFamily_00800014", 0x00800014U },
        { "EffectFalloffFamily_00800015", 0x00800015U },
        { "EffectFalloffFamily_00800030", 0x00800030U },
        { "EffectFalloffFamily_00800031", 0x00800031U },
        { "EffectFalloffFamily_00800034", 0x00800034U },
        { "EffectFalloffFamily_00800035", 0x00800035U },
        { "EffectFalloffFamily_00800410", 0x00800410U },
        { "EffectFalloffFamily_00800411", 0x00800411U },
        { "EffectFalloffFamily_00800414", 0x00800414U },
        { "EffectFalloffFamily_00800415", 0x00800415U },
        { "EffectFalloffFamily_00800430", 0x00800430U },
        { "EffectFalloffFamily_00800431", 0x00800431U },
        { "EffectFalloffFamily_00800434", 0x00800434U },
        { "EffectFalloffFamily_00800435", 0x00800435U },
        { "EffectFalloffFamily_00801010", 0x00801010U },
        { "EffectFalloffFamily_00801011", 0x00801011U },
        { "EffectFalloffFamily_00801014", 0x00801014U },
        { "EffectFalloffFamily_00801015", 0x00801015U },
        { "EffectFalloffFamily_00801030", 0x00801030U },
        { "EffectFalloffFamily_00801031", 0x00801031U },
        { "EffectFalloffFamily_00801034", 0x00801034U },
        { "EffectFalloffFamily_00801035", 0x00801035U },
        { "EffectFalloffFamily_00801055", 0x00801055U },
        { "EffectFalloffFamily_00801411", 0x00801411U },
        { "EffectFalloffFamily_00801414", 0x00801414U },
        { "EffectFalloffFamily_00801415", 0x00801415U },
        { "EffectFalloffFamily_00801430", 0x00801430U },
        { "EffectFalloffFamily_00801431", 0x00801431U },
        { "EffectFalloffFamily_00801434", 0x00801434U },
        { "EffectFalloffFamily_00801435", 0x00801435U },
        { "EffectFalloffFamily_00802014", 0x00802014U },
        { "EffectFalloffFamily_00802015", 0x00802015U },
        { "EffectFalloffFamily_00802034", 0x00802034U },
        { "EffectFalloffFamily_00802035", 0x00802035U },
        { "EffectFalloffFamily_00802415", 0x00802415U },
        { "EffectFalloffFamily_00802435", 0x00802435U },
        { "EffectFalloffFamily_00803015", 0x00803015U },
        { "EffectFalloffFamily_00803034", 0x00803034U },
        { "EffectFalloffFamily_00803035", 0x00803035U },
        { "EffectFalloffFamily_00803414", 0x00803414U },
        { "EffectFalloffFamily_00803415", 0x00803415U },
        { "EffectFalloffFamily_00803434", 0x00803434U },
        { "EffectFalloffFamily_00803435", 0x00803435U },
        { "EffectFalloffFamily_00804434", 0x00804434U },
        { "EffectFalloffFamily_00805455", 0x00805455U },
        { "EffectFalloffFamily_00A00004", 0x00A00004U },
        { "EffectFalloffFamily_00A00005", 0x00A00005U },
        { "EffectFalloffFamily_00A00011", 0x00A00011U },
        { "EffectFalloffFamily_00A00014", 0x00A00014U },
        { "EffectFalloffFamily_00A00020", 0x00A00020U },
        { "EffectFalloffFamily_00A00024", 0x00A00024U },
        { "EffectFalloffFamily_00A00030", 0x00A00030U },
        { "EffectFalloffFamily_00A00031", 0x00A00031U },
        { "EffectFalloffFamily_00A00035", 0x00A00035U },
        { "EffectFalloffFamily_00A00404", 0x00A00404U },
        { "EffectFalloffFamily_00A00405", 0x00A00405U },
        { "EffectFalloffFamily_00A00414", 0x00A00414U },
        { "EffectFalloffFamily_00A00424", 0x00A00424U },
        { "EffectFalloffFamily_00A00430", 0x00A00430U },
        { "EffectFalloffFamily_00A01005", 0x00A01005U },
        { "EffectFalloffFamily_00A01014", 0x00A01014U },
        { "EffectFalloffFamily_00A01015", 0x00A01015U },
        { "EffectFalloffFamily_00A01024", 0x00A01024U },
        { "EffectFalloffFamily_00A01025", 0x00A01025U },
        { "EffectFalloffFamily_00A01030", 0x00A01030U },
        { "EffectFalloffFamily_00A01034", 0x00A01034U },
        { "EffectFalloffFamily_00A01035", 0x00A01035U },
        { "EffectFalloffFamily_00A01401", 0x00A01401U },
        { "EffectFalloffFamily_00A01424", 0x00A01424U },
        { "EffectFalloffFamily_00A01431", 0x00A01431U },
        { "EffectFalloffFamily_00A04015", 0x00A04015U },
        { "EffectFalloffFamily_00A04034", 0x00A04034U },
        { "EffectFalloffFamily_00A04035", 0x00A04035U },
        { "EffectFalloffFamily_00A05005", 0x00A05005U },
        { "EffectFalloffFamily_00A05014", 0x00A05014U },
        { "EffectFalloffFamily_00A05035", 0x00A05035U },
        { "EffectFalloffFamily_00A05055", 0x00A05055U },
        { "EffectFalloffFamily_00A05414", 0x00A05414U },
        { "EffectFalloffFamily_00A05415", 0x00A05415U },
        { "EffectFalloffFamily_00A05435", 0x00A05435U },
        { "EffectFalloffFamily_00A07055", 0x00A07055U },
        { "EffectFalloffFamily_01001414", 0x01001414U },
        { "EffectFalloffFamily_40800010", 0x40800010U },
        { "EffectFalloffFamily_40800011", 0x40800011U },
        { "EffectFalloffFamily_40800014", 0x40800014U },
        { "EffectFalloffFamily_40800015", 0x40800015U },
        { "EffectFalloffFamily_40800030", 0x40800030U },
        { "EffectFalloffFamily_40800031", 0x40800031U },
        { "EffectFalloffFamily_40800034", 0x40800034U },
        { "EffectFalloffFamily_40800035", 0x40800035U },
        { "EffectFalloffFamily_40800414", 0x40800414U },
        { "EffectFalloffFamily_40800415", 0x40800415U },
        { "EffectFalloffFamily_40800430", 0x40800430U },
        { "EffectFalloffFamily_40800434", 0x40800434U },
        { "EffectFalloffFamily_40800435", 0x40800435U },
        { "EffectFalloffFamily_40801015", 0x40801015U },
        { "EffectFalloffFamily_40801031", 0x40801031U },
        { "EffectFalloffFamily_40801034", 0x40801034U },
        { "EffectFalloffFamily_40801035", 0x40801035U },
        { "EffectFalloffFamily_40801415", 0x40801415U },
        { "EffectFalloffFamily_40801431", 0x40801431U },
        { "EffectFalloffFamily_40801435", 0x40801435U },
        { "EffectFalloffFamily_40802034", 0x40802034U },
        { "EffectFalloffFamily_40802035", 0x40802035U },
        { "EffectFalloffFamily_40802415", 0x40802415U },
        { "EffectFalloffFamily_40802435", 0x40802435U },
        { "EffectFalloffFamily_40803015", 0x40803015U },
        { "EffectFalloffFamily_40803034", 0x40803034U },
        { "EffectFalloffFamily_40803035", 0x40803035U },
        { "EffectFalloffFamily_40803414", 0x40803414U },
        { "EffectFalloffFamily_40803415", 0x40803415U },
        { "EffectFalloffFamily_40803435", 0x40803435U },
        { "EffectFalloffFamily_40804435", 0x40804435U },
        { "EffectFalloffFamily_40806015", 0x40806015U },
        { "EffectFalloffFamily_40806435", 0x40806435U },
        { "EffectFalloffFamily_40807034", 0x40807034U },
        { "EffectFalloffFamily_40A01014", 0x40A01014U },
        { "EffectFalloffFamily_40A02014", 0x40A02014U },
        { "EffectFalloffFamily_40A04035", 0x40A04035U },
        { "EffectFalloffFamily_40A05014", 0x40A05014U },
        { "EffectFalloffFamily_40A05015", 0x40A05015U },
        { "EffectFalloffFamily_40A05035", 0x40A05035U },
        { "EffectFalloffFamily_40A05415", 0x40A05415U },
        { "EffectFalloffFamily_40A06424", 0x40A06424U },
        { "EffectFalloffFamily_40A07014", 0x40A07014U },
        { "EffectMembraneCore_00800204", 0x00800204U },
        { "EffectMembraneCore_00800205", 0x00800205U },
        { "EffectMembraneCore_00800224", 0x00800224U },
        { "EffectMembraneCore_00800225", 0x00800225U },
        { "EffectMembraneCore_00800604", 0x00800604U },
        { "EffectMembraneCore_00800624", 0x00800624U },
        { "EffectMembraneCore_00800625", 0x00800625U },
        { "EffectMembraneCore_00802204", 0x00802204U },
        { "EffectMembraneCore_00802205", 0x00802205U },
        { "EffectMembraneCore_00802224", 0x00802224U },
        { "EffectMembraneCore_00802225", 0x00802225U },
        { "EffectMembraneCore_00802604", 0x00802604U },
        { "EffectMembraneCore_00802605", 0x00802605U },
        { "EffectMembraneCore_00802624", 0x00802624U },
        { "EffectMembraneCore_00802625", 0x00802625U },
        { "EffectMembraneCore_00804204", 0x00804204U },
        { "EffectMembraneCore_00804224", 0x00804224U },
        { "EffectMembraneCore_00804225", 0x00804225U },
        { "EffectMembraneCore_00804604", 0x00804604U },
        { "EffectMembraneCore_00804605", 0x00804605U },
        { "EffectMembraneCore_00804624", 0x00804624U },
        { "EffectMembraneCore_00806204", 0x00806204U },
        { "EffectMembraneCore_00806205", 0x00806205U },
        { "EffectMembraneCore_00806224", 0x00806224U },
        { "EffectMembraneCore_00806225", 0x00806225U },
        { "EffectMembraneCore_00806604", 0x00806604U },
        { "EffectMembraneAlphaMask_00820204", 0x00820204U },
        { "EffectMembraneAlphaMask_00820205", 0x00820205U },
        { "EffectMembraneAlphaMask_00820224", 0x00820224U },
        { "EffectMembraneAlphaMask_00820225", 0x00820225U },
        { "EffectMembraneAlphaMask_00820606", 0x00820606U },
        { "EffectMembraneAlphaMask_00820607", 0x00820607U },
        { "EffectMembraneAlphaMask_00820624", 0x00820624U },
        { "EffectMembraneAlphaMask_00820625", 0x00820625U },
        { "EffectMembraneAlphaMask_00822204", 0x00822204U },
        { "EffectMembraneAlphaMask_00822205", 0x00822205U },
        { "EffectMembraneAlphaMask_00822224", 0x00822224U },
        { "EffectMembraneAlphaMask_00822225", 0x00822225U },
        { "EffectMembraneAlphaMask_00822604", 0x00822604U },
        { "EffectMembraneAlphaMask_00822605", 0x00822605U },
        { "EffectMembraneAlphaMask_00822625", 0x00822625U },
        { "EffectMembraneAlphaMask_00822626", 0x00822626U },
        { "EffectMembraneAlphaMask_00824206", 0x00824206U },
        { "EffectMembraneAlphaMask_00824207", 0x00824207U },
        { "EffectMembraneAlphaMask_00824224", 0x00824224U },
        { "EffectMembraneAlphaMask_00824225", 0x00824225U },
        { "EffectMembraneAlphaMask_00824604", 0x00824604U },
        { "EffectMembraneAlphaMask_00824605", 0x00824605U },
        { "EffectMembraneAlphaMask_00824627", 0x00824627U },
        { "EffectMembraneAlphaMask_00826204", 0x00826204U },
        { "EffectMembraneAlphaMask_00826205", 0x00826205U },
        { "EffectMembraneAlphaMask_00826224", 0x00826224U },
        { "EffectMembraneAlphaMask_00826225", 0x00826225U },
        { "EffectMembraneAlphaMask_00826606", 0x00826606U },
        { "EffectMembraneAlphaMask_00826607", 0x00826607U },
        { "EffectMembraneVertexNormal_00800607", 0x00800607U },
        { "EffectMembraneVertexNormal_00804207", 0x00804207U },
        { "EffectMembraneVertexNormal_00804627", 0x00804627U },
        { "EffectMembraneVertexNormal_00806607", 0x00806607U },
        { "EffectMembraneVertexNormal_00808204", 0x00808204U },
        { "EffectMembraneVertexNormal_00808205", 0x00808205U },
        { "EffectMembraneVertexNormal_00808226", 0x00808226U },
        { "EffectMembraneVertexNormal_00808227", 0x00808227U },
        { "EffectMembraneVertexNormal_0080A226", 0x0080A226U },
        { "EffectMembraneVertexNormal_0080A227", 0x0080A227U },
        { "EffectMembraneVertexNormal_0080E205", 0x0080E205U },
        { "EffectMembraneVertexNormal_0080EA27", 0x0080EA27U },
        { "EffectMembraneVertexNormal_0080EE07", 0x0080EE07U },
        { "EffectMembraneVertexNormal_00828204", 0x00828204U },
        { "EffectMembraneVertexNormal_00828205", 0x00828205U },
        { "EffectMembraneVertexNormal_00828226", 0x00828226U },
        { "EffectMembraneVertexNormal_00828227", 0x00828227U },
        { "EffectMembraneVertexNormal_0082A226", 0x0082A226U },
        { "EffectMembraneVertexNormal_0082A227", 0x0082A227U },
        { "EffectMembraneNormalMap_00000206", 0x00000206U },
        { "EffectMembraneNormalMap_00000226", 0x00000226U },
        { "EffectMembraneNormalMap_00002206", 0x00002206U },
        { "EffectMembraneNormalMap_00002226", 0x00002226U },
        { "EffectMembraneNormalMap_00006206", 0x00006206U },
        { "EffectMembraneNormalMap_00006226", 0x00006226U },
        { "EffectMembraneNormalMap_00006227", 0x00006227U },
        { "EffectMembraneNormalMap_00020204", 0x00020204U },
        { "EffectMembraneNormalMap_00020226", 0x00020226U },
        { "EffectMembraneNormalMap_00026226", 0x00026226U },
        { "EffectEnvironmentMap_00880000", 0x00880000U },
        { "EffectEnvironmentMap_00880001", 0x00880001U },
        { "EffectEnvironmentMap_00880004", 0x00880004U },
        { "EffectEnvironmentMap_00880005", 0x00880005U },
        { "EffectEnvironmentMap_00880010", 0x00880010U },
        { "EffectEnvironmentMap_00880011", 0x00880011U },
        { "EffectEnvironmentMap_00880014", 0x00880014U },
        { "EffectEnvironmentMap_00880015", 0x00880015U },
        { "EffectEnvironmentMap_00880020", 0x00880020U },
        { "EffectEnvironmentMap_00880021", 0x00880021U },
        { "EffectEnvironmentMap_00880024", 0x00880024U },
        { "EffectEnvironmentMap_00880025", 0x00880025U },
        { "EffectEnvironmentMap_00880030", 0x00880030U },
        { "EffectEnvironmentMap_00880031", 0x00880031U },
        { "EffectEnvironmentMap_00880034", 0x00880034U },
        { "EffectEnvironmentMap_00880035", 0x00880035U },
        { "EffectEnvironmentMap_00880040", 0x00880040U },
        { "EffectEnvironmentMap_00880045", 0x00880045U },
        { "EffectEnvironmentMap_00880055", 0x00880055U },
        { "EffectEnvironmentMap_00880400", 0x00880400U },
        { "EffectEnvironmentMap_00880401", 0x00880401U },
        { "EffectEnvironmentMap_00880404", 0x00880404U },
        { "EffectEnvironmentMap_00880405", 0x00880405U },
        { "EffectEnvironmentMap_00880410", 0x00880410U },
        { "EffectEnvironmentMap_00880414", 0x00880414U },
        { "EffectEnvironmentMap_00880415", 0x00880415U },
        { "EffectEnvironmentMap_00880420", 0x00880420U },
        { "EffectEnvironmentMap_00880424", 0x00880424U },
        { "EffectEnvironmentMap_00880425", 0x00880425U },
        { "EffectEnvironmentMap_00880430", 0x00880430U },
        { "EffectEnvironmentMap_00880431", 0x00880431U },
        { "EffectEnvironmentMap_00880434", 0x00880434U },
        { "EffectEnvironmentMap_00880435", 0x00880435U },
        { "EffectEnvironmentMap_00880444", 0x00880444U },
        { "EffectEnvironmentMap_00880454", 0x00880454U },
        { "EffectEnvironmentMap_00881015", 0x00881015U },
        { "EffectEnvironmentMap_00881035", 0x00881035U },
        { "EffectEnvironmentMap_00881401", 0x00881401U },
        { "EffectEnvironmentMap_00881405", 0x00881405U },
        { "EffectEnvironmentMap_00881411", 0x00881411U },
        { "EffectEnvironmentMap_00881414", 0x00881414U },
        { "EffectEnvironmentMap_00881415", 0x00881415U },
        { "EffectEnvironmentMap_00881434", 0x00881434U },
        { "EffectEnvironmentMap_00881435", 0x00881435U },
        { "EffectEnvironmentMap_00882004", 0x00882004U },
        { "EffectEnvironmentMap_00882005", 0x00882005U },
        { "EffectEnvironmentMap_00882014", 0x00882014U },
        { "EffectEnvironmentMap_00882015", 0x00882015U },
        { "EffectEnvironmentMap_00882024", 0x00882024U },
        { "EffectEnvironmentMap_00882025", 0x00882025U },
        { "EffectEnvironmentMap_00882030", 0x00882030U },
        { "EffectEnvironmentMap_00882034", 0x00882034U },
        { "EffectEnvironmentMap_00882035", 0x00882035U },
        { "EffectEnvironmentMap_00882045", 0x00882045U },
        { "EffectEnvironmentMap_00882404", 0x00882404U },
        { "EffectEnvironmentMap_00882405", 0x00882405U },
        { "EffectEnvironmentMap_00882415", 0x00882415U },
        { "EffectEnvironmentMap_00882424", 0x00882424U },
        { "EffectEnvironmentMap_00882425", 0x00882425U },
        { "EffectEnvironmentMap_00882430", 0x00882430U },
        { "EffectEnvironmentMap_00882435", 0x00882435U },
        { "EffectEnvironmentMap_00882444", 0x00882444U },
        { "EffectEnvironmentMap_00883434", 0x00883434U },
        { "EffectEnvironmentMap_00883435", 0x00883435U },
        { "EffectEnvironmentMap_00884004", 0x00884004U },
        { "EffectEnvironmentMap_00884005", 0x00884005U },
        { "EffectEnvironmentMap_00884405", 0x00884405U },
        { "EffectEnvironmentMap_00884435", 0x00884435U },
        { "EffectEnvironmentMap_00885024", 0x00885024U },
        { "EffectEnvironmentMap_00885434", 0x00885434U },
        { "EffectEnvironmentMap_00886004", 0x00886004U },
        { "EffectEnvironmentMap_00886005", 0x00886005U },
        { "EffectEnvironmentMap_00886024", 0x00886024U },
        { "EffectEnvironmentMap_00886025", 0x00886025U },
        { "EffectEnvironmentMap_00886045", 0x00886045U },
        { "EffectEnvironmentMap_00886404", 0x00886404U },
        { "EffectEnvironmentMap_00886405", 0x00886405U },
        { "EffectEnvironmentMap_00886424", 0x00886424U },
        { "EffectEnvironmentMap_00886425", 0x00886425U },
        { "EffectEnvironmentMap_00887004", 0x00887004U },
        { "EffectEnvironmentMap_00887024", 0x00887024U },
        { "EffectEnvironmentMap_00887404", 0x00887404U },
        { "EffectEnvironmentMap_00887405", 0x00887405U },
        { "EffectEnvironmentMap_00887424", 0x00887424U },
        { "EffectEnvironmentMap_00887425", 0x00887425U },
        { "EffectEnvironmentMap_00980004", 0x00980004U },
        { "EffectEnvironmentMap_00980014", 0x00980014U },
        { "EffectEnvironmentMap_00A80004", 0x00A80004U },
        { "EffectEnvironmentMap_00A80005", 0x00A80005U },
        { "EffectEnvironmentMap_00A80014", 0x00A80014U },
        { "EffectEnvironmentMap_00A80015", 0x00A80015U },
        { "EffectEnvironmentMap_00A80030", 0x00A80030U },
        { "EffectEnvironmentMap_00A80034", 0x00A80034U },
        { "EffectEnvironmentMap_00A80035", 0x00A80035U },
        { "EffectEnvironmentMap_00A80404", 0x00A80404U },
        { "EffectEnvironmentMap_00A80405", 0x00A80405U },
        { "EffectEnvironmentMap_00A80414", 0x00A80414U },
        { "EffectEnvironmentMap_00A80415", 0x00A80415U },
        { "EffectEnvironmentMap_00A80424", 0x00A80424U },
        { "EffectEnvironmentMap_00A80425", 0x00A80425U },
        { "EffectEnvironmentMap_00A80434", 0x00A80434U },
        { "EffectEnvironmentMap_00A80435", 0x00A80435U },
        { "EffectEnvironmentMap_00A80454", 0x00A80454U },
        { "EffectEnvironmentMap_00A80455", 0x00A80455U },
        { "EffectEnvironmentMap_00A81005", 0x00A81005U },
        { "EffectEnvironmentMap_00A81411", 0x00A81411U },
        { "EffectEnvironmentMap_00A81434", 0x00A81434U },
        { "EffectEnvironmentMap_00A81435", 0x00A81435U },
        { "EffectEnvironmentMap_00A82455", 0x00A82455U },
        { "EffectEnvironmentMap_00A83404", 0x00A83404U },
        { "EffectEnvironmentMap_00A84435", 0x00A84435U },
        { "EffectEnvironmentMap_01080404", 0x01080404U },
        { "EffectEnvironmentMap_40880000", 0x40880000U },
        { "EffectEnvironmentMap_40880001", 0x40880001U },
        { "EffectEnvironmentMap_40880004", 0x40880004U },
        { "EffectEnvironmentMap_40880005", 0x40880005U },
        { "EffectEnvironmentMap_40880014", 0x40880014U },
        { "EffectEnvironmentMap_40880015", 0x40880015U },
        { "EffectEnvironmentMap_40880020", 0x40880020U },
        { "EffectEnvironmentMap_40880021", 0x40880021U },
        { "EffectEnvironmentMap_40880024", 0x40880024U },
        { "EffectEnvironmentMap_40880025", 0x40880025U },
        { "EffectEnvironmentMap_40880030", 0x40880030U },
        { "EffectEnvironmentMap_40880034", 0x40880034U },
        { "EffectEnvironmentMap_40880035", 0x40880035U },
        { "EffectEnvironmentMap_40880045", 0x40880045U },
        { "EffectEnvironmentMap_40880400", 0x40880400U },
        { "EffectEnvironmentMap_40880404", 0x40880404U },
        { "EffectEnvironmentMap_40880405", 0x40880405U },
        { "EffectEnvironmentMap_40880414", 0x40880414U },
        { "EffectEnvironmentMap_40880415", 0x40880415U },
        { "EffectEnvironmentMap_40880420", 0x40880420U },
        { "EffectEnvironmentMap_40880424", 0x40880424U },
        { "EffectEnvironmentMap_40880425", 0x40880425U },
        { "EffectEnvironmentMap_40880430", 0x40880430U },
        { "EffectEnvironmentMap_40880434", 0x40880434U },
        { "EffectEnvironmentMap_40881405", 0x40881405U },
        { "EffectEnvironmentMap_40881414", 0x40881414U },
        { "EffectEnvironmentMap_40882005", 0x40882005U },
        { "EffectEnvironmentMap_40882025", 0x40882025U },
        { "EffectEnvironmentMap_40882030", 0x40882030U },
        { "EffectEnvironmentMap_40882404", 0x40882404U },
        { "EffectEnvironmentMap_40882405", 0x40882405U },
        { "EffectEnvironmentMap_40882415", 0x40882415U },
        { "EffectEnvironmentMap_40882430", 0x40882430U },
        { "EffectEnvironmentMap_40884005", 0x40884005U },
        { "EffectEnvironmentMap_40884405", 0x40884405U },
        { "EffectEnvironmentMap_40886005", 0x40886005U },
        { "EffectEnvironmentMap_40886025", 0x40886025U },
        { "EffectEnvironmentMap_40886405", 0x40886405U },
        { "EffectEnvironmentMap_40886424", 0x40886424U },
        { "EffectEnvironmentMap_40886425", 0x40886425U },
        { "EffectEnvironmentMap_40A80030", 0x40A80030U },
        { "EffectEnvironmentMap_40A80035", 0x40A80035U },
        { "EffectEnvironmentMap_40A87404", 0x40A87404U },
        { "EffectParticleDistortion_0040008D", 0x0040008DU },
        { "EffectParticleDistortion_004000AD", 0x004000ADU },
        { "EffectParticleDistortion_004000CD", 0x004000CDU },
        { "EffectParticleDistortion_0040048D", 0x0040048DU },
        { "EffectParticleDistortion_004004AD", 0x004004ADU },
        { "EffectParticleDistortion_004004CD", 0x004004CDU },
        { "EffectParticleDistortion_0040108D", 0x0040108DU },
        { "EffectParticleDistortion_004010AD", 0x004010ADU },
        { "EffectParticleDistortion_004010CD", 0x004010CDU },
        { "EffectParticleDistortion_0040148D", 0x0040148DU },
        { "EffectParticleDistortion_004014A5", 0x004014A5U },
        { "EffectParticleDistortion_004014CD", 0x004014CDU },
        { "EffectParticleDistortion_0040208D", 0x0040208DU },
        { "EffectParticleDistortion_004020AD", 0x004020ADU },
        { "EffectParticleDistortion_0040308D", 0x0040308DU },
        { "EffectParticleDistortion_004030AD", 0x004030ADU },
        { "EffectParticleDistortion_0040348D", 0x0040348DU },
        { "EffectParticleDistortion_004034AD", 0x004034ADU },
        { "EffectParticleDistortion_0040408D", 0x0040408DU },
        { "EffectParticleDistortion_004040AD", 0x004040ADU },
        { "EffectParticleDistortion_004040CD", 0x004040CDU },
        { "EffectParticleDistortion_0040448D", 0x0040448DU },
        { "EffectParticleDistortion_004044AD", 0x004044ADU },
        { "EffectParticleDistortion_004044CD", 0x004044CDU },
        { "EffectParticleDistortion_0040508D", 0x0040508DU },
        { "EffectParticleDistortion_004050AD", 0x004050ADU },
        { "EffectParticleDistortion_004050CD", 0x004050CDU },
        { "EffectParticleDistortion_0040548D", 0x0040548DU },
        { "EffectParticleDistortion_004054AD", 0x004054ADU },
        { "EffectParticleDistortion_004054CD", 0x004054CDU },
        { "EffectParticleDistortion_0040608D", 0x0040608DU },
        { "EffectParticleDistortion_004060AD", 0x004060ADU },
        { "EffectParticleDistortion_00406485", 0x00406485U },
        { "EffectParticleDistortion_004064AD", 0x004064ADU },
        { "EffectParticleDistortion_0040708D", 0x0040708DU },
        { "EffectParticleDistortion_004070AD", 0x004070ADU },
        { "EffectParticleDistortion_0040748D", 0x0040748DU },
        { "EffectParticleDistortion_004074AD", 0x004074ADU },
        { "EffectParticleDistortion_4040008D", 0x4040008DU },
        { "EffectParticleDistortion_404000AD", 0x404000ADU },
        { "EffectParticleDistortion_4040048D", 0x4040048DU },
        { "EffectParticleDistortion_4040108D", 0x4040108DU },
        { "EffectParticleDistortion_404010AD", 0x404010ADU },
        { "EffectParticleDistortion_4040148D", 0x4040148DU },
        { "EffectParticleDistortion_404020AD", 0x404020ADU },
        { "EffectParticleDistortion_4040348D", 0x4040348DU },
        { "EffectParticleDistortion_404034AD", 0x404034ADU },
        { "EffectParticleDistortion_404044CD", 0x404044CDU },
        { "EffectParticleDistortion_4040508D", 0x4040508DU },
        { "EffectParticleDistortion_404050AD", 0x404050ADU },
        { "EffectParticleDistortion_4040548D", 0x4040548DU },
        { "EffectParticleDistortion_404054AD", 0x404054ADU },
        { "EffectParticleDistortion_404054CD", 0x404054CDU },
        { "EffectParticleDistortion_4040608D", 0x4040608DU },
        { "EffectParticleDistortion_404060AD", 0x404060ADU },
        { "EffectParticleDistortion_4040648D", 0x4040648DU },
        { "EffectParticleDistortion_4040708D", 0x4040708DU },
        { "EffectParticleDistortion_404070AD", 0x404070ADU },
        { "EffectParticleDistortion_4040748D", 0x4040748DU },
        { "EffectParticleDistortion_404074AD", 0x404074ADU },
    } };

    struct alignas(16) EffectPerTechnique
    {
        Pixel depthParameters{};
        Pixel uiMaskControls{};
        std::array<Pixel, 16> uiMaskRectangles{};
        Pixel uiMaskColor{};
    };
    static_assert(sizeof(EffectPerTechnique) == 19 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskControls) == sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskRectangles) == 2 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerTechnique, uiMaskColor) == 18 * sizeof(Pixel));

    struct alignas(16) EffectPerMaterial
    {
        Pixel baseColor{};
        Pixel baseColorScale{};
        Pixel lightingInfluence{};
        Pixel environmentMapScale{};
        Pixel depthTestParameters{};
    };

    struct alignas(16) EffectPerGeometry
    {
        std::array<Pixel, 2> pointLightPositionX{};
        std::array<Pixel, 2> pointLightPositionY{};
        std::array<Pixel, 2> pointLightPositionZ{};
        std::array<Pixel, 2> spotLightDirectionX{};
        std::array<Pixel, 2> spotLightDirectionY{};
        std::array<Pixel, 2> spotLightDirectionZ{};
        Pixel pipboyControls{};
        Pixel spotLightExponent{};
        Pixel spotLightCosHalfAngle{};
        Pixel pointLightInverseRadius{};
        Pixel pointLightColorR{};
        Pixel pointLightColorG{};
        Pixel pointLightColorB{};
        Pixel directionalLightColor{};
        Pixel propertyColor{};
        Pixel alphaTest{};
        Pixel membraneRimColor{};
        Pixel membraneVariables{};
    };
    static_assert(sizeof(EffectPerGeometry) == 24 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, spotLightDirectionX) == 6 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, pipboyControls) == 12 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, pointLightInverseRadius) ==
        15 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, directionalLightColor) ==
        19 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, propertyColor) == 20 * sizeof(Pixel));
    static_assert(offsetof(EffectPerGeometry, alphaTest) == 21 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, membraneRimColor) == 22 * sizeof(Pixel));
    static_assert(
        offsetof(EffectPerGeometry, membraneVariables) == 23 * sizeof(Pixel));

    struct RenderTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11Texture2D> staging;
    };

    void require(HRESULT result, const char* operation)
    {
        if (FAILED(result)) {
            throw std::runtime_error(
                std::string(operation) + " failed with HRESULT " +
                std::to_string(static_cast<std::uint32_t>(result)));
        }
    }

    std::vector<std::byte> readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("could not open " + path.string());
        }
        const auto size = stream.tellg();
        if (size <= 0) {
            throw std::runtime_error("empty shader " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("could not read " + path.string());
        }
        return bytes;
    }

    template <class T>
    ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device* device,
        const T& value)
    {
        static_assert(sizeof(T) % 16 == 0);
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(T));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{ &value, 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        require(
            device->CreateBuffer(&description, &initial, buffer.GetAddressOf()),
            "CreateBuffer");
        return buffer;
    }

    ComPtr<ID3D11VertexShader> createVertexShader(
        ID3D11Device* device,
        bool vertexColored,
        bool needsParticleData,
        bool depthTested,
        bool pipboy,
        bool lighting,
        bool rightEye,
        bool membraneVertexNormal = false,
        bool membraneNormalMap = false,
        bool membraneTangentBasis = false,
        bool environmentMap = false)
    {
        constexpr char source[] = R"(
struct VSOutput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#if defined(EFFECT_PIPBOY) && !defined(EFFECT_ENVIRONMENT_MAP)
    float4 pipboyTexCoord : TEXCOORD4;
#endif
#ifdef EFFECT_MEMBRANE_VERTEX_NORMAL
    float4 membraneNormal : TEXCOORD4;
#elif defined(EFFECT_MEMBRANE_NORMAL_MAP)
    float4 membraneViewVector : TEXCOORD4;
#endif
#ifdef EFFECT_ENVIRONMENT_MAP
    float4 environmentViewVector : TEXCOORD4;
#endif
#ifdef EFFECT_DEPTH_TEST
    float4 depthTestData : TEXCOORD3;
#endif
#ifdef EFFECT_ENVIRONMENT_MAP
    float3 environmentTangent0 : TEXCOORD7;
    float3 environmentTangent1 : TEXCOORD8;
    float3 environmentTangent2 : TEXCOORD9;
#endif
#ifdef EFFECT_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float4 color : COLOR1;
#ifdef EFFECT_PIPBOY
    float3 pipboyData : TEXCOORD1;
#endif
#ifdef EFFECT_MEMBRANE_VERTEX_NORMAL
    float3 membraneViewVector : TEXCOORD1;
#elif defined(EFFECT_MEMBRANE_TANGENT_BASIS)
    float3 membraneTangent0 : TEXCOORD1;
    float3 membraneTangent1 : TEXCOORD2;
    float3 membraneTangent2 : TEXCOORD3;
#endif
#ifdef EFFECT_LIGHTING
    float3 modelPosition : TEXCOORD6;
#endif
#ifdef EFFECT_PARTICLE
    float3 particleData : TEXCOORD5;
#endif
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    output.texCoord = float4(0.5, 0.5, 0.65, 0.0);
#if defined(EFFECT_PIPBOY) && !defined(EFFECT_ENVIRONMENT_MAP)
    output.pipboyTexCoord = 0.0.xxxx;
#endif
#ifdef EFFECT_PIPBOY
    output.pipboyData = 0.0.xxx;
#endif
#ifdef EFFECT_MEMBRANE_VERTEX_NORMAL
    output.membraneNormal = float4(0.1, 0.2, 0.8, 0.65);
    output.membraneViewVector = float3(0.2, -0.1, 0.6);
#elif defined(EFFECT_MEMBRANE_NORMAL_MAP)
    output.membraneViewVector = float4(0.2, -0.1, 0.6, 0.72);
#ifdef EFFECT_MEMBRANE_TANGENT_BASIS
    output.membraneTangent0 = float3(0.7, 0.2, -0.1);
    output.membraneTangent1 = float3(-0.3, 0.8, 0.4);
    output.membraneTangent2 = float3(0.5, -0.6, 0.9);
#endif
#endif
#ifdef EFFECT_ENVIRONMENT_MAP
#ifdef EFFECT_RIGHT_EYE
    output.environmentViewVector = float4(-0.8, 0.6, 0.0, 0.0);
#else
    output.environmentViewVector = float4(0.0, -0.6, 0.8, 0.0);
#endif
    output.environmentTangent0 = float3(0.0, 1.0, 0.0);
    output.environmentTangent1 = float3(0.0, 0.0, 1.0);
    output.environmentTangent2 = float3(1.0, 0.0, 0.0);
#endif
#ifdef EFFECT_LIGHTING
    output.modelPosition = float3(0.2, -0.1, 0.3);
#endif
#ifdef EFFECT_DEPTH_TEST
    output.depthTestData = float4(0.0, 0.0, 0.25, 0.0);
#endif
#ifdef EFFECT_VERTEX_COLOR
    output.vertexColor = float4(0.55, 0.75, 0.35, 0.6);
#endif
    output.color = float4(0.65, 0.8, 0.45, 0.7);
#ifdef EFFECT_PARTICLE
    output.particleData = float3(0.2, 0.4, 0.75);
#endif
#ifdef EFFECT_RIGHT_EYE
    output.eyeIndex = 1;
#else
    output.eyeIndex = 0;
#endif
    output.cullDistance = 1.0;
    output.clipDistance = 1.0;
    return output;
}
)";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        std::array<D3D_SHADER_MACRO, 11> macros{};
        std::size_t macroCount = 0;
        if (vertexColored) {
            macros[macroCount++] = { "EFFECT_VERTEX_COLOR", "1" };
        }
        if (needsParticleData) {
            macros[macroCount++] = { "EFFECT_PARTICLE", "1" };
        }
        if (depthTested) {
            macros[macroCount++] = { "EFFECT_DEPTH_TEST", "1" };
        }
        if (pipboy) {
            macros[macroCount++] = { "EFFECT_PIPBOY", "1" };
        }
        if (lighting) {
            macros[macroCount++] = { "EFFECT_LIGHTING", "1" };
        }
        if (rightEye) {
            macros[macroCount++] = { "EFFECT_RIGHT_EYE", "1" };
        }
        if (membraneVertexNormal) {
            macros[macroCount++] = { "EFFECT_MEMBRANE_VERTEX_NORMAL", "1" };
        }
        if (membraneNormalMap) {
            macros[macroCount++] = { "EFFECT_MEMBRANE_NORMAL_MAP", "1" };
        }
        if (membraneTangentBasis) {
            macros[macroCount++] = { "EFFECT_MEMBRANE_TANGENT_BASIS", "1" };
        }
        if (environmentMap) {
            macros[macroCount++] = { "EFFECT_ENVIRONMENT_MAP", "1" };
        }
        const auto result = D3DCompile(
            source,
            sizeof(source) - 1,
            "EffectLinearLightingParityVS",
            macroCount != 0 ? macros.data() : nullptr,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(result)) {
            const std::string detail = errors ?
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()) :
                "no compiler diagnostics";
            throw std::runtime_error("vertex shader compile failed: " + detail);
        }
        ComPtr<ID3D11VertexShader> shader;
        require(
            device->CreateVertexShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                shader.GetAddressOf()),
            "CreateVertexShader");
        return shader;
    }

    ComPtr<ID3D11PixelShader> createPixelShader(
        ID3D11Device* device,
        const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytes.data(), bytes.size(), nullptr, shader.GetAddressOf()),
            "CreatePixelShader");
        return shader;
    }

    ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device* device,
        const Pixel& pixel)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ pixel.data(), sizeof(Pixel), 0 };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView");
        return view;
    }

    ComPtr<ID3D11ShaderResourceView> createTexture2x2(
        ID3D11Device* device,
        const std::array<Pixel, 4>& pixels)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 2;
        description.Height = 2;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{
            pixels.data(),
            2 * sizeof(Pixel),
            0
        };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(2x2 shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView(2x2)");
        return view;
    }

    ComPtr<ID3D11ShaderResourceView> createEnvironmentCube(
        ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 6;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        std::array<D3D11_SUBRESOURCE_DATA, 6> initial{};
        for (std::size_t face = 0; face < initial.size(); ++face) {
            initial[face].pSysMem = kEnvironmentCubeFaces[face].data();
            initial[face].SysMemPitch = sizeof(Pixel);
        }

        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, initial.data(), texture.GetAddressOf()),
            "CreateTexture2D(environment cube)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView(environment cube)");
        return view;
    }

    ComPtr<ID3D11ShaderResourceView> createGrayscaleTexture(
        ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 4;
        description.Height = 4;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{
            kGrayscaleTexture.data(),
            4 * sizeof(Pixel),
            0
        };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(grayscale shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView(grayscale)");
        return view;
    }

    RenderTarget createRenderTarget(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;

        RenderTarget target;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.texture.GetAddressOf()),
            "CreateTexture2D(render target)");
        require(
            device->CreateRenderTargetView(
                target.texture.Get(), nullptr, target.view.GetAddressOf()),
            "CreateRenderTargetView");

        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.staging.GetAddressOf()),
            "CreateTexture2D(staging)");
        return target;
    }

    Pixel render(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* techniqueBuffer,
        ID3D11Buffer* materialBuffer,
        ID3D11Buffer* geometryBuffer,
        ID3D11Buffer* frameBuffer,
        ID3D11ShaderResourceView* texture,
        ID3D11ShaderResourceView* depthTexture,
        ID3D11ShaderResourceView* depthTestTexture,
        ID3D11ShaderResourceView* grayscaleTexture,
        ID3D11ShaderResourceView* pipboyTexture,
        ID3D11SamplerState* sampler,
        ID3D11ShaderResourceView* alphaMaskTexture = nullptr,
        ID3D11ShaderResourceView* normalTexture = nullptr,
        ID3D11ShaderResourceView* environmentTexture = nullptr,
        ID3D11ShaderResourceView* environmentMaskTexture = nullptr)
    {
        auto target = createRenderTarget(device);
        const float clear[4]{};
        context->ClearRenderTargetView(target.view.Get(), clear);
        ID3D11RenderTargetView* renderTarget = target.view.Get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        const D3D11_VIEWPORT viewport{ 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &techniqueBuffer);
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        context->PSSetConstantBuffers(2, 1, &geometryBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->PSSetShaderResources(0, 1, &texture);
        auto* boundNormalTexture = normalTexture != nullptr ?
            normalTexture : texture;
        context->PSSetShaderResources(1, 1, &boundNormalTexture);
        auto* boundAlphaMaskTexture = alphaMaskTexture != nullptr ?
            alphaMaskTexture : texture;
        context->PSSetShaderResources(2, 1, &boundAlphaMaskTexture);
        context->PSSetShaderResources(3, 1, &depthTexture);
        context->PSSetShaderResources(4, 1, &grayscaleTexture);
        context->PSSetShaderResources(5, 1, &environmentTexture);
        context->PSSetShaderResources(6, 1, &pipboyTexture);
        context->PSSetShaderResources(7, 1, &environmentMaskTexture);
        context->PSSetShaderResources(8, 1, &depthTestTexture);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetSamplers(1, 1, &sampler);
        context->PSSetSamplers(2, 1, &sampler);
        context->PSSetSamplers(4, 1, &sampler);
        context->PSSetSamplers(5, 1, &sampler);
        context->PSSetSamplers(6, 1, &sampler);
        context->PSSetSamplers(7, 1, &sampler);
        context->Draw(3, 0);

        context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(
            context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(render target)");
        Pixel result{};
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(target.staging.Get(), 0);

        ID3D11ShaderResourceView* nullTexture{};
        context->PSSetShaderResources(0, 1, &nullTexture);
        context->PSSetShaderResources(1, 1, &nullTexture);
        context->PSSetShaderResources(2, 1, &nullTexture);
        context->PSSetShaderResources(3, 1, &nullTexture);
        context->PSSetShaderResources(4, 1, &nullTexture);
        context->PSSetShaderResources(5, 1, &nullTexture);
        context->PSSetShaderResources(6, 1, &nullTexture);
        context->PSSetShaderResources(7, 1, &nullTexture);
        context->PSSetShaderResources(8, 1, &nullTexture);
        ID3D11RenderTargetView* nullTarget{};
        context->OMSetRenderTargets(1, &nullTarget, nullptr);
        return result;
    }

    bool nearValue(float actual, float expected)
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <=
                kTolerance * std::max(1.0F, std::abs(expected));
    }

    bool compare(
        const Pixel& actual,
        const Pixel& expected,
        const std::string& label)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel) {
            if (!nearValue(actual[channel], expected[channel])) {
                std::cerr << label << " channel " << channel << " expected "
                          << expected[channel] << " but got " << actual[channel]
                          << '\n';
                return false;
            }
        }
        return true;
    }

    float expectedSoftFade()
    {
        const auto deviceDepth = 1.0F - kDepthTexture[0];
        const auto sceneDepth =
            deviceDepth * kDepthParameters[2] + kDepthParameters[1];
        const auto cameraFadeDepth =
            kDepthParameters[1] * kDepthParameters[2] +
            kDepthParameters[1];
        const auto intersectionFade = std::clamp(
            kSoftDepthScale / sceneDepth - kSoftParticleDepth,
            0.0F,
            1.0F);
        auto cameraFade = std::clamp(
            kSoftParticleDepth - kSoftDepthScale / cameraFadeDepth,
            0.0F,
            1.0F);
        cameraFade = std::clamp(
            (cameraFade - 0.075F) * 2.352941176470588F,
            0.0F,
            1.0F);
        cameraFade =
            cameraFade * cameraFade * (3.0F - 2.0F * cameraFade);
        return intersectionFade * cameraFade;
    }

    Pixel sampleGrayscaleTexture(float u, float v)
    {
        const auto texel = [](float coordinate) {
            return std::min(
                3U,
                static_cast<unsigned>(
                    std::floor(std::max(0.0F, coordinate) * 4.0F)));
        };
        return kGrayscaleTexture[texel(v) * 4U + texel(u)];
    }

    const Pixel& effectTextureSample(const EffectContract& contract)
    {
        return contract.particleDistortion() ?
            kDistortedTextureColor : kTextureColor;
    }

    Pixel grayscaleColorSample(const EffectContract& contract)
    {
        const auto& textureColor = effectTextureSample(contract);
        auto v = std::pow(kBaseColor[0], 1.0F / 2.2F);
        if (!contract.particleDistortion()) {
            v *= kGrayscaleInput;
        }
        if (contract.vertexColored()) {
            v *= kVertexColor[0];
        }
        if (contract.soft()) {
            v *= expectedSoftFade();
        }
        const auto u = contract.particleDistortion() ?
            std::pow(kTextureColor[1], 1.0F / 2.2F) *
                kDistortionMaskTexture[3][1] :
            std::pow(textureColor[1], 1.0F / 2.2F);
        return sampleGrayscaleTexture(u, v);
    }

    float grayscaleAlphaSample(const EffectContract& contract)
    {
        const auto& textureColor = effectTextureSample(contract);
        auto v = std::pow(kBaseColor[3], 1.0F / 2.2F) *
            std::pow(kPropertyColor[3], 1.0F / 2.2F);
        if (!contract.particleDistortion()) {
            v *= kGrayscaleInput;
        }
        if (contract.vertexColored()) {
            v *= kVertexColor[3];
        }
        if (contract.soft() && !contract.particleDistortion()) {
            v *= expectedSoftFade();
        }
        return sampleGrayscaleTexture(textureColor[3], v)[3];
    }

    Pixel expectedEnvironmentColor(
        std::uint32_t eyeIndex,
        const Settings* settings)
    {
        const float normalX = kEnvironmentNormalTexture[0] * 2.0F - 1.0F;
        const float normalY = kEnvironmentNormalTexture[1] * 2.0F - 1.0F;
        const float normalZ = std::sqrt(
            1.0F - std::min(normalX * normalX + normalY * normalY, 1.0F));
        std::array<float, 3> normal{
            normalX * kEnvironmentTangent0[0] +
                normalY * kEnvironmentTangent1[0] +
                normalZ * kEnvironmentTangent2[0],
            normalX * kEnvironmentTangent0[1] +
                normalY * kEnvironmentTangent1[1] +
                normalZ * kEnvironmentTangent2[1],
            normalX * kEnvironmentTangent0[2] +
                normalY * kEnvironmentTangent1[2] +
                normalZ * kEnvironmentTangent2[2],
        };
        const float inverseNormalLength = 1.0F / std::sqrt(
            normal[0] * normal[0] +
            normal[1] * normal[1] +
            normal[2] * normal[2]);
        for (auto& channel : normal) {
            channel *= inverseNormalLength;
        }

        const auto& view = eyeIndex == 0 ?
            kEnvironmentViewVectorLeft : kEnvironmentViewVectorRight;
        const float viewNormalDot =
            view[0] * normal[0] +
            view[1] * normal[1] +
            view[2] * normal[2];
        const std::array<float, 3> reflection{
            view[0] - 2.0F * viewNormalDot * normal[0],
            view[1] - 2.0F * viewNormalDot * normal[1],
            view[2] - 2.0F * viewNormalDot * normal[2],
        };

        const auto absoluteX = std::abs(reflection[0]);
        const auto absoluteY = std::abs(reflection[1]);
        const auto absoluteZ = std::abs(reflection[2]);
        std::size_t face{};
        if (absoluteX >= absoluteY && absoluteX >= absoluteZ) {
            face = reflection[0] >= 0.0F ? 0U : 1U;
        } else if (absoluteY >= absoluteZ) {
            face = reflection[1] >= 0.0F ? 2U : 3U;
        } else {
            face = reflection[2] >= 0.0F ? 4U : 5U;
        }

        Pixel result{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto cubeColor = settings != nullptr ?
                std::pow(
                    std::abs(kEnvironmentCubeFaces[face][channel]),
                    settings->effectGamma) :
                kEnvironmentCubeFaces[face][channel];
            result[channel] = cubeColor *
                kEnvironmentMapScale *
                kEnvironmentNormalTexture[3] *
                kEnvironmentMaskTexture[0];
        }
        return result;
    }

    Pixel expectedMembrane(
        const EffectContract& contract,
        const Settings* settings)
    {
        const auto enabled = settings != nullptr && settings->enabled;
        const auto effectColor = [&](float color) {
            return enabled ?
                std::pow(std::abs(color), settings->effectGamma) :
                color;
        };
        const auto effectMembraneColor = [&](float color) {
            return enabled ?
                std::pow(
                    std::abs(color),
                    settings->effectGamma / 2.2F) :
                color;
        };

        Pixel membraneNormal = kMembraneNormal;
        const Pixel* membraneViewVector = &kMembraneViewVector;
        auto membraneGrayscaleScale = kMembraneNormal[3];
        if (contract.normalMappedMembrane()) {
            const Pixel tangentNormal{
                kMembraneNormalMapTexture[0] * 2.0F - 1.0F,
                kMembraneNormalMapTexture[2] * 2.0F - 1.0F,
                kMembraneNormalMapTexture[1] * 2.0F - 1.0F,
                0.0F
            };
            membraneNormal = tangentNormal;
            if (contract.membraneTangentBasis()) {
                membraneNormal[0] =
                    tangentNormal[0] * kMembraneTangent0[0] +
                    tangentNormal[1] * kMembraneTangent0[1] +
                    tangentNormal[2] * kMembraneTangent0[2];
                membraneNormal[1] =
                    tangentNormal[0] * kMembraneTangent1[0] +
                    tangentNormal[1] * kMembraneTangent1[1] +
                    tangentNormal[2] * kMembraneTangent1[2];
                membraneNormal[2] =
                    tangentNormal[0] * kMembraneTangent2[0] +
                    tangentNormal[1] * kMembraneTangent2[1] +
                    tangentNormal[2] * kMembraneTangent2[2];
            }
            membraneViewVector = &kMembraneNormalMapViewVector;
            membraneGrayscaleScale = kMembraneNormalMapViewVector[3];
        }

        Pixel baseColor = kTextureColor;
        if (contract.ignoresTextureAlpha()) {
            baseColor[3] = 1.0F;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            baseColor[channel] =
                effectColor(kTextureColor[channel]) *
                effectMembraneColor(kPropertyColor[channel]);
            if (contract.vertexColored()) {
                baseColor[channel] *= enabled ?
                    effectColor(kVertexColor[channel]) :
                    std::pow(kVertexColor[channel], 2.2F);
            }
        }
        baseColor[3] *= kPropertyColor[3];
        if (contract.vertexColored()) {
            baseColor[3] *= enabled ?
                kVertexColor[3] :
                std::pow(kVertexColor[3], 2.2F);
        }

        if (contract.grayscaleColor()) {
            auto v =
                std::pow(
                    kPropertyColor[0],
                    1.0F / 2.2F) *
                membraneGrayscaleScale;
            if (contract.vertexColored()) {
                v *= kVertexColor[0];
            }
            const auto grayscale = sampleGrayscaleTexture(
                std::pow(kTextureColor[1], 1.0F / 2.2F),
                v);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                baseColor[channel] = effectColor(
                    grayscale[channel] * kMembraneVariables[2]);
            }
        }
        if (contract.grayscaleAlpha()) {
            auto v =
                std::pow(kPropertyColor[3], 1.0F / 2.2F) *
                membraneGrayscaleScale;
            if (contract.vertexColored()) {
                v *= kVertexColor[3];
            }
            baseColor[3] =
                sampleGrayscaleTexture(kTextureColor[3], v)[3];
        }

        const auto normalDotView =
            membraneNormal[0] * (*membraneViewVector)[0] +
            membraneNormal[1] * (*membraneViewVector)[1] +
            membraneNormal[2] * (*membraneViewVector)[2];
        const auto membraneFactor = std::pow(
            std::clamp(1.0F - normalDotView, 0.0F, 1.0F),
            kMembraneVariables[0]);
        const auto membraneAlpha =
            kMembraneRimColor[3] * membraneFactor;
        baseColor[3] += membraneAlpha;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            baseColor[channel] +=
                effectMembraneColor(kMembraneRimColor[channel]) *
                membraneFactor * membraneAlpha;
            if (enabled) {
                baseColor[channel] *= settings->membraneEffectMultiplier;
            }
        }

        const auto fogFactor = enabled ?
            std::pow(
                std::abs(kFogParam[3]),
                settings->fogAlphaGamma) :
            kFogParam[3];
        Pixel result{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (contract.additive()) {
                result[channel] = baseColor[channel] * (1.0F - fogFactor);
            } else {
                const auto fogColor = enabled ?
                    std::pow(
                        std::abs(kFogParam[channel]),
                        settings->fogGamma / 2.2F) :
                    kFogParam[channel];
                result[channel] = baseColor[channel] +
                    fogFactor * (fogColor - baseColor[channel]);
            }
        }
        result[3] = enabled ?
            std::pow(
                std::abs(baseColor[3]),
                settings->effectAlphaGamma) :
            baseColor[3];
        return result;
    }

    float expectedUIMaskFactor()
    {
        constexpr float u = 0.5F;
        constexpr float v = 0.5F;
        const auto upperX = std::abs(kUIMaskRectangle[0]) - u;
        const auto upperY = kUIMaskRectangle[1] - v;
        const auto lowerX = u - kUIMaskRectangle[2];
        const auto lowerY = v - kUIMaskRectangle[3];
        const auto distance = std::max(
            std::max(upperX, lowerX),
            std::max(upperY, lowerY));
        const auto rectangleMask = std::clamp(
            1.0F - kUIMaskControls[1] * distance,
            0.0F,
            1.0F);
        auto verticalRamp = 1.0F;
        if (kUIMaskRectangle[0] < 0.0F &&
            v >= kUIMaskRectangle[1] - 0.0025F &&
            v <= kUIMaskRectangle[3] + 0.0025F) {
            verticalRamp = (v - kUIMaskRectangle[1]) /
                (kUIMaskRectangle[3] - kUIMaskRectangle[1]);
        }
        return rectangleMask * verticalRamp * kUIMaskControls[3];
    }

    Pixel expectedLightingColor(
        std::uint32_t eyeIndex,
        const Settings* enabledSettings)
    {
        if (eyeIndex >= kPointLightPositionX.size()) {
            throw std::runtime_error("invalid Effect lighting eye index");
        }

        Pixel attenuation{};
        for (std::size_t light = 0; light < attenuation.size(); ++light) {
            const auto deltaX =
                kModelPosition[0] - kPointLightPositionX[eyeIndex][light];
            const auto deltaY =
                kModelPosition[1] - kPointLightPositionY[eyeIndex][light];
            const auto deltaZ =
                kModelPosition[2] - kPointLightPositionZ[eyeIndex][light];
            const auto distance = std::sqrt(
                deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            const auto scaledDistance = std::clamp(
                distance * kPointLightInverseRadius[light],
                0.0F,
                1.0F);
            auto distanceFade =
                1.0F - scaledDistance * scaledDistance;
            if (enabledSettings == nullptr) {
                distanceFade = std::pow(distanceFade, 2.2F);
            }

            const auto safeDistance = std::max(distance, 0.001F);
            const auto spotCosine = std::clamp(
                deltaX / safeDistance *
                    kSpotLightDirectionX[eyeIndex][light] +
                deltaY / safeDistance *
                    kSpotLightDirectionY[eyeIndex][light] +
                deltaZ / safeDistance *
                    kSpotLightDirectionZ[eyeIndex][light],
                0.0F,
                1.0F);
            const auto coneFade = std::clamp(
                1.0F -
                    (1.0F - spotCosine) /
                        (1.0F - kSpotLightCosHalfAngle[light]),
                0.0F,
                1.0F);
            const auto spotFade = kSpotLightExponent[light] != 0.0F ?
                std::min(
                    std::pow(coneFade, kSpotLightExponent[light]),
                    1.0F) :
                1.0F;
            attenuation[light] = distanceFade * spotFade;
        }

        Pixel result = kDirectionalLightColor;
        const std::array<const Pixel*, 3> packedColors{
            &kPointLightColorR,
            &kPointLightColorG,
            &kPointLightColorB,
        };
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (enabledSettings != nullptr) {
                result[channel] *=
                    enabledSettings->effectLightingMultiplier;
            }
            for (std::size_t light = 0; light < attenuation.size(); ++light) {
                auto lightColor = (*packedColors[channel])[light];
                if (enabledSettings != nullptr) {
                    lightColor *= enabledSettings->pointLightMultiplier *
                        enabledSettings->effectLightingMultiplier;
                }
                result[channel] += attenuation[light] * lightColor;
            }
        }
        return result;
    }

    Pixel expectedVanilla(
        const EffectContract& contract,
        const Pixel& pipboyControls = kPipboyControlsConverted,
        bool usePipboyAlpha = true,
        bool uiMaskColorIsLinear = false,
        std::uint32_t eyeIndex = 0)
    {
        if (contract.membrane()) {
            return expectedMembrane(contract, nullptr);
        }
        if (contract.uiMaskRects()) {
            auto alpha = kTextureColor[3] *
                kBaseColor[3] * kPropertyColor[3];
            if (contract.pipboy() && usePipboyAlpha) {
                auto pipboyAlpha = kPipboyTexture[3];
                if (pipboyControls[1] == 0.0F) {
                    pipboyAlpha = std::pow(pipboyAlpha, 2.2F);
                }
                alpha = pipboyAlpha;
                if (pipboyControls[0] != 0.0F) {
                    alpha *= kBaseColor[3];
                }
                alpha *= pipboyControls[2];
            }
            alpha *= expectedUIMaskFactor();

            Pixel result{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                result[channel] = uiMaskColorIsLinear ?
                    kUIMaskColor[channel] :
                    std::pow(kUIMaskColor[channel], 2.2F);
                if (contract.premultipliedAlpha()) {
                    result[channel] *= alpha;
                }
            }
            result[3] = alpha;
            return result;
        }

        const auto& textureColor = effectTextureSample(contract);
        Pixel result{};
        auto alpha = contract.grayscaleAlpha() ?
            grayscaleAlphaSample(contract) :
            kBaseColor[3] *
                (contract.vertexColored() ?
                        std::pow(kVertexColor[3], 2.2F) :
                        1.0F) *
                (contract.usesBaseTextureAlpha() ? textureColor[3] : 1.0F) *
                kPropertyColor[3];
        if (contract.soft() &&
            (!contract.grayscaleAlpha() || contract.particleDistortion())) {
            alpha *= expectedSoftFade();
        }
        if (contract.falloff() && !contract.grayscaleAlpha()) {
            alpha *= kGrayscaleInput;
        }
        auto pipboyColor = kPipboyTexture;
        if (contract.pipboy() && pipboyControls[1] == 0.0F) {
            for (auto& channel : pipboyColor) {
                channel = std::pow(channel, 2.2F);
            }
        }
        auto outputAlpha = alpha;
        if (contract.pipboy()) {
            outputAlpha = usePipboyAlpha ? pipboyColor[3] : alpha;
            if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                outputAlpha *= kBaseColor[3];
            }
            outputAlpha *= usePipboyAlpha ? pipboyControls[2] : 1.0F;
        }
        const auto grayscaleColor = grayscaleColorSample(contract);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            auto baseColor = contract.grayscaleColor() ?
                grayscaleColor[channel] * kBaseColorScale :
                kBaseColor[channel] *
                    (contract.vertexColored() ?
                            std::pow(kVertexColor[channel], 2.2F) :
                            1.0F) *
                    (contract.textured() ?
                            textureColor[channel] :
                            1.0F);
            if (contract.rgbFalloff() && !contract.grayscaleColor()) {
                baseColor *= kGrayscaleInput;
            }
            if (contract.environmentMap()) {
                baseColor += expectedEnvironmentColor(
                    eyeIndex, nullptr)[channel];
            }
            const auto propertyColor = contract.lighting() ?
                expectedLightingColor(eyeIndex, nullptr)[channel] :
                kPropertyColor[channel];
            const auto lightColor = baseColor +
                kLightingInfluence *
                    (propertyColor * baseColor - baseColor);
            if (contract.additive()) {
                result[channel] = lightColor * (1.0F - kFogParam[3]);
            } else if (contract.multiplyBlend()) {
                const auto fogFactor =
                    std::clamp(1.5F * kFogParam[3], 0.0F, 1.0F);
                const auto foggedColor =
                    lightColor + fogFactor * (1.0F - lightColor);
                result[channel] = 1.0F + alpha * (foggedColor - 1.0F);
            } else {
                result[channel] = lightColor +
                    kFogParam[3] * (kFogParam[channel] - lightColor);
            }
            if (contract.pipboy()) {
                result[channel] +=
                    pipboyColor[channel] * pipboyControls[3];
                if (usePipboyAlpha && pipboyControls[0] != 0.0F) {
                    result[channel] *= kBaseColor[3];
                }
            }
            if (contract.premultipliedAlpha()) {
                result[channel] *= outputAlpha;
            }
        }
        result[3] = outputAlpha;
        return result;
    }

    int run(const std::filesystem::path& root)
    {
        D3D_FEATURE_LEVEL featureLevel{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &featureLevel,
                context.GetAddressOf()),
            "D3D11CreateDevice(WARP)");
        if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
            throw std::runtime_error("WARP did not provide feature level 11");
        }

        EffectPerTechnique techniqueConstants{};
        techniqueConstants.depthParameters = kDepthParameters;
        techniqueConstants.uiMaskControls = kUIMaskControls;
        techniqueConstants.uiMaskRectangles[0] = kUIMaskRectangle;
        techniqueConstants.uiMaskColor = kUIMaskColor;
        auto linearUIMaskTechniqueConstants = techniqueConstants;
        linearUIMaskTechniqueConstants.uiMaskControls[2] = 1.0F;
        EffectPerMaterial materialConstants{};
        materialConstants.baseColor = kBaseColor;
        materialConstants.baseColorScale[0] = kBaseColorScale;
        materialConstants.lightingInfluence[0] = kLightingInfluence;
        materialConstants.lightingInfluence[1] = kSoftDepthScale;
        materialConstants.environmentMapScale[0] = kEnvironmentMapScale;
        materialConstants.depthTestParameters[0] = 1.0F;
        EffectPerGeometry geometryConstants{};
        geometryConstants.pointLightPositionX = kPointLightPositionX;
        geometryConstants.pointLightPositionY = kPointLightPositionY;
        geometryConstants.pointLightPositionZ = kPointLightPositionZ;
        geometryConstants.spotLightDirectionX = kSpotLightDirectionX;
        geometryConstants.spotLightDirectionY = kSpotLightDirectionY;
        geometryConstants.spotLightDirectionZ = kSpotLightDirectionZ;
        geometryConstants.pipboyControls = kPipboyControlsConverted;
        geometryConstants.spotLightExponent = kSpotLightExponent;
        geometryConstants.spotLightCosHalfAngle = kSpotLightCosHalfAngle;
        geometryConstants.pointLightInverseRadius = kPointLightInverseRadius;
        geometryConstants.pointLightColorR = kPointLightColorR;
        geometryConstants.pointLightColorG = kPointLightColorG;
        geometryConstants.pointLightColorB = kPointLightColorB;
        geometryConstants.directionalLightColor = kDirectionalLightColor;
        geometryConstants.propertyColor = kPropertyColor;
        geometryConstants.alphaTest = { 0.001F, 0.8F, 0.0F, 1.0F };
        geometryConstants.membraneRimColor = kMembraneRimColor;
        geometryConstants.membraneVariables = kMembraneVariables;
        auto rawPipboyGeometryConstants = geometryConstants;
        rawPipboyGeometryConstants.pipboyControls = kPipboyControlsRaw;
        rawPipboyGeometryConstants.alphaTest[3] = 0.0F;
        const auto techniqueBuffer =
            createConstantBuffer(device.Get(), techniqueConstants);
        const auto linearUIMaskTechniqueBuffer =
            createConstantBuffer(device.Get(), linearUIMaskTechniqueConstants);
        const auto materialBuffer =
            createConstantBuffer(device.Get(), materialConstants);
        const auto geometryBuffer =
            createConstantBuffer(device.Get(), geometryConstants);
        const auto rawPipboyGeometryBuffer =
            createConstantBuffer(device.Get(), rawPipboyGeometryConstants);

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        const FrameData disabledFrame =
            makeFrameData(disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame =
            makeFrameData(
                enabledSettings,
                true,
                false,
                1.0F,
                enabledSettings.lightGamma);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);

        const auto texture = createTexture(device.Get(), kTextureColor);
        const auto membraneNormalMapTexture =
            createTexture(device.Get(), kMembraneNormalMapTexture);
        const auto environmentNormalTexture =
            createTexture(device.Get(), kEnvironmentNormalTexture);
        const auto environmentMaskTexture =
            createTexture(device.Get(), kEnvironmentMaskTexture);
        const auto environmentTexture = createEnvironmentCube(device.Get());
        const auto distortionFieldTexture =
            createTexture2x2(device.Get(), kDistortionFieldTexture);
        const auto distortionMaskTexture =
            createTexture2x2(device.Get(), kDistortionMaskTexture);
        const auto depthTexture = createTexture(device.Get(), kDepthTexture);
        const auto depthTestTexturePass =
            createTexture(device.Get(), kDepthTestTexturePass);
        const auto depthTestTextureFail =
            createTexture(device.Get(), kDepthTestTextureFail);
        const auto alphaMaskTextureFail =
            createTexture(device.Get(), kAlphaMaskTextureFail);
        const auto grayscaleTexture = createGrayscaleTexture(device.Get());
        const auto pipboyTexture = createTexture(device.Get(), kPipboyTexture);
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device->CreateSamplerState(
                &samplerDescription, sampler.GetAddressOf()),
            "CreateSamplerState");

        const auto defaultVertexShader =
            createVertexShader(
                device.Get(), false, false, false, false, false, false);
        const auto vertexColorVertexShader =
            createVertexShader(
                device.Get(), true, false, false, false, false, false);
        const auto particleVertexShader =
            createVertexShader(
                device.Get(), false, true, false, false, false, false);
        const auto vertexColorParticleVertexShader =
            createVertexShader(
                device.Get(), true, true, false, false, false, false);
        const auto depthTestVertexShader =
            createVertexShader(
                device.Get(), false, false, true, false, false, false);
        const auto pipboyVertexShader =
            createVertexShader(
                device.Get(), false, false, false, true, false, false);
        const auto vertexColorPipboyVertexShader =
            createVertexShader(
                device.Get(), true, false, false, true, false, false);
        const auto lightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, false);
        const auto rightEyeLightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, true);
        const auto vertexColorLightingVertexShader = createVertexShader(
            device.Get(), true, false, false, false, true, false);
        const auto rightEyeVertexColorLightingVertexShader = createVertexShader(
            device.Get(), true, false, false, false, true, true);
        const auto particleLightingVertexShader = createVertexShader(
            device.Get(), false, true, false, false, true, false);
        const auto rightEyeParticleLightingVertexShader = createVertexShader(
            device.Get(), false, true, false, false, true, true);
        const auto vertexColorParticleLightingVertexShader = createVertexShader(
            device.Get(), true, true, false, false, true, false);
        const auto rightEyeVertexColorParticleLightingVertexShader =
            createVertexShader(
                device.Get(), true, true, false, false, true, true);
        const auto depthTestLightingVertexShader = createVertexShader(
            device.Get(), false, false, true, false, true, false);
        const auto rightEyeDepthTestLightingVertexShader = createVertexShader(
            device.Get(), false, false, true, false, true, true);
        const auto depthTestParticleLightingVertexShader = createVertexShader(
            device.Get(), false, true, true, false, true, false);
        const auto rightEyeDepthTestParticleLightingVertexShader =
            createVertexShader(
                device.Get(), false, true, true, false, true, true);
        const auto membraneVertexShader = createVertexShader(
            device.Get(), false, false, false, false, false, false, true);
        const auto vertexColorMembraneVertexShader = createVertexShader(
            device.Get(), true, false, false, false, false, false, true);
        const auto membraneLightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, false, true);
        const auto rightEyeMembraneLightingVertexShader = createVertexShader(
            device.Get(), false, false, false, false, true, true, true);
        const auto vertexColorMembraneLightingVertexShader =
            createVertexShader(
                device.Get(), true, false, false, false, true, false, true);
        const auto rightEyeVertexColorMembraneLightingVertexShader =
            createVertexShader(
                device.Get(), true, false, false, false, true, true, true);
        const auto membraneNormalMapVertexShader = createVertexShader(
            device.Get(), false, false, false, false, false, false,
            false, true, false);
        const auto membraneNormalMapTangentVertexShader = createVertexShader(
            device.Get(), false, false, false, false, false, false,
            false, true, true);
        const auto vertexColorMembraneNormalMapTangentVertexShader =
            createVertexShader(
                device.Get(), true, false, false, false, false, false,
                false, true, true);
        std::array<ComPtr<ID3D11VertexShader>, 64>
            environmentVertexShaders{};
        const auto selectVertexShader = [&](const EffectContract& contract,
                                            bool rightEye) {
            if (contract.environmentMap()) {
                const std::size_t key =
                    (contract.vertexColored() ? 1U : 0U) |
                    (contract.needsParticleData() ? 2U : 0U) |
                    (contract.depthTested() ? 4U : 0U) |
                    (contract.pipboy() ? 8U : 0U) |
                    (contract.lighting() ? 16U : 0U) |
                    (rightEye ? 32U : 0U);
                auto& shader = environmentVertexShaders[key];
                if (!shader) {
                    shader = createVertexShader(
                        device.Get(),
                        contract.vertexColored(),
                        contract.needsParticleData(),
                        contract.depthTested(),
                        contract.pipboy(),
                        contract.lighting(),
                        rightEye,
                        false,
                        false,
                        false,
                        true);
                }
                return shader.Get();
            }
            if (contract.membrane()) {
                if (contract.normalMappedMembrane()) {
                    if (contract.vertexColored()) {
                        return vertexColorMembraneNormalMapTangentVertexShader.Get();
                    }
                    return contract.membraneTangentBasis() ?
                        membraneNormalMapTangentVertexShader.Get() :
                        membraneNormalMapVertexShader.Get();
                }
                if (!contract.lighting()) {
                    return contract.vertexColored() ?
                        vertexColorMembraneVertexShader.Get() :
                        membraneVertexShader.Get();
                }
                if (contract.vertexColored()) {
                    return rightEye ?
                        rightEyeVertexColorMembraneLightingVertexShader.Get() :
                        vertexColorMembraneLightingVertexShader.Get();
                }
                return rightEye ?
                    rightEyeMembraneLightingVertexShader.Get() :
                    membraneLightingVertexShader.Get();
            }
            if (contract.lighting()) {
                if (contract.depthTested()) {
                    if (contract.needsParticleData()) {
                        return rightEye ?
                            rightEyeDepthTestParticleLightingVertexShader.Get() :
                            depthTestParticleLightingVertexShader.Get();
                    }
                    return rightEye ?
                        rightEyeDepthTestLightingVertexShader.Get() :
                        depthTestLightingVertexShader.Get();
                }
                if (contract.needsParticleData()) {
                    if (contract.vertexColored()) {
                        return rightEye ?
                            rightEyeVertexColorParticleLightingVertexShader.Get() :
                            vertexColorParticleLightingVertexShader.Get();
                    }
                    return rightEye ?
                        rightEyeParticleLightingVertexShader.Get() :
                        particleLightingVertexShader.Get();
                }
                if (contract.vertexColored()) {
                    return rightEye ?
                        rightEyeVertexColorLightingVertexShader.Get() :
                        vertexColorLightingVertexShader.Get();
                }
                return rightEye ?
                    rightEyeLightingVertexShader.Get() :
                    lightingVertexShader.Get();
            }
            if (contract.depthTested()) {
                return depthTestVertexShader.Get();
            }
            if (contract.pipboy()) {
                return contract.vertexColored() ?
                    vertexColorPipboyVertexShader.Get() :
                    pipboyVertexShader.Get();
            }
            if (contract.needsParticleData()) {
                return contract.vertexColored() ?
                    vertexColorParticleVertexShader.Get() :
                    particleVertexShader.Get();
            }
            return contract.vertexColored() ?
                vertexColorVertexShader.Get() :
                defaultVertexShader.Get();
        };
        bool passed = true;
        for (const auto& contract : kEffectContracts) {
            auto* vertexShader = selectVertexShader(contract, false);
            auto* normalTexture = contract.particleDistortion() ?
                distortionFieldTexture.Get() :
                (contract.environmentMap() ?
                        environmentNormalTexture.Get() :
                        membraneNormalMapTexture.Get());
            auto* environmentCube = contract.environmentMap() ?
                environmentTexture.Get() : nullptr;
            auto* environmentMask = contract.particleDistortion() ?
                distortionMaskTexture.Get() :
                (contract.environmentMap() ?
                        environmentMaskTexture.Get() :
                        nullptr);
            const auto vanillaShader = createPixelShader(
                device.Get(),
                root /
                    "package/Shaders/Community/VerifiedEffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));
            const auto replacementShader = createPixelShader(
                device.Get(),
                root / "package/Shaders/Community/EffectLinearLighting" /
                    (std::string(contract.name) + ".dxbc"));

            const auto vanilla = render(
                device.Get(), context.Get(), vertexShader,
                vanillaShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                nullptr, normalTexture, environmentCube, environmentMask);
            const auto disabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                nullptr, normalTexture, environmentCube, environmentMask);
            const auto enabled = render(
                device.Get(), context.Get(), vertexShader,
                replacementShader.Get(), techniqueBuffer.Get(), materialBuffer.Get(),
                geometryBuffer.Get(), enabledFrameBuffer.Get(), texture.Get(),
                depthTexture.Get(), depthTestTexturePass.Get(),
                grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                nullptr, normalTexture, environmentCube, environmentMask);

            const std::string label = contract.name;
            passed &= compare(
                vanilla,
                expectedVanilla(contract),
                label + " vanilla model");
            passed &= compare(
                disabled,
                vanilla,
                label + " disabled parity");
            passed &= compare(
                enabled,
                expectedVanilla(contract),
                label + " enabled default parity");
            if (contract.lighting() || contract.environmentMap()) {
                auto* rightEyeVertexShader =
                    selectVertexShader(contract, true);
                const auto rightEyeVanilla = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto rightEyeDisabled = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto rightEyeEnabled = render(
                    device.Get(), context.Get(), rightEyeVertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                passed &= compare(
                    rightEyeVanilla,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        false,
                        1),
                    label + " right-eye vanilla model");
                passed &= compare(
                    rightEyeDisabled,
                    rightEyeVanilla,
                    label + " right-eye disabled parity");
                passed &= compare(
                    rightEyeEnabled,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        false,
                        1),
                    label + " right-eye enabled model");
            }
            if (contract.uiMaskRects()) {
                const auto vanillaLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto disabledLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto enabledLinearColor = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), linearUIMaskTechniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                passed &= compare(
                    vanillaLinearColor,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        true),
                    label + " linear UI-mask color vanilla model");
                passed &= compare(
                    disabledLinearColor,
                    vanillaLinearColor,
                    label + " linear UI-mask color disabled parity");
                passed &= compare(
                    enabledLinearColor,
                    expectedVanilla(
                        contract,
                        kPipboyControlsConverted,
                        true,
                        true),
                    label + " linear UI-mask color enabled model");
            }
            if (contract.depthTested()) {
                const auto vanillaDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTextureFail.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto replacementDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTextureFail.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                constexpr Pixel discarded{};
                passed &= compare(
                    vanillaDiscard,
                    discarded,
                    label + " vanilla depth discard");
                passed &= compare(
                    replacementDiscard,
                    vanillaDiscard,
                    label + " disabled depth discard parity");
            }
            if (contract.alphaMaskTested()) {
                const auto vanillaDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    alphaMaskTextureFail.Get(),
                    membraneNormalMapTexture.Get());
                const auto replacementDiscard = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), geometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    alphaMaskTextureFail.Get(),
                    membraneNormalMapTexture.Get());
                constexpr Pixel discarded{};
                passed &= compare(
                    vanillaDiscard,
                    discarded,
                    label + " vanilla alpha-mask discard");
                passed &= compare(
                    replacementDiscard,
                    vanillaDiscard,
                    label + " disabled alpha-mask discard parity");
            }
            if (contract.pipboy()) {
                const auto vanillaRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    vanillaShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto disabledRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    disabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                const auto enabledRawPipboy = render(
                    device.Get(), context.Get(), vertexShader,
                    replacementShader.Get(), techniqueBuffer.Get(),
                    materialBuffer.Get(), rawPipboyGeometryBuffer.Get(),
                    enabledFrameBuffer.Get(), texture.Get(),
                    depthTexture.Get(), depthTestTexturePass.Get(),
                    grayscaleTexture.Get(), pipboyTexture.Get(), sampler.Get(),
                    nullptr, normalTexture, environmentCube, environmentMask);
                passed &= compare(
                    vanillaRawPipboy,
                    expectedVanilla(
                        contract,
                        kPipboyControlsRaw,
                        false),
                    label + " raw Pipboy vanilla model");
                passed &= compare(
                    disabledRawPipboy,
                    vanillaRawPipboy,
                    label + " raw Pipboy disabled parity");
                passed &= compare(
                    enabledRawPipboy,
                    expectedVanilla(
                        contract,
                        kPipboyControlsRaw,
                        false),
                    label + " raw Pipboy enabled model");
            }
        }
        if (!passed) {
            return 1;
        }
        std::cout <<
            "All 631 Effect Linear Lighting parity and enabled model tests passed.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr <<
            "usage: EffectLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "Effect Linear Lighting parity test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
