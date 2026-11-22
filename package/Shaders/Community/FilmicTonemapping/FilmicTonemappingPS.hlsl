struct PixelInput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

#ifndef FO4VR_FILMIC_FADE
#define FO4VR_FILMIC_FADE 0
#endif

SamplerState BloomSampler : register(s0);
SamplerState SceneSampler : register(s1);
SamplerState AdaptedLuminanceSampler : register(s2);
SamplerState BypassMaskSampler : register(s3);
SamplerState BloomGlareSampler : register(s4);
SamplerState GradeTableSampler : register(s5);

Texture2D<float4> BloomTexture : register(t0);
Texture2D<float4> SceneTexture : register(t1);
Texture2D<float4> AdaptedLuminanceTexture : register(t2);
Texture2D<float4> BypassMaskTexture : register(t3);
Texture2D<float4> EnhancedBloomTexture : register(t4);
Texture2D<float4> PhysicalGlareTexture : register(t5);
Texture3D<float4> GradeTableFirst : register(t6);
Texture3D<float4> GradeTableSecond : register(t7);

cbuffer NativeHdrBlend : register(b2)
{
    float4 NativeFlags : packoffset(c0);
    float4 NativeExposure : packoffset(c1);
    float4 NativeCinematic : packoffset(c2);
    float4 NativeTint : packoffset(c3);
    float4 NativeBloomUv : packoffset(c4);
#if FO4VR_FILMIC_FADE
    float4 NativeFade : packoffset(c5);
#endif
};

cbuffer FilmicSettings : register(b12)
{
    // x = exp2(exposure EV), y = native adaptation weight,
    // z = hue-preserving filmic strength, w = 5.6 white-point scale.
    float4 FilmicParameters;
    // Colour grading.
    // x > 0.5 = grade this frame, y = colour table strength,
    // z = weight of the second table, w = gamma.
    float4 GradeControl;
    // Texture coordinate scale and offset: xy first table, zw second table.
    float4 GradeTables;
    // x = black point, y = 1 / (white point - black point),
    // z = brightness, w = contrast.
    float4 GradeLevels;
    // x = saturation, y = shadow desaturation.
    float4 GradeColor;
    float4 GradeShadowTint;
    float4 GradeHighlightTint;
};

cbuffer BloomGlareSettings : register(b13)
{
    // x=enhanced bloom ready, y=physical glare ready.
    float4 BloomGlareComposite;
};

static const float3 LuminanceWeights =
    float3(0.2125, 0.7154, 0.0721);
static const float NativeWhitePoint = 5.6;

float3 NativeHableUnnormalized(float3 color, float curveE)
{
    const float3 doubled = color * 2.0;
    const float3 numerator =
        doubled * (0.3 * color + 0.05) + 0.2 * curveE;
    const float3 denominator =
        doubled * (0.3 * color + 0.5) + 0.06;
    return numerator / denominator - curveE / 0.3;
}

float3 TonemapFilmic(float3 hdrColor, float curveE)
{
    const float whitePoint =
        NativeWhitePoint * clamp(FilmicParameters.w, 0.5, 2.0);
    const float whiteResponse = max(
        NativeHableUnnormalized(whitePoint.xxx, curveE).x,
        1.0e-5);
    const float3 nativeMapped =
        NativeHableUnnormalized(hdrColor, curveE) / whiteResponse;

    const float luminance = max(dot(hdrColor, LuminanceWeights), 0.0);
    const float mappedLuminance =
        NativeHableUnnormalized(luminance.xxx, curveE).x / whiteResponse;
    const float3 huePreserving = luminance > 1.0e-5 ?
        hdrColor * (mappedLuminance / luminance) : 0.0;
    return lerp(
        nativeMapped,
        huePreserving,
        saturate(FilmicParameters.z));
}

