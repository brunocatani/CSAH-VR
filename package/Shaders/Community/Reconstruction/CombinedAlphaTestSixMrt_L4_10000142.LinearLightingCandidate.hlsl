#define LINEAR_LIGHTING_COMBINED 1

#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
#endif

#ifndef LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_GRADIENT_REMAP 0
#endif

#ifndef LINEAR_LIGHTING_COMBINED_BLEND
#define LINEAR_LIGHTING_COMBINED_BLEND 0
#endif

#if LINEAR_LIGHTING_COMBINED_BLEND && \
    (LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || LINEAR_LIGHTING_GRADIENT_REMAP)
#error The verified combined blend contract has no alpha-mask or gradient variant.
#endif

cbuffer PerMaterial : register(b2)
{
#if LINEAR_LIGHTING_GRADIENT_REMAP && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    float4 cb2[7];
#elif LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || \
    LINEAR_LIGHTING_COMBINED_BLEND
    float4 cb2[6];
#else
    float4 cb2[5];
#endif
};

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
#if LINEAR_LIGHTING_COMBINED_BLEND
    float4 cb12[51];
#else
    float4 cb12[71];
#endif
};

// This is the exact 92-byte t4 record consumed by the VR combined-material
// shaders. Keep the uint texture selectors typed: the original bytecode
// explicitly converts them to floating-point array slices.
struct CombinedMaterialData
{
    float4 material;
    float4 emitColorAndAlphaReference;
    float4 interpolationAndProperties;
    float4 reserved;
    uint4 textureSlices;
    uint gradientTextureSlice;
    float gradientRow;
    float depth;
};

// The combined path sources its byte-scaled depth/material value from the
// final dword of a separate 100-byte t6 record.
struct CombinedDepthData
{
    float4 reserved[6];
    float value;
};

Texture2DArray<float4> TexDiffuse : register(t0);
Texture2DArray<float4> TexNormal : register(t1);
Texture2DArray<float4> TexSpecular : register(t2);
StructuredBuffer<CombinedMaterialData> CombinedMaterials : register(t4);
#if LINEAR_LIGHTING_GRADIENT_REMAP
Texture2DArray<float4> TexGradientRemap : register(t5);
#endif
StructuredBuffer<CombinedDepthData> CombinedDepths : register(t6);
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
Texture2D<float4> TexAdditionalAlpha : register(t12);
Texture2D<float4> TexAdditionalAlphaNoise : register(t15);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
#if LINEAR_LIGHTING_GRADIENT_REMAP
SamplerState SampGradientRemap : register(s5);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif

#if LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_COMBINED_BLEND
#define LINEAR_LIGHTING_COMBINED_FLAGS cb2[5]
#else
#define LINEAR_LIGHTING_COMBINED_FLAGS cb2[4]
#endif

#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#if LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_COMBINED_ALPHA_MASK cb2[6]
#else
#define LINEAR_LIGHTING_COMBINED_ALPHA_MASK cb2[5]
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
    uint materialIndex : COLOR2;
    uint eyeIndex : EYEINDEX;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput
{
    float4 target0 : SV_Target0;
#if LINEAR_LIGHTING_COMBINED_BLEND
    float4 target1 : SV_Target1;
#else
    float2 target1 : SV_Target1;
#endif
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
#if LINEAR_LIGHTING_COMBINED_BLEND
    float4 target4 : SV_Target4;
#else
    float3 target4 : SV_Target4;
    float2 target5 : SV_Target5;
#endif
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    const float2 uv = float2(
        input.currentPosition.w,
        input.previousPosition.w);

#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    if (LINEAR_LIGHTING_COMBINED_ALPHA_MASK.y != 0.0)
    {
        const float2 coordinateSign =
            (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        const int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        const float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_COMBINED_ALPHA_MASK.y * (0.5 - noise)) +
            LINEAR_LIGHTING_COMBINED_ALPHA_MASK.z - 0.5);
    }
    if (LINEAR_LIGHTING_COMBINED_ALPHA_MASK.w != 0.0)
    {
        const float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, uv).w;
        clip(LINEAR_LIGHTING_COMBINED_ALPHA_MASK.x - additionalAlpha);
    }
#endif

    const CombinedMaterialData material =
        CombinedMaterials[input.materialIndex];
    const float3 textureSlices = float3(material.textureSlices.xyz);
    const float4 diffuseSample = AdjustMaterialLodAlbedo(TexDiffuse.Sample(
        SampDiffuse,
        float3(uv, textureSlices.x)));

