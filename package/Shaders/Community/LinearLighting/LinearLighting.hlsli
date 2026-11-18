#ifndef FO4VR_COMMUNITY_SHADERS_LINEAR_LIGHTING_HLSLI
#define FO4VR_COMMUNITY_SHADERS_LINEAR_LIGHTING_HLSLI
#include "../TerrainVariation/TerrainVariation.hlsli"
#include "../LODBlending/LODBlending.hlsli"

// These registers are only valid for replacement shaders whose original
// reflection contract proves that b5 and b8 are unused.
cbuffer LinearLightingFrame : register(b5)
{
    uint enableLinearLighting;
    uint isDirLightLinear;
    float dirLightMult;
    float lightGamma;

    float colorGamma;
    float emitColorGamma;
    float glowmapGamma;
    float ambientGamma;

    float fogGamma;
    float fogAlphaGamma;
    float effectGamma;
    float effectAlphaGamma;

    float skyGamma;
    float waterGamma;
    float vlGamma;
    float vanillaDiffuseColorMult;

    float directionalLightMult;
    float pointLightMult;
    float ambientMult;
    float emitColorMult;

    float glowmapMult;
    float effectLightingMult;
    float membraneEffectMult;
    float bloodEffectMult;

    float projectedEffectMult;
    float deferredEffectMult;
    float otherEffectMult;
    // ABI-compatible bit pattern of the exponent currently used by the
    // verified native Effect light/property producer.
    uint linearLightingPad0;

#if LINEAR_LIGHTING_COMPLEX_PARALLAX
    // Appended ABI region used only by the three exact landscape
    // complex-parallax replacements. Existing Linear Lighting bytecode keeps
    // consuming the original seven b5 registers unchanged.
    uint enableComplexParallax;
    float parallaxDepth;
    float parallaxMinimumSteps;
    float parallaxMaximumSteps;

    float parallaxGrazingClamp;
    float parallaxFadeStart;
    float parallaxFadeEnd;
    float parallaxPad0;
#endif
};

cbuffer LinearLightingGeometry : register(b8)
{
    float emissiveMult;
    float3 linearLightingGeometryPad0;
};

// FO4VR uses this fixed exponent in several CPU-side shader-constant
// producers. Producer-space helpers apply only the remaining exponent.
static const float kLinearLightingVanillaProducerGamma = 2.2f;

// FO4VR already lights in linear space: colour textures are bound through
// sRGB views, and encoded constants are decoded with 2.2 by the native
// shader or its CPU producer. Every response setting is therefore relative
// to the native decode: 2.2 reproduces the game exactly, a lower value
// brightens dark values and a higher one darkens them. Helpers that receive
// a value the game already uses as linear apply response / 2.2; sites that
// replace a native pow(x, 2.2) apply the response itself.
float3 LinearLightingResponse(float3 linearColor, float response)
{
    return pow(abs(linearColor), response / kLinearLightingVanillaProducerGamma);
}

float LinearLightingResponse(float linearValue, float response)
{
    return pow(abs(linearValue), response / kLinearLightingVanillaProducerGamma);
}

float LinearLightingSkyrimGammaToLinear(float color)
{
    return pow(abs(color), 1.6f);
}

float3 LinearLightingSkyrimGammaToLinear(float3 color)
{
    return pow(abs(color), 1.6f);
}

float LinearLightingLinearToSkyrimGamma(float color)
{
    return pow(abs(color), 1.0f / 1.6f);
}

float3 LinearLightingLinearToSkyrimGamma(float3 color)
{
    return pow(abs(color), 1.0f / 1.6f);
}

float3 LinearLightingDiffuse(float3 color)
{
    return enableLinearLighting != 0u ?
        LinearLightingResponse(color, colorGamma) * vanillaDiffuseColorMult : color;
}

float3 LinearLightingDecodedDiffuse(float3 color)
{
    return enableLinearLighting != 0u ?
        color * vanillaDiffuseColorMult : color;
}

float3 LinearLightingLight(float3 color, bool isLinear)
{
    return enableLinearLighting != 0u && !isLinear ?
        LinearLightingResponse(color, lightGamma) : color;
}

float3 LinearLightingDirectionalLight(float3 color, bool isLinear)
{
    return LinearLightingLight(color, isLinear) *
        ((enableLinearLighting != 0u && !isLinear) ?
            directionalLightMult : 1.0f);
}

float3 LinearLightingPointLight(float3 color, bool isLinear)
{
    return LinearLightingLight(color, isLinear) *
        ((enableLinearLighting != 0u && !isLinear) ?
            pointLightMult : 1.0f);
}

