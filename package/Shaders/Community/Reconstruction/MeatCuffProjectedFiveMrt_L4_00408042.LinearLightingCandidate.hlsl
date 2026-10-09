#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_MEAT_CUFF
#define LINEAR_LIGHTING_MEAT_CUFF 1
#endif

#ifndef LINEAR_LIGHTING_SKIN_TINT
#define LINEAR_LIGHTING_SKIN_TINT 0
#endif

#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
#endif

#ifndef LINEAR_LIGHTING_TEXTURED_EMISSION
#define LINEAR_LIGHTING_TEXTURED_EMISSION 0
#endif

#ifndef LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_GRADIENT_REMAP 0
#endif

#if LINEAR_LIGHTING_SKIN_TINT && LINEAR_LIGHTING_GRADIENT_REMAP
#error Combined meat-cuff skin tint and gradient remap are not verified.
#endif

cbuffer PerMaterial : register(b2)
{
#if (LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP) && \
    LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[11];
#elif LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP || \
    LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[10];
#else
    float4 cb2[9];
#endif
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[51];
};

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);
#if LINEAR_LIGHTING_TEXTURED_EMISSION
Texture2D<float4> TexGlow : register(t3);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
Texture2D<float4> TexGradientRemap : register(t5);
#endif
Texture2D<float4> TexMeatCuffDiffuse : register(t9);
Texture2D<float4> TexMeatCuffNormal : register(t10);
Texture2D<float4> TexMeatCuffSpecular : register(t11);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
Texture2D<float4> TexAdditionalAlpha : register(t12);
Texture2D<float4> TexAdditionalAlphaNoise : register(t15);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
#if LINEAR_LIGHTING_TEXTURED_EMISSION
SamplerState SampGlow : register(s3);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
SamplerState SampGradientRemap : register(s5);
#endif
SamplerState SampMeatCuffDiffuse : register(s9);
SamplerState SampMeatCuffNormal : register(s10);
SamplerState SampMeatCuffSpecular : register(s11);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif

#if LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_MEAT_CUFF_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_X cb2[7]
#define LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_Y cb2[8]
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK cb2[9]
#define LINEAR_LIGHTING_MEAT_CUFF_DEPTH cb2[10]
#else
#define LINEAR_LIGHTING_MEAT_CUFF_DEPTH cb2[9]
#endif
#else
#define LINEAR_LIGHTING_MEAT_CUFF_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_X cb2[6]
#define LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_Y cb2[7]
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK cb2[8]
#define LINEAR_LIGHTING_MEAT_CUFF_DEPTH cb2[9]
#else
#define LINEAR_LIGHTING_MEAT_CUFF_DEPTH cb2[8]
#endif
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float2 cuffIndex : TEXCOORD6;
    float3 cuffOrientation : TEXCOORD7;
    float3 cuffBasisX : TEXCOORD8;
    float3 cuffBasisY : TEXCOORD9;
    uint eyeIndex : EYEINDEX;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float4 target4 : SV_Target4;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    const float2 baseUv = float2(
        input.currentPosition.w,
        input.previousPosition.w);
    if (LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK.y != 0.0)
    {
        const float2 coordinateSign =
            (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        const int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        const float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK.y *
            (0.5 - noise)) +
            LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK.z - 0.5);
    }
    if (LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK.w != 0.0)
    {
        const float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, baseUv).w;
        clip(LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK.x - additionalAlpha);
    }
