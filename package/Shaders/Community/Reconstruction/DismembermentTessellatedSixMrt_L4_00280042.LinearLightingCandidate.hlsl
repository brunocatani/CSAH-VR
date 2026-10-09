#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_TESSELLATED_INPUTS
#define LINEAR_LIGHTING_TESSELLATED_INPUTS 1
#endif

#ifndef LINEAR_LIGHTING_DISMEMBERMENT
#define LINEAR_LIGHTING_DISMEMBERMENT 1
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
#error Combined dismemberment skin tint and gradient remap are not verified.
#endif

cbuffer PerMaterial : register(b2)
{
#if (LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP) && \
    LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[12];
#elif LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP || \
    LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[11];
#else
    float4 cb2[10];
#endif
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
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
Texture2D<float4> TexDismembermentDiffuse : register(t9);
Texture2D<float4> TexDismembermentNormal : register(t10);
Texture2D<float4> TexDismembermentSpecular : register(t11);
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
SamplerState SampDismembermentDiffuse : register(s9);
SamplerState SampDismembermentNormal : register(s10);
SamplerState SampDismembermentSpecular : register(s11);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif

#if LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_DISMEMBERMENT_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_X cb2[6]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Y cb2[7]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Z cb2[8]
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK cb2[10]
#define LINEAR_LIGHTING_DISMEMBERMENT_DEPTH cb2[11]
#else
#define LINEAR_LIGHTING_DISMEMBERMENT_DEPTH cb2[10]
#endif
#else
#define LINEAR_LIGHTING_DISMEMBERMENT_INTERPOLATION cb2[2]
#define LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES cb2[4]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_X cb2[5]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Y cb2[6]
#define LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Z cb2[7]
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK cb2[9]
#define LINEAR_LIGHTING_DISMEMBERMENT_DEPTH cb2[10]
#else
#define LINEAR_LIGHTING_DISMEMBERMENT_DEPTH cb2[9]
#endif
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 dismembermentUv : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float2 dismembermentSelector : TEXCOORD5;
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
    uint eyeIndex : EYEINDEX;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
    float2 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
    float3 target4 : SV_Target4;
    float2 target5 : SV_Target5;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    if (LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK.y != 0.0)
    {
        const float2 coordinateSign =
            (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        const int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        const float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK.y *
            (0.5 - noise)) +
            LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK.z - 0.5);
    }
    if (LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK.w != 0.0)
    {
        const float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, input.uv).w;
        clip(LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK.x - additionalAlpha);
    }
#endif
    const bool useDismemberment = input.dismembermentSelector.y > 0.0;

#if LINEAR_LIGHTING_GRADIENT_REMAP
    const float gradientRemapSource =
        AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, input.uv)).y;
    float gradientRemapRow = cb2[2].x;
#if LINEAR_LIGHTING_VERTEX_COLOR
    gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0;
#endif
    const float4 gradientRemap = TexGradientRemap.SampleLevel(
        SampGradientRemap,
        float2(pow(gradientRemapSource, 0.454545), gradientRemapRow),
        0.0);
