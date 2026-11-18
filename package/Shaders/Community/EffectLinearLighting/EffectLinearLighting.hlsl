#include "../LinearLighting/LinearLighting.hlsli"

#ifndef EFFECT_TECHNIQUE
#error EFFECT_TECHNIQUE must identify a verified FO4VR Effect technique
#endif

struct EffectPixelInput
{
    float4 position : SV_POSITION0;
    float4 texCoord : TEXCOORD0;
#if (EFFECT_TECHNIQUE & 0x00100000) != 0 && \
    (EFFECT_TECHNIQUE & 0x00080000) == 0
    float4 pipboyTexCoord : TEXCOORD4;
#endif
#if (EFFECT_TECHNIQUE & 0x00000200) != 0
#if (EFFECT_TECHNIQUE & 0x00800000) != 0
    float4 membraneNormal : TEXCOORD4;
#else
    float4 membraneViewVector : TEXCOORD4;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
    float4 environmentViewVector : TEXCOORD4;
#endif
#if (EFFECT_TECHNIQUE & 0x03000000) != 0
    float4 depthTestData : TEXCOORD3;
#endif
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
    float3 environmentTangent0 : TEXCOORD7;
    float3 environmentTangent1 : TEXCOORD8;
    float3 environmentTangent2 : TEXCOORD9;
#endif
#if (EFFECT_TECHNIQUE & 0x1) != 0
    float4 vertexColor : COLOR0;
#endif
    float4 fogParam : COLOR1;
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float3 pipboyData : TEXCOORD1;
#endif
#if (EFFECT_TECHNIQUE & 0x00000200) != 0
#if (EFFECT_TECHNIQUE & 0x00800000) != 0
    float3 membraneViewVector : TEXCOORD1;
#elif (EFFECT_TECHNIQUE & 0x2) != 0
    float3 membraneTangent0 : TEXCOORD1;
    float3 membraneTangent1 : TEXCOORD2;
    float3 membraneTangent2 : TEXCOORD3;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00000400) != 0
    float3 modelPosition : TEXCOORD6;
#endif
#if (EFFECT_TECHNIQUE & 0x1080) != 0
    float3 particleData : TEXCOORD5;
#endif
    uint eyeIndex : EYEINDEX0;
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
};

cbuffer EffectPerTechnique : register(b0)
{
#if (EFFECT_TECHNIQUE & 0x08000000) != 0
    float4 EffectUIMaskTechniqueData[19] : packoffset(c0);
#else
    float4 EffectDepthParameters : packoffset(c0);
#endif
};
#if (EFFECT_TECHNIQUE & 0x08000000) != 0
#define EffectDepthParameters EffectUIMaskTechniqueData[0]
#endif

cbuffer EffectPerMaterial : register(b1)
{
    float4 EffectBaseColor : packoffset(c0);
    float4 EffectUnusedPerMaterial : packoffset(c1);
    float4 EffectLightingInfluence : packoffset(c2);
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
    float4 EffectEnvironmentMapScale : packoffset(c3);
#elif (EFFECT_TECHNIQUE & 0x00400000) != 0
    float4 EffectDistortionScale : packoffset(c3);
#elif (EFFECT_TECHNIQUE & 0x03000000) != 0
    float4 EffectUnusedPerMaterialDepthTest : packoffset(c3);
#endif
#if (EFFECT_TECHNIQUE & 0x03000000) != 0
    float4 EffectDepthTestParameters : packoffset(c4);
#endif
};

cbuffer EffectPerGeometry : register(b2)
{
#if (EFFECT_TECHNIQUE & 0x00000400) != 0
    float4 EffectPointLightPositionX[2] : packoffset(c0);
    float4 EffectPointLightPositionY[2] : packoffset(c2);
    float4 EffectPointLightPositionZ[2] : packoffset(c4);
    float4 EffectSpotLightDirectionX[2] : packoffset(c6);
    float4 EffectSpotLightDirectionY[2] : packoffset(c8);
    float4 EffectSpotLightDirectionZ[2] : packoffset(c10);
    float4 EffectUnusedLightingGeometry : packoffset(c12);
    float4 EffectSpotLightExponent : packoffset(c13);
    float4 EffectSpotLightCosHalfAngle : packoffset(c14);
    float4 EffectPointLightInverseRadius : packoffset(c15);
    float4 EffectPointLightColorR : packoffset(c16);
    float4 EffectPointLightColorG : packoffset(c17);
    float4 EffectPointLightColorB : packoffset(c18);
    float4 EffectDirectionalLightColor : packoffset(c19);
#elif (EFFECT_TECHNIQUE & 0x00100000) != 0
    float4 EffectUnusedPerGeometryBeforePipboy[12] : packoffset(c0);
    float4 EffectPipboyControls : packoffset(c12);
    float4 EffectUnusedPerGeometryAfterPipboy[7] : packoffset(c13);
#else
    float4 EffectUnusedPerGeometry[20] : packoffset(c0);
#endif
    float4 EffectPropertyColor : packoffset(c20);
    float4 EffectAlphaTest : packoffset(c21);
#if (EFFECT_TECHNIQUE & 0x00000200) != 0
    float4 EffectMembraneRimColor : packoffset(c22);
    float4 EffectMembraneVariables : packoffset(c23);
#endif
};

