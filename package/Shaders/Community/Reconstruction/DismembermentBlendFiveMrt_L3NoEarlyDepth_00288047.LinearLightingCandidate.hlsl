#define LINEAR_LIGHTING_VERTEX_COLOR 1
#define LINEAR_LIGHTING_DISMEMBERMENT 1
#define LINEAR_LIGHTING_DISMEMBERMENT_BLEND 1

#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
#endif

cbuffer PerMaterial : register(b2)
{
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[12];
#else
    float4 cb2[11];
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
SamplerState SampDismembermentDiffuse : register(s9);
SamplerState SampDismembermentNormal : register(s10);
SamplerState SampDismembermentSpecular : register(s11);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif

#define LINEAR_LIGHTING_BLEND_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_BLEND_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_BLEND_BASIS_X cb2[6]
#define LINEAR_LIGHTING_BLEND_BASIS_Y cb2[7]
#define LINEAR_LIGHTING_BLEND_BASIS_Z cb2[8]
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_BLEND_ALPHA_MASK cb2[10]
#define LINEAR_LIGHTING_BLEND_DEPTH cb2[11]
#else
#define LINEAR_LIGHTING_BLEND_DEPTH cb2[10]
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 dismembermentUv : TEXCOORD4;
    float4 vertexColor : COLOR0;
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
    float2 dismembermentSelector : TEXCOORD5;
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
    if (LINEAR_LIGHTING_BLEND_ALPHA_MASK.y != 0.0)
    {
        const float2 coordinateSign =
            (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        const int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        const float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_BLEND_ALPHA_MASK.y * (0.5 - noise)) +
            LINEAR_LIGHTING_BLEND_ALPHA_MASK.z - 0.5);
    }
    if (LINEAR_LIGHTING_BLEND_ALPHA_MASK.w != 0.0)
    {
        const float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, input.uv).w;
        clip(LINEAR_LIGHTING_BLEND_ALPHA_MASK.x - additionalAlpha);
    }
#endif

    const float4 baseDiffuseSample = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, input.uv));
    const bool useDismemberment = input.dismembermentSelector.y > 0.0;
    const float2 specularSample = useDismemberment ?
        TexDismembermentSpecular.Sample(
            SampDismembermentSpecular, input.dismembermentUv).yx :
        TexSpecular.Sample(SampSpecular, input.uv).yx;

    float3 projectedNormal;
    if (useDismemberment)
    {
        const float2 normalXY =
            (TexDismembermentNormal.Sample(
                SampDismembermentNormal, input.dismembermentUv).xy * 2.0) -
            1.0;
        const float normalZ = sqrt(1.0 - dot(normalXY, normalXY));
        const float3 tangentNormal = float3(normalXY, normalZ);
        projectedNormal = float3(
            dot(LINEAR_LIGHTING_BLEND_BASIS_X.xyz, tangentNormal),
            dot(LINEAR_LIGHTING_BLEND_BASIS_Y.xyz, tangentNormal),
            dot(LINEAR_LIGHTING_BLEND_BASIS_Z.xyz, tangentNormal));
    }
    else
    {
        const float2 normalXY =
            (TexNormal.Sample(SampNormal, input.uv).xy * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        const float3 tangentNormal = float3(
            normalXY,
            input.isFrontFace ? normalZ : -normalZ);
        projectedNormal = float3(
            dot(normalize(input.tangent), tangentNormal),
            dot(normalize(input.bitangent), tangentNormal),
            dot(normalize(input.normal), tangentNormal));
    }

    const float2 materialXYCandidate =
        (((LINEAR_LIGHTING_BLEND_INTERPOLATION.xy - cb2[0].xy) *
        cb12[50].xx) + cb2[0].xy) * cb2[0].xy;
    const float2 materialXY =
        (LINEAR_LIGHTING_BLEND_INTERPOLATION.xy >= 0.0) ?
        materialXYCandidate : cb2[0].xy;
    const float specBlendA =
        cb12[50].x * LINEAR_LIGHTING_BLEND_PROPERTIES.z;
    const float specBlendB =
        (-LINEAR_LIGHTING_BLEND_PROPERTIES.z * cb12[50].x) + 1.0;
    const float specBlend =
        (specularSample.y * specBlendB) + specBlendA;
    output.target3.xy = materialXY * float2(specularSample.x, specBlend);

    projectedNormal.z = min(projectedNormal.z, 0.0);
    projectedNormal = normalize(projectedNormal);
    output.target1.z = -projectedNormal.z;
    const float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    const float depthSwitch =
        (LINEAR_LIGHTING_BLEND_DEPTH.w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange =
        LINEAR_LIGHTING_BLEND_DEPTH.w - LINEAR_LIGHTING_BLEND_DEPTH.z;
    const float depthValue = (LINEAR_LIGHTING_BLEND_DEPTH.y != 0.0) ?
        ((depthSwitch * depthRange) + LINEAR_LIGHTING_BLEND_DEPTH.z) :
        (depthSwitch * LINEAR_LIGHTING_BLEND_DEPTH.w);
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.x =
        ((cb12[50].x != 0.0 && LINEAR_LIGHTING_BLEND_PROPERTIES.y != 0.0) ||
        LINEAR_LIGHTING_BLEND_PROPERTIES.x != 0.0) ? 1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_BLEND_DEPTH.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_BLEND_DEPTH.x);

    const float baseAlpha = baseDiffuseSample.w * input.vertexColor.w;
    const float alphaSource = (cb2[2].y == 1.0) ? baseAlpha : 1.0;
    const float alpha = cb2[2].x * alphaSource;
    clip(alpha - 0.015686);

    const float3 diffuse = useDismemberment ?
        TexDismembermentDiffuse.Sample(
            SampDismembermentDiffuse, input.dismembermentUv).xyz :
        (baseDiffuseSample.xyz * input.vertexColor.xyz);
    const float fade = (LINEAR_LIGHTING_BLEND_PROPERTIES.w == -1.0) ?
        1.0 :
        ((-LINEAR_LIGHTING_BLEND_PROPERTIES.w * cb12[50].x) + 1.0);
    const float3 transformedDiffuse = useDismemberment ?
        LinearLightingDiffuse(diffuse) :
        LinearLightingDecodedDiffuse(diffuse);
    output.target0.xyz = fade * transformedDiffuse;
    output.target0.w = alpha;
    output.target1.w = alpha;
    output.target3.z = cb2[0].w * 0.01;
    output.target3.w = alpha;
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
    output.target4.w = alpha;
    return output;
}