#endif
    const float3 surfaceTangent = normalize(input.tangent);
    const float3 surfaceBitangent = normalize(input.bitangent);
    const float3 surfaceNormal = normalize(input.normal);
    const bool invalidCuffIndex =
        input.cuffIndex.x < 0.0 || input.cuffIndex.x > 10.0;

    const float orientation =
        (dot(
            LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_X.xyz,
            input.cuffOrientation) * 0.5) + 0.5;
    const bool positiveOrientation =
        dot(
            LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_Y.xyz,
            input.cuffOrientation) > 0.0;
    const float cuffNormalU = positiveOrientation ?
        orientation * 0.5 : 1.0 - (orientation * 0.5);
    const float2 cuffNormalUv = float2(
        cuffNormalU,
        input.cuffIndex.x * 0.1);
    const float2 cuffMaterialUv = 1.0 - cuffNormalUv;

    float4 diffuseSample = TexMeatCuffDiffuse.Sample(
        SampMeatCuffDiffuse, cuffMaterialUv);
    float2 specularSample = TexMeatCuffSpecular.Sample(
        SampMeatCuffSpecular, cuffMaterialUv).xy;
    const float2 cuffNormalXY =
        (TexMeatCuffNormal.Sample(
            SampMeatCuffNormal, cuffNormalUv).xy * 2.0) - 1.0;
#if !LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    const float2 baseUv = float2(
        input.currentPosition.w,
        input.previousPosition.w);
#endif
    const float4 baseDiffuseSample = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, baseUv));
    const float3 baseNormalSample = TexNormal.Sample(SampNormal, baseUv).xyz;
    const float2 baseSpecularSample = TexSpecular.Sample(
        SampSpecular, baseUv).xy;
#if LINEAR_LIGHTING_GRADIENT_REMAP
    const float gradientRemapSource = baseDiffuseSample.y;
    float gradientRemapRow = cb2[3].x;
#if LINEAR_LIGHTING_VERTEX_COLOR
    gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0;
#endif
    const float4 gradientRemap = TexGradientRemap.SampleLevel(
        SampGradientRemap,
        float2(pow(gradientRemapSource, 0.454545), gradientRemapRow),
        0.0);