SamplerState EffectSampler : register(s0);
#if ((EFFECT_TECHNIQUE & 0x00000200) != 0 && \
     (EFFECT_TECHNIQUE & 0x00800000) == 0) || \
    (EFFECT_TECHNIQUE & 0x00080000) != 0
SamplerState EffectNormalSampler : register(s1);
#elif (EFFECT_TECHNIQUE & 0x00400000) != 0
SamplerState EffectDistortionSampler : register(s1);
#endif
#if (EFFECT_TECHNIQUE & 0x00020000) != 0
SamplerState EffectAlphaMaskSampler : register(s2);
#endif
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
SamplerState EffectGrayscaleSampler : register(s4);
#endif
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
SamplerState EffectPipboySampler : register(s6);
#endif
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
SamplerState EffectEnvironmentSampler : register(s5);
SamplerState EffectEnvironmentMaskSampler : register(s7);
#elif (EFFECT_TECHNIQUE & 0x00400000) != 0
SamplerState EffectDistortionMaskSampler : register(s7);
#endif
Texture2D<float4> EffectTexture : register(t0);
#if ((EFFECT_TECHNIQUE & 0x00000200) != 0 && \
     (EFFECT_TECHNIQUE & 0x00800000) == 0) || \
    (EFFECT_TECHNIQUE & 0x00080000) != 0
Texture2D<float4> EffectNormalTexture : register(t1);
#elif (EFFECT_TECHNIQUE & 0x00400000) != 0
Texture2D<float4> EffectDistortionTexture : register(t1);
#endif
#if (EFFECT_TECHNIQUE & 0x00020000) != 0
Texture2D<float4> EffectAlphaMaskTexture : register(t2);
#endif
Texture2D<float4> EffectDepthTexture : register(t3);
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
Texture2D<float4> EffectGrayscaleTexture : register(t4);
#endif
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
Texture2D<float4> EffectPipboyTexture : register(t6);
#endif
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
TextureCube<float4> EffectEnvironmentTexture : register(t5);
Texture2D<float4> EffectEnvironmentMaskTexture : register(t7);
#elif (EFFECT_TECHNIQUE & 0x00400000) != 0
Texture2D<float4> EffectDistortionMaskTexture : register(t7);
#endif
#if (EFFECT_TECHNIQUE & 0x03000000) != 0
Texture2D<float4> EffectDepthTestTexture : register(t8);
#endif

float4 LinearLightingEffectVertexColor(float4 color)
{
    if (enableLinearLighting != 0u) {
        return float4(
            LinearLightingEffect(color.xyz),
            exp2(log2(color.w) * 2.2f));
    }
    return exp2(log2(color) * 2.2f);
}

#if (EFFECT_TECHNIQUE & 0x00100000) != 0
float4 LinearLightingEffectPipboyColor(float4 color)
{
    if (enableLinearLighting != 0u) {
        return float4(
            LinearLightingEffect(color.xyz),
            exp2(log2(color.w) * 2.2f));
    }
    return exp2(log2(color) * 2.2f);
}
#endif

