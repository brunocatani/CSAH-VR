#ifndef SKY_TECHNIQUE
#error SKY_TECHNIQUE must identify the FO4VR Sky pixel-shader descriptor.
#endif

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer SkyPerGeometry : register(b2)
{
    float2 skyParameters;
    float2 skyPerGeometryPadding;
};

cbuffer SkyStereoData : register(b12)
{
    float4 skyStereoData[71];
};

Texture2D<float4> skyBaseTexture : register(t0);
Texture2D<float4> skyBlendTexture : register(t1);
Texture2D<float4> skyNoiseTexture : register(t2);

SamplerState skyBaseSampler : register(s0);
SamplerState skyBlendSampler : register(s1);
SamplerState skyNoiseSampler : register(s2);

struct SkyPixelInput
{
    float4 position : SV_POSITION;
#if SKY_TECHNIQUE != 0 && SKY_TECHNIQUE != 1
    float2 baseUv : TEXCOORD0;
#endif
#if SKY_TECHNIQUE == 6
    float2 blendUv : TEXCOORD1;
#endif
#if SKY_TECHNIQUE == 3
    float horizonFade : TEXCOORD2;
#endif
#if SKY_TECHNIQUE != 0
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
    nointerpolation uint eyeIndex : POSITION2;
#endif
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

struct SkyPixelOutput
{
    float4 color : SV_Target0;
#if SKY_TECHNIQUE != 0
    float4 motion : SV_Target1;
#endif
};

float3 ApplySkyScale(float3 color)
{
    if (skyParameters.y > 0.0f)
    {
        color *= skyParameters.y;
    }
    return color;
}

float4 ComputeMotionVector(SkyPixelInput input)
{
    const uint matrixBase = input.eyeIndex * 4u;
    const float currentW = dot(
        skyStereoData[matrixBase + 66u], input.currentPosition);
    const float2 currentNdc = float2(
        dot(skyStereoData[matrixBase + 63u], input.currentPosition),
        dot(skyStereoData[matrixBase + 64u], input.currentPosition)) /
        currentW;

    const float previousW = dot(
        skyStereoData[matrixBase + 54u], input.previousPosition);
    const float2 previousNdc = float2(
        dot(skyStereoData[matrixBase + 51u], input.previousPosition),
        dot(skyStereoData[matrixBase + 52u], input.previousPosition)) /
        previousW;
    return float4(
        (currentNdc - previousNdc) * float2(-0.5f, 0.5f),
        1.0f,
        1.0f);
}

SkyPixelOutput PSMain(SkyPixelInput input)
{
    SkyPixelOutput output;

#if SKY_TECHNIQUE == 0
    output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
#elif SKY_TECHNIQUE == 1
    float3 skyColor = ApplySkyScale(
        LinearLightingSkyProducerColor(input.color.xyz));
    const float2 noiseUv = input.position.xy * 0.125f;
    const float noise =
        skyNoiseTexture.Sample(skyNoiseSampler, noiseUv).x * 0.0078125f -
        0.001953125f;
    output.color = float4(skyColor + noise.xxx, 1.0f);
#elif SKY_TECHNIQUE == 2
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    output.color.xyz = LinearLightingSky(baseColor.xyz);
    output.color.w = pow(abs(baseColor.w), 2.2f);
    clip(output.color.w - (1.0f / 255.0f));
#elif SKY_TECHNIQUE == 3
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    const float3 composed = ApplySkyScale(
        LinearLightingSky(baseColor.xyz) *
        LinearLightingSkyProducerColor(input.color.xyz));
    output.color.xyz = composed * 1.5f;
    output.color.w = baseColor.w * input.color.w * input.horizonFade;
#elif SKY_TECHNIQUE == 4
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    output.color.xyz = ApplySkyScale(
        LinearLightingSky(baseColor.xyz) *
        LinearLightingSkyCloudColor(input.color.xyz));
    output.color.w = pow(abs(baseColor.w), 2.2f) * input.color.w;
#elif SKY_TECHNIQUE == 5
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    output.color.xyz = ApplySkyScale(
        LinearLightingSky(baseColor.xyz) *
        LinearLightingSkyCloudColor(input.color.xyz));
    output.color.w = baseColor.w * input.color.w;
#elif SKY_TECHNIQUE == 6
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    const float4 blendColor = skyBlendTexture.Sample(
        skyBlendSampler, input.blendUv);
    const float3 blendedSky = lerp(
        LinearLightingSky(baseColor.xyz),
        LinearLightingSky(blendColor.xyz),
        skyParameters.x);
    output.color.xyz = ApplySkyScale(
        blendedSky * LinearLightingSkyCloudColor(input.color.xyz));
    output.color.w =
        lerp(baseColor.w, blendColor.w, skyParameters.x) * input.color.w;
#elif SKY_TECHNIQUE == 7
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    output.color.xyz = ApplySkyScale(
        LinearLightingSky(baseColor.xyz) *
        LinearLightingSkyCloudColor(input.color.xyz));
    output.color.w = saturate(skyParameters.x - 0.4f) *
        baseColor.w * input.color.w * (1.0f / 0.6f);
#elif SKY_TECHNIQUE == 8
    const float4 baseColor = skyBaseTexture.Sample(
        skyBaseSampler, input.baseUv);
    float3 skyColor = ApplySkyScale(
        LinearLightingSky(baseColor.xyz) *
        LinearLightingSkyCloudColor(input.color.xyz));
    const float2 noiseUv = input.position.xy * 0.125f;
    const float noise =
        skyNoiseTexture.Sample(skyNoiseSampler, noiseUv).x * 0.0078125f -
        0.001953125f;
    output.color = float4(skyColor + noise.xxx, 1.0f);
#else
#error Unsupported SKY_TECHNIQUE value.
#endif

    output.motion = ComputeMotionVector(input);
    return output;
}