#endif

    float3 projectedNormal;
    if (invalidCuffIndex)
    {
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 modelNormal = (baseNormalSample * 2.0) - 1.0;
        const float3 tangentNormal = float3(
            modelNormal.x,
            modelNormal.z,
            input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
        const float2 normalXY = (baseNormalSample.xy * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        const float3 tangentNormal = float3(
            normalXY,
            input.isFrontFace ? normalZ : -normalZ);
#endif
        projectedNormal = float3(
            dot(surfaceTangent, tangentNormal),
            dot(surfaceBitangent, tangentNormal),
            dot(surfaceNormal, tangentNormal));
#if LINEAR_LIGHTING_GRADIENT_REMAP
        diffuseSample.xyz = gradientRemap.xyz;
        diffuseSample.w = baseDiffuseSample.w;
#if LINEAR_LIGHTING_VERTEX_COLOR
        diffuseSample.w *= input.vertexColor.w;
#endif
#elif LINEAR_LIGHTING_VERTEX_COLOR
        diffuseSample = baseDiffuseSample * input.vertexColor;
#endif
#if LINEAR_LIGHTING_SKIN_TINT
#if !LINEAR_LIGHTING_VERTEX_COLOR
        diffuseSample = baseDiffuseSample;
#endif
        const float3 skinTintGamma = pow(abs(cb2[3].xyz), 0.454545);
        const float3 skinBaseGamma =
            pow(abs(diffuseSample.xyz), 0.454545);
        const float3 skinTintDark =
            (2.0 * skinBaseGamma * skinTintGamma) +
            (skinBaseGamma * skinBaseGamma *
            (1.0 - (2.0 * skinTintGamma)));
        const float3 skinTintLight =
            (sqrt(skinBaseGamma) * ((2.0 * skinTintGamma) - 1.0)) +
            (2.0 * skinBaseGamma * (1.0 - skinTintGamma));
        const float3 skinTintGammaResult =
            (skinTintGamma < 0.5) ? skinTintDark : skinTintLight;
        const float3 skinTint = pow(abs(skinTintGammaResult), 2.2);
        diffuseSample.xyz =
            lerp(diffuseSample.xyz, skinTint, cb2[3].w);
#endif
        clip(-1.0);
#if !LINEAR_LIGHTING_VERTEX_COLOR && !LINEAR_LIGHTING_SKIN_TINT
#if !LINEAR_LIGHTING_GRADIENT_REMAP
        diffuseSample = baseDiffuseSample;
#endif
#endif
        specularSample = baseSpecularSample;
#if LINEAR_LIGHTING_GRADIENT_REMAP
        specularSample.y *= gradientRemap.w;
#endif
    }
    else
    {
        const float cuffNormalZ = sqrt(
            1.0 - dot(cuffNormalXY, cuffNormalXY));
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 reconstructedNormal = normalize(
            (cuffNormalXY.y * input.cuffBasisY) +
            (cuffNormalXY.x * input.cuffBasisX) +
            (cuffNormalZ * input.cuffOrientation));
        projectedNormal = float3(
            dot(surfaceTangent, reconstructedNormal),
            dot(surfaceBitangent, reconstructedNormal),
            dot(surfaceNormal, reconstructedNormal));
#else
        const float3 tangentNormal = float3(cuffNormalXY, cuffNormalZ);
        projectedNormal = float3(
            dot(surfaceTangent, tangentNormal),
            dot(surfaceBitangent, tangentNormal),
            dot(surfaceNormal, tangentNormal));
#endif
    }

    const float2 materialXYCandidate =
        (((LINEAR_LIGHTING_MEAT_CUFF_INTERPOLATION.xy - cb2[0].xy) *
        cb12[50].xx) + cb2[0].xy) *
        cb2[0].xy;
    const float2 materialXY =
        (LINEAR_LIGHTING_MEAT_CUFF_INTERPOLATION.xy >= 0.0) ?
        materialXYCandidate : cb2[0].xy;
    const float specBlendA =
        cb12[50].x * LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.z;
    const float specBlendB =
        (-LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.z * cb12[50].x) + 1.0;
    const float specBlend =
        (specularSample.x * specBlendB) + specBlendA;
    output.target3.x = materialXY.x * specularSample.y;
    output.target3.y = materialXY.y * specBlend;

    projectedNormal.z = min(projectedNormal.z, 0.0);
    projectedNormal = normalize(projectedNormal);
    output.target1.z = -projectedNormal.z;
    const float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    const float depthSwitch =
        (LINEAR_LIGHTING_MEAT_CUFF_DEPTH.w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange =
        LINEAR_LIGHTING_MEAT_CUFF_DEPTH.w -
        LINEAR_LIGHTING_MEAT_CUFF_DEPTH.z;
    const float depthValue = (LINEAR_LIGHTING_MEAT_CUFF_DEPTH.y != 0.0) ?
        ((depthSwitch * depthRange) + LINEAR_LIGHTING_MEAT_CUFF_DEPTH.z) :
        (depthSwitch * LINEAR_LIGHTING_MEAT_CUFF_DEPTH.w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 &&
        LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.y != 0.0) ||
        LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.x != 0.0) ?
        1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_MEAT_CUFF_DEPTH.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_MEAT_CUFF_DEPTH.x);

    const float alphaSource = (cb2[2].y == 1.0) ? diffuseSample.w : 1.0;
    const float alpha = cb2[2].x * alphaSource;
    clip(alpha - 0.015686);
    const float fade = (LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.w == -1.0) ?
        1.0 : ((-LINEAR_LIGHTING_MEAT_CUFF_PROPERTIES.w * cb12[50].x) + 1.0);
    output.target0.xyz =
        fade * LinearLightingDiffuse(diffuseSample.xyz);
    output.target0.w = alpha;
    output.target1.w = alpha;
    output.target3.z = cb2[0].w * 0.01;
#if LINEAR_LIGHTING_SKIN_TINT
    output.target3.w = 0.019608;
#else
    output.target3.w = alpha;
#endif
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    const float3 glow =
        LinearLightingGlowmap(TexGlow.Sample(SampGlow, baseUv).xyz);
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
#endif
    output.target4.w = alpha;
    return output;
}