#endif

    float2 specularSample;
    float3 projectedNormal;
    if (useDismemberment)
    {
        specularSample = TexDismembermentSpecular.Sample(
            SampDismembermentSpecular, input.dismembermentUv).xy;
        const float2 normalXY =
            (TexDismembermentNormal.Sample(
                SampDismembermentNormal, input.dismembermentUv).xy * 2.0) -
            1.0;
        const float normalZ = sqrt(1.0 - dot(normalXY, normalXY));
        const float3 tangentNormal = float3(normalXY, normalZ);
        projectedNormal = float3(
            dot(LINEAR_LIGHTING_DISMEMBERMENT_BASIS_X.xyz, tangentNormal),
            dot(LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Y.xyz, tangentNormal),
            min(dot(
                LINEAR_LIGHTING_DISMEMBERMENT_BASIS_Z.xyz,
                tangentNormal), 0.0));
    }
    else
    {
        specularSample = TexSpecular.Sample(SampSpecular, input.uv).xy;
#if LINEAR_LIGHTING_GRADIENT_REMAP
        specularSample.y *= gradientRemap.w;
#endif
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
        const float3 modelNormal =
            (TexNormal.Sample(SampNormal, input.uv).xyz * 2.0) - 1.0;
        const float3 tangentNormal = float3(
            modelNormal.x,
            modelNormal.z,
            input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
        const float2 normalXY =
            (TexNormal.Sample(SampNormal, input.uv).xy * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        const float3 tangentNormal = float3(
            normalXY,
            input.isFrontFace ? normalZ : -normalZ);
#endif
        projectedNormal = float3(
            dot(normalize(input.tangent), tangentNormal),
            dot(normalize(input.bitangent), tangentNormal),
            min(dot(normalize(input.normal), tangentNormal), 0.0));
    }
    projectedNormal = normalize(projectedNormal);

    const float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    const float2 materialXYCandidate =
        (((LINEAR_LIGHTING_DISMEMBERMENT_INTERPOLATION.xy - cb2[0].xy) *
        cb12[50].xx) + cb2[0].xy) *
        cb2[0].xy;
    const float2 materialXY =
        (LINEAR_LIGHTING_DISMEMBERMENT_INTERPOLATION.xy >= 0.0) ?
        materialXYCandidate : cb2[0].xy;
    const float specBlendA =
        cb12[50].x * LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.z;
    const float specBlendB =
        (-LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.z * cb12[50].x) + 1.0;
    const float specBlend =
        (specularSample.x * specBlendB) + specBlendA;
    output.target3.xy = materialXY * float2(
        specularSample.y,
        specBlend);

    const float depthSwitch =
        (LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange =
        LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.w -
        LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.z;
    const float depthValue = (LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.y != 0.0) ?
        ((depthSwitch * depthRange) +
        LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.z) :
        (depthSwitch * LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 &&
        LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.y != 0.0) ||
        LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.x != 0.0) ?
        1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_DISMEMBERMENT_DEPTH.x);

    float3 diffuse;
    if (useDismemberment)
    {
        diffuse = TexDismembermentDiffuse.Sample(
            SampDismembermentDiffuse, input.dismembermentUv).xyz;
    }
    else
    {
#if LINEAR_LIGHTING_GRADIENT_REMAP
        diffuse = gradientRemap.xyz;
#else
        diffuse = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, input.uv)).xyz;
#if LINEAR_LIGHTING_VERTEX_COLOR
        diffuse *= input.vertexColor.xyz;
#endif
#if LINEAR_LIGHTING_SKIN_TINT
        const float3 skinTintGamma = pow(abs(cb2[2].xyz), 0.454545);
        const float3 skinBaseGamma = pow(abs(diffuse), 0.454545);
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
        diffuse = lerp(diffuse, skinTint, cb2[2].w);
#endif
#endif
    }
    const float fade = (LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.w == -1.0) ?
        1.0 :
        ((-LINEAR_LIGHTING_DISMEMBERMENT_PROPERTIES.w * cb12[50].x) + 1.0);
    const float3 transformedDiffuse = useDismemberment ?
        LinearLightingDiffuse(diffuse) :
        LinearLightingDecodedDiffuse(diffuse);
    output.target0.xyz = fade * transformedDiffuse;

    const uint matrixBase = input.eyeIndex * 4u;
    const float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    const float currentW = dot(cb12[matrixBase + 66u], currentPosition);
    const float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;
    const float previousW = dot(cb12[matrixBase + 54u], currentPosition);
    const float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], currentPosition),
        dot(cb12[matrixBase + 52u], currentPosition)) / previousW;
    output.target5.xy =
        (currentNdc - previousNdc) * float2(-0.5, 0.5);

    output.target0.w = cb2[0].z;
    output.target3.z = cb2[0].w * 0.01;
#if LINEAR_LIGHTING_SKIN_TINT
    output.target3.w = 0.019608;
#else
    output.target3.w = 1.0;
#endif
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    const float3 glow =
        LinearLightingGlowmap(TexGlow.Sample(SampGlow, input.uv).xyz);
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
#endif
    return output;
}