float EffectSoftParticleFade(float2 pixelPosition, float particleDepth)
{
    const float deviceDepth = 1.0f - EffectDepthTexture.Load(
        int3(int2(pixelPosition), 0)).x;
    const float sceneDepth = mad(
        deviceDepth,
        EffectDepthParameters.z,
        EffectDepthParameters.y);
    const float cameraFadeDepth = mad(
        EffectDepthParameters.y,
        EffectDepthParameters.z,
        EffectDepthParameters.y);
    const float2 scaledDepth = EffectLightingInfluence.yy /
        float2(sceneDepth, cameraFadeDepth);
    const float intersectionFade = saturate(
        scaledDepth.x - particleDepth);
    float cameraFade = saturate(particleDepth - scaledDepth.y);
    cameraFade = saturate((cameraFade - 0.075f) * 2.352941176470588f);
    cameraFade = cameraFade * cameraFade * (3.0f - 2.0f * cameraFade);
    return intersectionFade * cameraFade;
}

#if (EFFECT_TECHNIQUE & 0x00400000) != 0
void EffectDistortedTextureSamples(
    float4 texCoord,
    float particleScale,
    out float4 textureColor,
    out float4 undistortedTextureColor,
    out float4 distortionMaskColor)
{
    const float2 distortion =
        EffectDistortionTexture.Sample(
            EffectDistortionSampler,
            texCoord.zw).xy * 2.0f - 1.0f;
    const float2 offset =
        distortion * particleScale * EffectDistortionScale.x;
    undistortedTextureColor = EffectTexture.Sample(
        EffectSampler,
        texCoord.zw + offset);
    distortionMaskColor = EffectDistortionMaskTexture.Sample(
        EffectDistortionMaskSampler,
        texCoord.xy + offset);
    textureColor = undistortedTextureColor * distortionMaskColor;
}
#endif

#if (EFFECT_TECHNIQUE & 0x00000400) != 0
float4 EffectPointLightColorToLinear(float4 color)
{
    return enableLinearLighting != 0u ?
        color * pointLightMult * effectLightingMult :
        color;
}