// The native output is linear and the game's own colour pass encodes it
// afterwards. Colour tables and grading controls are authored on the encoded
// picture, so the grade encodes, grades and decodes again; the game's colour
// pass then runs on it as before.
float3 GradeColorOutput(float3 linearColor)
{
    if (GradeControl.x < 0.5) {
        return linearColor;
    }
    float3 color = pow(saturate(linearColor), 1.0 / 2.2);
    if (GradeControl.y > 0.0) {
        const float3 first = GradeTableFirst.SampleLevel(
            GradeTableSampler,
            color * GradeTables.x + GradeTables.y,
            0.0).xyz;
        const float3 second = GradeTableSecond.SampleLevel(
            GradeTableSampler,
            color * GradeTables.z + GradeTables.w,
            0.0).xyz;
        color = lerp(color, lerp(first, second, GradeControl.z), GradeControl.y);
    }

    color = saturate((color - GradeLevels.x) * GradeLevels.y);
    color = pow(color, GradeControl.w);
    color *= GradeLevels.z;
    color = (color - 0.5) * GradeLevels.w + 0.5;

    float luminance = dot(color, LuminanceWeights);
    color = lerp(luminance.xxx, color, GradeColor.x);
    luminance = dot(color, LuminanceWeights);
    float shadow = saturate(1.0 - luminance);
    shadow *= shadow;
    color = lerp(color, luminance.xxx, GradeColor.y * shadow * shadow);

    // Shadows lift toward their tint, so white stays white; highlights scale
    // by theirs, so black stays black.
    color = saturate(color);
    luminance = dot(color, LuminanceWeights);
    const float dark = (1.0 - luminance) * (1.0 - luminance);
    color += GradeShadowTint.xyz * dark * (1.0 - color);
    color += GradeHighlightTint.xyz * luminance * luminance * color;
    return pow(saturate(color), 2.2);
}

float4 PSMain(PixelInput input) : SV_Target0
{
    const float2 uv = input.texCoord;
    const float3 scene = SceneTexture.Sample(SceneSampler, uv).xyz;
    const float bypassMask =
        BypassMaskTexture.Sample(BypassMaskSampler, uv).x;
    const bool bypass = abs(bypassMask * 255.0 - 4.0) < 0.25;
    const float2 bloomUv = uv * NativeBloomUv.zw;
    float3 bloom = BloomTexture.Sample(BloomSampler, bloomUv).xyz;
    if (BloomGlareComposite.x > 0.5) {
        bloom += EnhancedBloomTexture.SampleLevel(
            BloomGlareSampler, uv, 0.0).xyz;
    }
    if (BloomGlareComposite.y > 0.5) {
        bloom += PhysicalGlareTexture.SampleLevel(
            BloomGlareSampler, uv, 0.0).xyz;
    }
    if (bypass) {
        return float4(scene, 1.0);
    }

    const float adaptedLuminance = AdaptedLuminanceTexture.Sample(
        AdaptedLuminanceSampler, uv).x;
    const float nativeExposure = clamp(
        NativeExposure.z / (adaptedLuminance + 0.001),
        NativeExposure.y,
        NativeExposure.x);
    const float exposure = lerp(
        1.0,
        nativeExposure,
        saturate(FilmicParameters.y)) * FilmicParameters.x;
    const float3 hdrColor = max((scene + bloom) * exposure, 0.0);
    const float3 mapped = TonemapFilmic(hdrColor, NativeExposure.w);

    const float luminance = dot(mapped, LuminanceWeights);
    float4 graded = float4(mapped, 0.0);
    graded = lerp(luminance.xxxx, graded, NativeCinematic.x);
    graded = lerp(
        graded,
        luminance * NativeTint,
        NativeTint.w);
    graded *= NativeCinematic.w;
    graded = lerp(
        adaptedLuminance.xxxx,
        graded,
        NativeCinematic.z);
#if FO4VR_FILMIC_FADE
    graded = lerp(graded, NativeFade, NativeFade.w);
#endif
    graded.xyz = GradeColorOutput(graded.xyz);
    return graded;
}