#if LINEAR_LIGHTING_GRADIENT_REMAP
    const float4 gradientRemap = TexGradientRemap.SampleLevel(
        SampGradientRemap,
        float3(
            pow(diffuseSample.y, 0.454545),
            material.gradientRow,
            float(material.gradientTextureSlice)),
        0.0);
    float3 diffuse = gradientRemap.xyz;
#else
    float3 diffuse = diffuseSample.xyz;
#endif
#if LINEAR_LIGHTING_VERTEX_COLOR
    diffuse *= input.vertexColor.xyz;
    clip((diffuseSample.w * input.vertexColor.w) -
        material.emitColorAndAlphaReference.w);
#else
    clip(diffuseSample.w - material.emitColorAndAlphaReference.w);
#endif

    const float2 normalXY =
        (TexNormal.Sample(
            SampNormal,
            float3(uv, textureSlices.y)).xy * 2.0) - 1.0;
    const float normalZ = sqrt(
        1.0 - min(dot(normalXY, normalXY), 1.0));
    const float3 tangentNormal = float3(
        normalXY,
        input.isFrontFace ? normalZ : -normalZ);
    const float3 sourceNormal = normalize(input.normal);
    float3 projectedNormal = float3(
        dot(normalize(input.tangent), tangentNormal),
        dot(normalize(input.bitangent), tangentNormal),
        min(dot(sourceNormal, tangentNormal), 0.0));
    projectedNormal = normalize(projectedNormal);

#if LINEAR_LIGHTING_COMBINED_BLEND
    output.target1.z = -projectedNormal.z;
#endif
    const float normalPackScale = sqrt(
        (projectedNormal.z * -8.0) + 8.0);
    output.target1.xy =
        (projectedNormal.xy / normalPackScale) + 0.5;

    float2 specularSample = TexSpecular.Sample(
        SampSpecular,
        float3(uv, textureSlices.z)).xy;
#if LINEAR_LIGHTING_GRADIENT_REMAP
    specularSample.y *= gradientRemap.w;
#endif
    const float geometrySwitch = cb12[50].x;
    const float specBlend =
        (specularSample.x *
            ((-material.interpolationAndProperties.z * geometrySwitch) +
                1.0)) +
        (material.interpolationAndProperties.z * geometrySwitch);
    const float2 interpolation =
        material.interpolationAndProperties.yx;
    const float2 interpolationFactor =
        (interpolation == -1.0) ? 0.0 : geometrySwitch.xx;
    const float2 materialXY =
        ((interpolation - material.material.xy) * interpolationFactor) +
        material.material.xy;
    output.target3.x = specularSample.y * materialXY.x;
    output.target3.y =
        material.material.y * specBlend * materialXY.y;
    output.target3.z = material.material.w * 0.01;

    const float depthValue = material.depth * 0.02;
    output.target2.z = sqrt(depthValue);
    output.target2.x =
        (LINEAR_LIGHTING_COMBINED_FLAGS.x != 0.0 ||
        (geometrySwitch != 0.0 &&
            LINEAR_LIGHTING_COMBINED_FLAGS.y != 0.0)) ? 1.0 : 0.0;
    const float depthByte = CombinedDepths[input.materialIndex].value;
    output.target2.y = depthByte * 0.003922;
    output.target2.w = saturate(depthByte);

    const float fade =
        (material.interpolationAndProperties.w == -1.0) ? 1.0 :
        ((-material.interpolationAndProperties.w * geometrySwitch) + 1.0);
    output.target0.xyz = fade * LinearLightingDecodedDiffuse(diffuse);
    output.target4.xyz = LinearLightingEmitColor(
        material.emitColorAndAlphaReference.xyz);

#if LINEAR_LIGHTING_COMBINED_BLEND
    const float alphaSource = (cb2[2].y == 1.0) ? diffuseSample.w : 1.0;
    const float alpha = cb2[2].x * alphaSource;
    clip(alpha - 0.015686);
    output.target0.w = alpha;
    output.target1.w = alpha;
    output.target3.w = alpha;
    output.target4.w = alpha;
#else
    output.target0.w = material.material.z;
    output.target3.w = 1.0;

    const uint matrixBase = input.eyeIndex * 4u;
    const float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    const float currentW = dot(
        cb12[matrixBase + 66u],
        currentPosition);
    const float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;
    const float4 previousPosition = float4(input.previousPosition.xyz, 1.0);
    const float previousW = dot(
        cb12[matrixBase + 54u],
        previousPosition);
    const float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], previousPosition),
        dot(cb12[matrixBase + 52u], previousPosition)) / previousW;
    output.target5.xy =
        (currentNdc - previousNdc) * float2(-0.5, 0.5);
#endif
    return output;
}