float3 EffectLightingColor(float3 modelPosition, uint eyeIndex)
{
    const float4 deltaX =
        modelPosition.xxxx - EffectPointLightPositionX[eyeIndex];
    const float4 deltaY =
        modelPosition.yyyy - EffectPointLightPositionY[eyeIndex];
    const float4 deltaZ =
        modelPosition.zzzz - EffectPointLightPositionZ[eyeIndex];
    const float4 distance = sqrt(
        deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    float4 distanceFade = 1.0f.xxxx -
        saturate(distance * EffectPointLightInverseRadius) *
        saturate(distance * EffectPointLightInverseRadius);
    distanceFade = exp2(log2(distanceFade) * 2.2f);

    const float4 safeDistance = max(distance, 0.001f.xxxx);
    const float4 lightDirectionX = deltaX / safeDistance;
    const float4 lightDirectionY = deltaY / safeDistance;
    const float4 lightDirectionZ = deltaZ / safeDistance;
    const float4 spotCosine = saturate(
        lightDirectionX * EffectSpotLightDirectionX[eyeIndex] +
        lightDirectionY * EffectSpotLightDirectionY[eyeIndex] +
        lightDirectionZ * EffectSpotLightDirectionZ[eyeIndex]);
    const float4 coneFade = saturate(
        1.0f.xxxx -
        (1.0f.xxxx - spotCosine) /
            (1.0f.xxxx - EffectSpotLightCosHalfAngle));
    float4 spotFade = min(
        exp2(log2(coneFade) * EffectSpotLightExponent),
        1.0f.xxxx);
    spotFade = EffectSpotLightExponent != 0.0f.xxxx ?
        spotFade : 1.0f.xxxx;

    const float4 attenuation = distanceFade * spotFade;
    const float3 pointLighting = float3(
        dot(attenuation, EffectPointLightColorToLinear(
            EffectPointLightColorR)),
        dot(attenuation, EffectPointLightColorToLinear(
            EffectPointLightColorG)),
        dot(attenuation, EffectPointLightColorToLinear(
            EffectPointLightColorB)));
    const float3 directionalLighting = EffectDirectionalLightColor.xyz *
        (enableLinearLighting != 0u ? effectLightingMult : 1.0f);
    return directionalLighting + pointLighting;
}
#endif

float4 PSMain(EffectPixelInput input) : SV_Target0
{
#if (EFFECT_TECHNIQUE & 0x08000000) != 0
    const float sampledAlpha = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy).w;
    const float materialAlpha = sampledAlpha * EffectBaseColor.w;
    const float propertyAlpha = materialAlpha * EffectPropertyColor.w;
    if (propertyAlpha - EffectAlphaTest.x < 0.0f) {
        discard;
    }

    float alpha = propertyAlpha;
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float pipboyAlpha = EffectPipboyTexture.Sample(
        EffectPipboySampler,
        input.texCoord.xy).w;
    if (EffectPipboyControls.y == 0.0f) {
        pipboyAlpha = exp2(log2(pipboyAlpha) * 2.2f);
    }
    const bool usePipboyAlpha = EffectAlphaTest.w != 0.0f;
    alpha = usePipboyAlpha ? pipboyAlpha : alpha;
    if (usePipboyAlpha && EffectPipboyControls.x != 0.0f) {
        alpha *= EffectBaseColor.w;
    }
    alpha *= usePipboyAlpha ? EffectPipboyControls.z : 1.0f;
#endif

    [branch] if (EffectAlphaTest.y < 1.0f) {
        if (EffectAlphaTest.y - sampledAlpha < 0.0f) {
            discard;
        }
    }

    const int rectangleCount = (int)EffectUIMaskTechniqueData[1].x;
    float rectangleMask = 0.0f;
    [loop] for (int rectangleIndex = 0;
                rectangleIndex < rectangleCount;
                ++rectangleIndex) {
        const float4 rectangle =
            EffectUIMaskTechniqueData[rectangleIndex + 2];
        const bool multiplyVerticalRamp = rectangle.x < 0.0f;
        const float2 upperDistance = float2(
            abs(rectangle.x) - input.texCoord.x,
            rectangle.y - input.texCoord.y);
        const float2 lowerDistance =
            input.texCoord.xy - rectangle.zw;
        const float distance = max(
            max(upperDistance.x, lowerDistance.x),
            max(upperDistance.y, lowerDistance.y));
        rectangleMask = max(
            rectangleMask,
            1.0f - EffectUIMaskTechniqueData[1].y * distance);

        if (multiplyVerticalRamp &&
            input.texCoord.y >= rectangle.y - 0.0025f &&
            input.texCoord.y <= rectangle.w + 0.0025f) {
            alpha *= (input.texCoord.y - rectangle.y) /
                (rectangle.w - rectangle.y);
        }
    }

    float3 outputColor = EffectUIMaskTechniqueData[1].z != 0.0f ?
        EffectUIMaskTechniqueData[18].xyz :
        (enableLinearLighting != 0u ?
            LinearLightingEffect(EffectUIMaskTechniqueData[18].xyz) :
            exp2(log2(EffectUIMaskTechniqueData[18].xyz) * 2.2f));
    if (enableLinearLighting != 0u) {
        outputColor *= otherEffectMult;
    }
    const float outputAlpha = LinearLightingEffectAlpha(
        alpha *
        saturate(rectangleMask) *
        EffectUIMaskTechniqueData[1].w);
#if (EFFECT_TECHNIQUE & 0x40000000) != 0
    outputColor *= outputAlpha;
#endif
    return float4(outputColor, outputAlpha);
#else
#if (EFFECT_TECHNIQUE & 0x03000000) != 0
    const int2 depthTestCoordinate = int2(
        (input.depthTestData.xy + 1.0f) *
        EffectDepthTestParameters.x * 0.5f);
    const float depthTestSample = EffectDepthTestTexture.Load(
        int3(depthTestCoordinate, 0)).x;
    if (depthTestSample - input.depthTestData.z < 0.0f) {
        discard;
    }
#endif
#if (EFFECT_TECHNIQUE & 0x00000200) != 0
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
#if (EFFECT_TECHNIQUE & 0x00800000) != 0
    const float3 membraneNormal = input.membraneNormal.xyz;
    const float membraneGrayscaleScale = input.membraneNormal.w;
#else
    float3 membraneNormal = EffectNormalTexture.Sample(
        EffectNormalSampler,
        input.texCoord.zw).xzy * 2.0f - 1.0f;
#if (EFFECT_TECHNIQUE & 0x2) != 0
    membraneNormal = mul(
        membraneNormal,
        transpose(float3x3(
            input.membraneTangent0,
            input.membraneTangent1,
            input.membraneTangent2)));
#endif
    const float membraneGrayscaleScale = input.membraneViewVector.w;
#endif
#if (EFFECT_TECHNIQUE & 0x00020000) != 0
    const float alphaMask = EffectAlphaMaskTexture.Sample(
        EffectAlphaMaskSampler,
        input.texCoord.zw).w;
    if (alphaMask - EffectAlphaTest.x < 0.0f) {
        discard;
    }
#endif
    float4 baseColor = textureColor;
#if (EFFECT_TECHNIQUE & 0x00008000) != 0
    baseColor.w = 1.0f;
#endif
    baseColor.xyz = LinearLightingEffectTexture(baseColor.xyz);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    const float4 vertexColor =
        LinearLightingEffectVertexColor(input.vertexColor);
    baseColor *= vertexColor;
#endif
    baseColor.xyz *= LinearLightingEffectMembraneColor(
        EffectPropertyColor.xyz);
    baseColor.w *= EffectPropertyColor.w;

#if (EFFECT_TECHNIQUE & 0x00002000) != 0
    float grayscaleColorY =
        LinearLightingEffectMembraneCoordinate(EffectPropertyColor.x) *
        membraneGrayscaleScale;
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleColorY *= input.vertexColor.x;
#endif
    const float3 grayscaleColor = EffectGrayscaleTexture.Sample(
        EffectGrayscaleSampler,
        float2(
            pow(abs(textureColor.y), 1.0f / 2.2f),
            grayscaleColorY)).xyz;
    baseColor.xyz = LinearLightingEffectTexture(
        grayscaleColor * EffectMembraneVariables.z);
#endif

#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    float grayscaleAlphaY =
        pow(abs(EffectPropertyColor.w), 1.0f / 2.2f) *
        membraneGrayscaleScale;
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleAlphaY *= input.vertexColor.w;
#endif
    baseColor.w = EffectGrayscaleTexture.Sample(
        EffectGrayscaleSampler,
        float2(textureColor.w, grayscaleAlphaY)).w;
#endif

    const float membraneFactor = pow(
        saturate(
            1.0f - dot(
                input.membraneViewVector.xyz,
                membraneNormal)),
        EffectMembraneVariables.x);
    const float4 membraneColor = float4(
        LinearLightingEffectMembraneColor(EffectMembraneRimColor.xyz),
        EffectMembraneRimColor.w) * membraneFactor;
    baseColor.xyz += membraneColor.xyz * membraneColor.w;
    baseColor.w += membraneColor.w;
    if (enableLinearLighting != 0u) {
        baseColor.xyz *= membraneEffectMult;
    }

    const float fogFactor = LinearLightingFogAlpha(input.fogParam.w);
#if (EFFECT_TECHNIQUE & 0x20) != 0
    const float3 blendedColor = baseColor.xyz * (1.0f - fogFactor);
#else
    const float3 blendedColor = lerp(
        baseColor.xyz,
        LinearLightingFog(input.fogParam.xyz),
        fogFactor);
#endif
    [branch] if (EffectAlphaTest.y < 1.0f) {
        if (EffectAlphaTest.y - textureColor.w < 0.0f) {
            discard;
        }
    }
    return float4(
        blendedColor,
        LinearLightingEffectAlpha(baseColor.w));
#else
#if (EFFECT_TECHNIQUE & 0x00006000) != 0
    float4 baseColor = float4(
        LinearLightingEffectMaterialColor(EffectBaseColor.xyz),
        EffectBaseColor.w);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    baseColor.xyz *= LinearLightingEffectVertexColor(input.vertexColor).xyz;
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    baseColor.w *= input.vertexColor.w;
#else
    baseColor.w *= LinearLightingEffectVertexColor(input.vertexColor).w;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00006004) != 0