float3 LinearLightingEmitColor(float3 color)
{
    if (enableLinearLighting == 0u) {
        return color;
    }

    const float safeEmissiveMult = max(emissiveMult, 1e-5f);
    return LinearLightingResponse(color / safeEmissiveMult, emitColorGamma) *
        emissiveMult * emitColorMult;
}

float3 LinearLightingGlowmap(float3 color)
{
    return enableLinearLighting != 0u ?
        LinearLightingResponse(color, glowmapGamma) * glowmapMult : color;
}

float3 LinearLightingAmbient(float3 color)
{
    return enableLinearLighting != 0u ?
        LinearLightingResponse(color, ambientGamma) * ambientMult : color;
}

float3 LinearLightingFog(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(
            abs(color),
            fogGamma / kLinearLightingVanillaProducerGamma) :
        color;
}

float LinearLightingFogAlpha(float alpha)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(alpha, fogAlphaGamma) : alpha;
}

// Effect colour the native shader decodes itself with pow(x, 2.2): vertex
// colours and UI mask constants.
float3 LinearLightingEffect(float3 color)
{
    return enableLinearLighting != 0u ? pow(abs(color), effectGamma) : color;
}

// Effect texture samples, already linear through their sRGB views.
float3 LinearLightingEffectTexture(float3 color)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(color, effectGamma) : color;
}

// Effect material RGB is uploaded after a fixed 2.2 producer decode.
float3 LinearLightingEffectMaterialColor(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(
            abs(color),
            effectGamma / kLinearLightingVanillaProducerGamma) :
        color;
}

// Effect per-geometry RGB shares the runtime-controlled light producer
// exponent. Convert from that producer space into the configured Effect
// space without decoding it a second time.
float3 LinearLightingEffectGeometryColor(float3 color)
{
    const float producerGamma = max(asfloat(linearLightingPad0), 1e-5f);
    return enableLinearLighting != 0u ?
        pow(abs(color), effectGamma / producerGamma) :
        color;
}

// FO4VR's membrane helper independently applies the native fixed 2.2 decode
// to both its fill and rim RGB constants. Unlike the general per-geometry
// Effect producer, that helper is not controlled by the runtime light
// producer exponent.
float3 LinearLightingEffectMembraneColor(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(
            abs(color),
            effectGamma / kLinearLightingVanillaProducerGamma) :
        color;
}

// Membrane grayscale coordinates originate in the same fixed-2.2 helper as
// the membrane fill colour, so recover the authored coordinate from that
// producer regardless of the general Effect producer's live exponent.
float LinearLightingEffectMembraneCoordinate(float color)
{
    return pow(
        abs(color),
        1.0f / kLinearLightingVanillaProducerGamma);
}

float LinearLightingEffectAlpha(float alpha)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(alpha, effectAlphaGamma) : alpha;
}

float3 LinearLightingSky(float3 color)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(color, skyGamma) : color;
}

float3 LinearLightingSkyProducerColor(float3 color)
{
    return enableLinearLighting != 0u ?
        pow(
            abs(color),
            skyGamma / kLinearLightingVanillaProducerGamma) :
        color;
}

// Cloud techniques multiply their texture by the weather tint as the game
// uses it. Apply the Sky response to its luminance while retaining the
// channel ratio: a per-channel power curve turns pale sunset tints crimson by
// suppressing their weaker green and blue channels disproportionately.
float3 LinearLightingSkyCloudColor(float3 color)
{
    if (enableLinearLighting == 0u) {
        return color;
    }

    const float3 positiveColor = abs(color);
    const float luminance = dot(
        positiveColor,
        float3(0.2126f, 0.7152f, 0.0722f));
    const float linearLuminance = LinearLightingResponse(luminance, skyGamma);
    return positiveColor * (linearLuminance / max(luminance, 1e-5f));
}

float3 LinearLightingWater(float3 color)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(color, waterGamma) : color;
}

float3 LinearLightingVolumetricLighting(float3 color)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(color, vlGamma) : color;
}

float3 LinearLightingColorToLinear(float3 color)
{
    return enableLinearLighting != 0u ? LinearLightingResponse(color, colorGamma) : color;
}

float3 LinearLightingRadianceToLinear(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float LinearLightingIrradianceToLinear(float color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float3 LinearLightingIrradianceToLinear(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingSkyrimGammaToLinear(color);
}

float LinearLightingIrradianceToGamma(float color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingLinearToSkyrimGamma(color);
}

float3 LinearLightingIrradianceToGamma(float3 color)
{
    return enableLinearLighting != 0u ? color :
        LinearLightingLinearToSkyrimGamma(color);
}

#endif