#if (EFFECT_TECHNIQUE & 0x00400000) != 0
    float4 textureColor;
    float4 undistortedTextureColor;
    float4 distortionMaskColor;
    EffectDistortedTextureSamples(
        input.texCoord,
        input.particleData.y,
        textureColor,
        undistortedTextureColor,
        distortionMaskColor);
#else
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
#endif
#if (EFFECT_TECHNIQUE & 0x00002004) == 0x4
    baseColor.xyz *= LinearLightingEffectTexture(textureColor.xyz);
#endif
#if (EFFECT_TECHNIQUE & 0x00004004) == 0x4
    baseColor.w *= textureColor.w;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    const float softParticleFade = EffectSoftParticleFade(
        input.position.xy,
        input.particleData.z);
#if (EFFECT_TECHNIQUE & 0x00004000) == 0
    baseColor.w *= softParticleFade;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00002000) != 0
#if (EFFECT_TECHNIQUE & 0x00400000) != 0
    float grayscaleColorY =
        pow(abs(EffectBaseColor.x), 1.0f / 2.2f);
#else
    float grayscaleColorY =
        pow(abs(EffectBaseColor.x), 1.0f / 2.2f) * input.texCoord.z;
#endif
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleColorY *= input.vertexColor.x;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    grayscaleColorY *= softParticleFade;
#endif
    const float2 grayscaleColorCoordinate = float2(
#if (EFFECT_TECHNIQUE & 0x00400000) != 0
        pow(abs(undistortedTextureColor.y), 1.0f / 2.2f) *
            distortionMaskColor.y,
#else
        pow(abs(textureColor.y), 1.0f / 2.2f),
#endif
        grayscaleColorY);
    const float3 grayscaleColor = EffectUnusedPerMaterial.x *
        EffectGrayscaleTexture.Sample(
            EffectGrayscaleSampler,
            grayscaleColorCoordinate).xyz;
    baseColor.xyz = LinearLightingEffectTexture(grayscaleColor);
#endif
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
#if (EFFECT_TECHNIQUE & 0x00400000) != 0
    float grayscaleAlphaY =
        pow(abs(EffectBaseColor.w), 1.0f / 2.2f) *
        pow(abs(EffectPropertyColor.w), 1.0f / 2.2f);
#else
    float grayscaleAlphaY =
        pow(abs(EffectBaseColor.w), 1.0f / 2.2f) *
        input.texCoord.z *
        pow(abs(EffectPropertyColor.w), 1.0f / 2.2f);
#endif
#if (EFFECT_TECHNIQUE & 0x1) != 0
    grayscaleAlphaY *= input.vertexColor.w;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
#if (EFFECT_TECHNIQUE & 0x00400000) == 0
    grayscaleAlphaY *= softParticleFade;
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00401000) == 0x00401000
    float grayscaleAlpha = EffectGrayscaleTexture.Sample(
        EffectGrayscaleSampler,
        float2(textureColor.w, grayscaleAlphaY)).w;
    grayscaleAlpha *= softParticleFade;
#else
    const float grayscaleAlpha = EffectGrayscaleTexture.Sample(
        EffectGrayscaleSampler,
        float2(textureColor.w, grayscaleAlphaY)).w;
#endif
#endif
#else
    float4 baseColor = EffectBaseColor;
    baseColor.xyz = LinearLightingEffectMaterialColor(baseColor.xyz);
#if (EFFECT_TECHNIQUE & 0x1) != 0
    baseColor *= LinearLightingEffectVertexColor(input.vertexColor);
#endif
#if (EFFECT_TECHNIQUE & 0x4) != 0
#if (EFFECT_TECHNIQUE & 0x00400000) != 0
    float4 textureColor;
    float4 undistortedTextureColor;
    float4 distortionMaskColor;
    EffectDistortedTextureSamples(
        input.texCoord,
        input.particleData.y,
        textureColor,
        undistortedTextureColor,
        distortionMaskColor);
#else
    const float4 textureColor = EffectTexture.Sample(
        EffectSampler,
        input.texCoord.xy);
#endif
    baseColor.xyz *= LinearLightingEffectTexture(textureColor.xyz);
    baseColor.w *= textureColor.w;
#endif
#if (EFFECT_TECHNIQUE & 0x1000) != 0
    baseColor.w *= EffectSoftParticleFade(
        input.position.xy,
        input.particleData.z);
#endif
#endif
#if (EFFECT_TECHNIQUE & 0x00000010) != 0 && \
    (EFFECT_TECHNIQUE & 0x00004000) == 0
    baseColor.w *= input.texCoord.z;
#endif
#if (EFFECT_TECHNIQUE & 0x00200000) != 0 && \
    (EFFECT_TECHNIQUE & 0x00002000) == 0
    baseColor.xyz *= input.texCoord.z;
#endif
#if (EFFECT_TECHNIQUE & 0x00080000) != 0
    const float4 environmentNormalSample = EffectNormalTexture.Sample(
        EffectNormalSampler,
        input.texCoord.xy);
    const float2 environmentNormalXY =
        environmentNormalSample.xy * 2.0f - 1.0f;
    const float environmentNormalZ = sqrt(
        1.0f - min(dot(environmentNormalXY, environmentNormalXY), 1.0f));
    float3 environmentNormal =
        environmentNormalXY.x * input.environmentTangent0 +
        environmentNormalXY.y * input.environmentTangent1 +
        environmentNormalZ * input.environmentTangent2;
    environmentNormal = normalize(environmentNormal);
    const float3 environmentReflectionVector = reflect(
        input.environmentViewVector.xyz,
        environmentNormal);
    const float3 environmentColor = LinearLightingEffectTexture(
        EffectEnvironmentTexture.Sample(
            EffectEnvironmentSampler,
            environmentReflectionVector).xyz);
    const float environmentMask = EffectEnvironmentMaskTexture.Sample(
        EffectEnvironmentMaskSampler,
        input.texCoord.xy).x;
    baseColor.xyz += environmentColor *
        EffectEnvironmentMapScale.x *
        environmentNormalSample.w *
        environmentMask;
#endif
#if (EFFECT_TECHNIQUE & 0x00000400) != 0
    const float3 propertyColor = EffectLightingColor(
        input.modelPosition,
        input.eyeIndex);
#else
    const float3 propertyColor =
        LinearLightingEffectGeometryColor(EffectPropertyColor.xyz);
#endif
    float3 lightColor = lerp(
        baseColor.xyz,
        propertyColor * baseColor.xyz,
        EffectLightingInfluence.x);
    if (enableLinearLighting != 0u) {
        lightColor *= otherEffectMult;
    }
    const float fogFactor = LinearLightingFogAlpha(input.fogParam.w);
#if (EFFECT_TECHNIQUE & 0x20) != 0
    float3 blendedColor = lightColor * (1.0f - fogFactor);
#elif (EFFECT_TECHNIQUE & 0x40) != 0
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    const float alpha = grayscaleAlpha;
#else
    const float alpha = baseColor.w * EffectPropertyColor.w;
#endif
    const float outputAlpha = LinearLightingEffectAlpha(alpha);
    const float3 foggedMultiplyColor = lerp(
        lightColor,
        1.0f.xxx,
        saturate(1.5f * fogFactor));
    float3 blendedColor = lerp(
        1.0f.xxx,
        foggedMultiplyColor,
        outputAlpha);
#else
    const float3 fogColor = LinearLightingFog(input.fogParam.xyz);
    float3 blendedColor = lerp(lightColor, fogColor, fogFactor);
#endif
#if (EFFECT_TECHNIQUE & 0x40) == 0 || (EFFECT_TECHNIQUE & 0x20) != 0
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    float alpha = grayscaleAlpha;
#else
    float alpha = baseColor.w * EffectPropertyColor.w;
#endif
#else
#if (EFFECT_TECHNIQUE & 0x00004000) != 0
    const float alpha = grayscaleAlpha;
#else
    const float alpha = baseColor.w * EffectPropertyColor.w;
#endif
#endif
#endif
    if (alpha - EffectAlphaTest.x < 0.0f) {
        discard;
    }
#if (EFFECT_TECHNIQUE & 0x00100000) != 0
    float4 pipboyColor = EffectPipboyTexture.Sample(
        EffectPipboySampler,
        input.texCoord.xy);
    if (EffectPipboyControls.y == 0.0f) {
        pipboyColor = LinearLightingEffectPipboyColor(pipboyColor);
    }
    blendedColor += pipboyColor.xyz * EffectPipboyControls.w *
        (enableLinearLighting != 0u ? otherEffectMult : 1.0f);
    const bool usePipboyAlpha = EffectAlphaTest.w != 0.0f;
    alpha = usePipboyAlpha ? pipboyColor.w : alpha;
    if (usePipboyAlpha && EffectPipboyControls.x != 0.0f) {
        blendedColor *= EffectBaseColor.w;
        alpha *= EffectBaseColor.w;
    }
    alpha *= usePipboyAlpha ? EffectPipboyControls.z : 1.0f;
#endif
    [branch] if (EffectAlphaTest.y < 1.0f) {
#if (EFFECT_TECHNIQUE & 0x00006004) != 0
        const float sampledAlpha = textureColor.w;
#else
        const float sampledAlpha = EffectTexture.Sample(
            EffectSampler,
            input.texCoord.xy).w;
#endif
        if (EffectAlphaTest.y - sampledAlpha < 0.0f) {
            discard;
        }
    }

#if (EFFECT_TECHNIQUE & 0x40) == 0 || (EFFECT_TECHNIQUE & 0x20) != 0
    const float outputAlpha = LinearLightingEffectAlpha(alpha);
#endif
#if (EFFECT_TECHNIQUE & 0x40000000) != 0
    blendedColor *= outputAlpha;
#endif
    return float4(blendedColor, outputAlpha);
#endif
#endif
}
