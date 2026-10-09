#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 1
#endif

#ifndef LINEAR_LIGHTING_LAND_LOD_BLEND
#define LINEAR_LIGHTING_LAND_LOD_BLEND 0
#endif

#ifndef LINEAR_LIGHTING_INSTANCED_LANDSCAPE
#define LINEAR_LIGHTING_INSTANCED_LANDSCAPE 0
#endif

#ifndef LINEAR_LIGHTING_COMPLEX_PARALLAX
#define LINEAR_LIGHTING_COMPLEX_PARALLAX 0
#endif

#ifndef LINEAR_LIGHTING_SURFACE_CLASSIFICATION
#define LINEAR_LIGHTING_SURFACE_CLASSIFICATION 0
#endif

cbuffer PerMaterial : register(b2)
{
    float4 cb2[9];
};

#if LINEAR_LIGHTING_LAND_LOD_BLEND
cbuffer LandscapeLodGlobals : register(b0)
{
    float4 cb0[1];
};
#endif

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
};

#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
// The VR instanced-landscape shader consumes this exact 76-byte t4 record.
// The texture selectors intentionally stay uint: the original shader converts
// each selector to a floating-point Texture2DArray slice at the sample site.
struct LandscapeLayerIndices
{
    float4 reserved0;
    float2 reserved1;
    uint lodTextureSlice;
    uint diffuse0;
    uint normal0;
    uint specular0;
    uint diffuse1;
    uint normal1;
    uint specular1;
    uint diffuse2;
    uint normal2;
    uint specular2;
    uint diffuse3;
    uint normal3;
    uint specular3;
};

Texture2DArray<float4> TexDiffuseLayers : register(t0);
Texture2DArray<float4> TexNormalLayers : register(t1);
Texture2DArray<float4> TexSpecularLayers : register(t2);
Texture2DArray<float4> TexLodMultiplierLayers : register(t3);
StructuredBuffer<LandscapeLayerIndices> LandscapeIndices : register(t4);
#else
Texture2D<float4> TexDiffuse0 : register(t0);
Texture2D<float4> TexDiffuse1 : register(t1);
Texture2D<float4> TexDiffuse2 : register(t2);
Texture2D<float4> TexDiffuse3 : register(t3);
Texture2D<float4> TexNormal0 : register(t4);
Texture2D<float4> TexNormal1 : register(t5);
Texture2D<float4> TexNormal2 : register(t6);
Texture2D<float4> TexNormal3 : register(t7);
Texture2D<float4> TexSpecular0 : register(t8);
Texture2D<float4> TexSpecular1 : register(t9);
Texture2D<float4> TexSpecular2 : register(t10);
Texture2D<float4> TexSpecular3 : register(t11);
#if LINEAR_LIGHTING_LAND_LOD_BLEND
Texture2D<float4> TexLodMultiplier : register(t14);
#endif
#endif

#if LINEAR_LIGHTING_LAND_LOD_BLEND
Texture2D<float4> TexLandscapeLodDiffuse : register(t13);
Texture2D<float4> TexLandscapeLodNormal : register(t15);
#endif

SamplerState Samp0 : register(s0);
SamplerState Samp1 : register(s1);
SamplerState Samp2 : register(s2);
SamplerState Samp3 : register(s3);
#if !LINEAR_LIGHTING_INSTANCED_LANDSCAPE
SamplerState Samp4 : register(s4);
SamplerState Samp5 : register(s5);
SamplerState Samp6 : register(s6);
SamplerState Samp7 : register(s7);
SamplerState Samp8 : register(s8);
SamplerState Samp9 : register(s9);
SamplerState Samp10 : register(s10);
SamplerState Samp11 : register(s11);
#endif
#if LINEAR_LIGHTING_LAND_LOD_BLEND
SamplerState SampLandscapeLodDiffuse : register(s13);
#if !LINEAR_LIGHTING_INSTANCED_LANDSCAPE
SamplerState SampLodMultiplier : register(s14);
#endif
SamplerState SampLandscapeLodNormal : register(s15);
#endif

struct PSInput
{
    float4 position : SV_POSITION;
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
    float4 vertexColor : COLOR0;
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    uint landscapeRecordIndex : COLOR2;
    uint eyeIndex : EYEINDEX;
#endif
    float2 lodMultiplierUv : TEXCOORD6;
    float4 layerWeights : TEXCOORD7;
    float3 landscapeLodCoordinatesAndBlend : TEXCOORD8;
#if !LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    uint eyeIndex : EYEINDEX;
#endif
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
#if LINEAR_LIGHTING_SURFACE_CLASSIFICATION
    float surfaceClass : SV_Target6;
    float4 pbrMaterial : SV_Target7;
#endif
};

static StochasticOffsets terrainOffsets;

#if LINEAR_LIGHTING_COMPLEX_PARALLAX
float4 NormalizeLandscapeWeights(float4 weights)
{
    const float4 positive = max(weights, 0.0);
    return positive / max(dot(positive, 1.0), 1.0e-5);
}

#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
float BlendedParallaxDepth(
    float2 uv,
    float2 gradientX,
    float2 gradientY,
    float4 weights,
    LandscapeLayerIndices indices)
{
    float weightedHeight = 0.0;
    float activeWeight = 0.0;
#define ACCUMULATE_ARRAY_PARALLAX_HEIGHT(Weight, Slice) \
    [branch] if ((Weight) > 0.0) { \
        weightedHeight += (Weight) * SampleVariedTerrain(TexSpecularLayers, \
            Samp2, float3(uv, float(Slice)), gradientX, gradientY, terrainOffsets, true, true).a; \
        activeWeight += (Weight); \
    }
    ACCUMULATE_ARRAY_PARALLAX_HEIGHT(weights.x, indices.specular0);
    ACCUMULATE_ARRAY_PARALLAX_HEIGHT(weights.y, indices.specular1);
    ACCUMULATE_ARRAY_PARALLAX_HEIGHT(weights.z, indices.specular2);
    ACCUMULATE_ARRAY_PARALLAX_HEIGHT(weights.w, indices.specular3);
#undef ACCUMULATE_ARRAY_PARALLAX_HEIGHT
    // The test assets store elevation in specular alpha. The ray advances
    // through depth, so invert elevation exactly once at this boundary.
    return 1.0 - (weightedHeight / max(activeWeight, 1.0e-5));
}
#else
float BlendedParallaxDepth(
    float2 uv,
    float2 gradientX,
    float2 gradientY,
    float4 weights)
{
    float weightedHeight = 0.0;
    float activeWeight = 0.0;
#define ACCUMULATE_PARALLAX_HEIGHT(Weight, Texture, Sampler) \
    [branch] if ((Weight) > 0.0) { \
        weightedHeight += (Weight) * SampleVariedTerrain(Texture, \
            Sampler, uv, gradientX, gradientY, terrainOffsets, true, true).a; \
        activeWeight += (Weight); \
    }
    ACCUMULATE_PARALLAX_HEIGHT(weights.x, TexSpecular0, Samp8);
    ACCUMULATE_PARALLAX_HEIGHT(weights.y, TexSpecular1, Samp9);
    ACCUMULATE_PARALLAX_HEIGHT(weights.z, TexSpecular2, Samp10);
    ACCUMULATE_PARALLAX_HEIGHT(weights.w, TexSpecular3, Samp11);
#undef ACCUMULATE_PARALLAX_HEIGHT
    return 1.0 - (weightedHeight / max(activeWeight, 1.0e-5));
}
#endif

float2 ApplyComplexParallax(
    PSInput input,
    float2 uv,
    float2 gradientX,
    float2 gradientY
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    , LandscapeLayerIndices indices
#endif
    )
{
    if (enableComplexParallax == 0u || parallaxDepth <= 0.0) {
        return uv;
    }

    const float distanceFromEye = length(input.currentPosition.xyz);
    float fade = 1.0 - saturate(
        (distanceFromEye - parallaxFadeStart) /
        max(parallaxFadeEnd - parallaxFadeStart, 1.0));
#if LINEAR_LIGHTING_LAND_LOD_BLEND
    fade *= 1.0 - saturate(input.landscapeLodCoordinatesAndBlend.z);
#endif
    if (fade <= 0.0) {
        return uv;
    }

    // currentPosition is eye-relative for the active EYEINDEX. Rotate that
    // per-eye ray into the same view basis used by the emitted TBN rows.
    const float3 eyeRelativeViewPosition = float3(
        dot(cb12[0].xyz, input.currentPosition.xyz),
        dot(cb12[1].xyz, input.currentPosition.xyz),
        dot(cb12[2].xyz, input.currentPosition.xyz));
    const float3 viewDirection = normalize(-eyeRelativeViewPosition);

    // TEXCOORD0/1/2 are tangent-to-view rows. Their orthonormal inverse is
    // the transpose, assembled as columns here.
    const float3 tangentAxis = normalize(float3(
        input.tangent.x,
        input.bitangent.x,
        input.normal.x));
    const float3 bitangentAxis = normalize(float3(
        input.tangent.y,
        input.bitangent.y,
        input.normal.y));
    const float3 normalAxis = normalize(float3(
        input.tangent.z,
        input.bitangent.z,
        input.normal.z));
    float3 tangentView = float3(
        dot(viewDirection, tangentAxis),
        dot(viewDirection, bitangentAxis),
        dot(viewDirection, normalAxis));
    tangentView.z = input.isFrontFace ? tangentView.z : -tangentView.z;

    const float viewZ = max(abs(tangentView.z), parallaxGrazingClamp);
    const float grazing = 1.0 - saturate(viewZ);
    const float fullDetailStepCount = lerp(
        max(parallaxMinimumSteps, 1.0),
        max(parallaxMaximumSteps, parallaxMinimumSteps),
        grazing);
    // Match Community Shaders' distance-adaptive POM budget: preserve the
    // configured step count at full displacement, then converge toward four
    // steps as the already-visible parallax displacement fades away.
    const float distanceDetail = sqrt(saturate(fade));
    const float stepCount = round(lerp(
        4.0,
        fullDetailStepCount,
        distanceDetail));
    const float layerStep = 1.0 / stepCount;
    const float2 rayStep =
        (tangentView.xy / viewZ) * (parallaxDepth * fade / stepCount);
    const float4 weights = NormalizeLandscapeWeights(input.layerWeights);

    float2 currentUv = uv;
    float currentLayer = 0.0;
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    float sampledDepth = BlendedParallaxDepth(
        currentUv, gradientX, gradientY, weights, indices);
#else
    float sampledDepth = BlendedParallaxDepth(
        currentUv, gradientX, gradientY, weights);
#endif
    float2 previousUv = currentUv;
    float previousLayer = currentLayer;
    float previousDepth = sampledDepth;
    [loop]
    for (uint step = 0u; step < 32u &&
         float(step) < stepCount && currentLayer < sampledDepth; ++step) {
        previousUv = currentUv;
        previousLayer = currentLayer;
        previousDepth = sampledDepth;
        currentUv -= rayStep;
        currentLayer += layerStep;
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
        sampledDepth = BlendedParallaxDepth(
            currentUv, gradientX, gradientY, weights, indices);
#else
        sampledDepth = BlendedParallaxDepth(
            currentUv, gradientX, gradientY, weights);
#endif
    }
    const float after = sampledDepth - currentLayer;
    const float before = previousDepth - previousLayer;
    const float crossingSpan = after - before;
    const float interpolation = saturate(
        after / ((abs(crossingSpan) > 1.0e-5) ?
            crossingSpan : -1.0e-5));
    return lerp(currentUv, previousUv, interpolation);
}
#endif

void AccumulateLandscapeLayer(
    float weight,
    float3 sampledDiffuse,
    float2 sampledNormal,
    float2 sampledSpecular,
    inout float3 diffuse,
    inout float3 normal,
    inout float2 specular)
{
    [branch]
    if (weight > 0.0)
    {
        const float2 normalXY = (sampledNormal * 2.0) - 1.0;
        const float normalZ = sqrt(
            1.0 - min(dot(normalXY, normalXY), 1.0));
        diffuse += sampledDiffuse * weight;
        normal += float3(normalXY, normalZ) * weight;
        specular += sampledSpecular * weight;
    }
}

#if !LINEAR_LIGHTING_LAND_LOD_BLEND
[earlydepthstencil]
#endif
PSOutput PSMain(PSInput input)
{
    PSOutput output;
    const float2 baseUv = float2(
        input.currentPosition.w,
        input.previousPosition.w);

    const float2 gradientX = ddx(baseUv);
    const float2 gradientY = ddy(baseUv);
    terrainOffsets=(StochasticOffsets)0;
    if(TerrainVariationParameters[0].x>0.5)
        terrainOffsets=ComputeStochasticOffsets(input.lodMultiplierUv*0.5);

#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    const LandscapeLayerIndices layerIndices =
        LandscapeIndices[input.landscapeRecordIndex];
#endif

#if LINEAR_LIGHTING_COMPLEX_PARALLAX
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    const float2 uv = ApplyComplexParallax(
        input, baseUv, gradientX, gradientY, layerIndices);
#else
    const float2 uv = ApplyComplexParallax(
        input, baseUv, gradientX, gradientY);
#endif
#define LANDSCAPE_SAMPLE_2D(Texture, Sampler, Uv) \
    SampleVariedTerrain(Texture, Sampler, Uv, gradientX, gradientY, terrainOffsets, true, false)
#define LANDSCAPE_SAMPLE_ARRAY(Texture, Sampler, Uv) \
    SampleVariedTerrain(Texture, Sampler, Uv, gradientX, gradientY, terrainOffsets, true, false)
#else
    const float2 uv = baseUv;
#define LANDSCAPE_SAMPLE_2D(Texture, Sampler, Uv) SampleVariedTerrain(Texture, Sampler, Uv, gradientX, gradientY, terrainOffsets, false, false)
#define LANDSCAPE_SAMPLE_ARRAY(Texture, Sampler, Uv) SampleVariedTerrain(Texture, Sampler, Uv, gradientX, gradientY, terrainOffsets, false, false)
#endif

    float3 diffuse = 0.0;
    float3 detailNormal = 0.0;
    float2 specular = 0.0;

#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    const float3 diffuse0 = LANDSCAPE_SAMPLE_ARRAY(
        TexDiffuseLayers, Samp0, float3(uv, float(layerIndices.diffuse0))).xyz;
    const float3 diffuse1 = LANDSCAPE_SAMPLE_ARRAY(
        TexDiffuseLayers, Samp0, float3(uv, float(layerIndices.diffuse1))).xyz;
    const float3 diffuse2 = LANDSCAPE_SAMPLE_ARRAY(
        TexDiffuseLayers, Samp0, float3(uv, float(layerIndices.diffuse2))).xyz;
    const float3 diffuse3 = LANDSCAPE_SAMPLE_ARRAY(
        TexDiffuseLayers, Samp0, float3(uv, float(layerIndices.diffuse3))).xyz;
    const float2 normal0 = LANDSCAPE_SAMPLE_ARRAY(
        TexNormalLayers, Samp1, float3(uv, float(layerIndices.normal0))).xy;
    const float2 normal1 = LANDSCAPE_SAMPLE_ARRAY(
        TexNormalLayers, Samp1, float3(uv, float(layerIndices.normal1))).xy;
    const float2 normal2 = LANDSCAPE_SAMPLE_ARRAY(
        TexNormalLayers, Samp1, float3(uv, float(layerIndices.normal2))).xy;
    const float2 normal3 = LANDSCAPE_SAMPLE_ARRAY(
        TexNormalLayers, Samp1, float3(uv, float(layerIndices.normal3))).xy;
    const float2 specular0 = LANDSCAPE_SAMPLE_ARRAY(
        TexSpecularLayers, Samp2, float3(uv, float(layerIndices.specular0))).xy;
    const float2 specular1 = LANDSCAPE_SAMPLE_ARRAY(
        TexSpecularLayers, Samp2, float3(uv, float(layerIndices.specular1))).xy;
    const float2 specular2 = LANDSCAPE_SAMPLE_ARRAY(
        TexSpecularLayers, Samp2, float3(uv, float(layerIndices.specular2))).xy;
    const float2 specular3 = LANDSCAPE_SAMPLE_ARRAY(
        TexSpecularLayers, Samp2, float3(uv, float(layerIndices.specular3))).xy;
#else
    const float3 diffuse0 = LANDSCAPE_SAMPLE_2D(TexDiffuse0, Samp0, uv).xyz;
    const float3 diffuse1 = LANDSCAPE_SAMPLE_2D(TexDiffuse1, Samp1, uv).xyz;
    const float3 diffuse2 = LANDSCAPE_SAMPLE_2D(TexDiffuse2, Samp2, uv).xyz;
    const float3 diffuse3 = LANDSCAPE_SAMPLE_2D(TexDiffuse3, Samp3, uv).xyz;
    const float2 normal0 = LANDSCAPE_SAMPLE_2D(TexNormal0, Samp4, uv).xy;
    const float2 normal1 = LANDSCAPE_SAMPLE_2D(TexNormal1, Samp5, uv).xy;
    const float2 normal2 = LANDSCAPE_SAMPLE_2D(TexNormal2, Samp6, uv).xy;
    const float2 normal3 = LANDSCAPE_SAMPLE_2D(TexNormal3, Samp7, uv).xy;
    const float2 specular0 = LANDSCAPE_SAMPLE_2D(TexSpecular0, Samp8, uv).xy;
    const float2 specular1 = LANDSCAPE_SAMPLE_2D(TexSpecular1, Samp9, uv).xy;
    const float2 specular2 = LANDSCAPE_SAMPLE_2D(TexSpecular2, Samp10, uv).xy;
    const float2 specular3 = LANDSCAPE_SAMPLE_2D(TexSpecular3, Samp11, uv).xy;
#endif

#undef LANDSCAPE_SAMPLE_2D
#undef LANDSCAPE_SAMPLE_ARRAY

    AccumulateLandscapeLayer(
        input.layerWeights.x,
        diffuse0,
        normal0,
        specular0,
        diffuse,
        detailNormal,
        specular);
    AccumulateLandscapeLayer(
        input.layerWeights.y,
        diffuse1,
        normal1,
        specular1,
        diffuse,
        detailNormal,
        specular);
    AccumulateLandscapeLayer(
        input.layerWeights.z,
        diffuse2,
        normal2,
        specular2,
        diffuse,
        detailNormal,
        specular);
    AccumulateLandscapeLayer(
        input.layerWeights.w,
        diffuse3,
        normal3,
        specular3,
        diffuse,
        detailNormal,
        specular);

#if LINEAR_LIGHTING_LAND_LOD_BLEND
    const float2 landscapeLodBase =
        input.landscapeLodCoordinatesAndBlend.xy + cb0[0].zw;
    const float blendFactor = input.landscapeLodCoordinatesAndBlend.z;
    const float3 landscapeLodDiffuse =
        (TexLandscapeLodDiffuse.Sample(
            SampLandscapeLodDiffuse,
            landscapeLodBase * 0.00025).xyz * 3.777778) - 2.006;
#if LINEAR_LIGHTING_INSTANCED_LANDSCAPE
    const float3 lodMultiplier = AdjustLodAlbedo(SampleVariedLod(TexLodMultiplierLayers,
        Samp3,
        float3(
            input.lodMultiplierUv,
            float(layerIndices.lodTextureSlice))),true).xyz;
#else
    const float3 lodMultiplier = AdjustLodAlbedo(SampleVariedLod(TexLodMultiplier,
        SampLodMultiplier, input.lodMultiplierUv),true).xyz;
#endif
    diffuse = lerp(
        diffuse,
        lodMultiplier * landscapeLodDiffuse,
        blendFactor);

    const float2 landscapeLodNormalXY =
        (TexLandscapeLodNormal.Sample(
            SampLandscapeLodNormal,
            landscapeLodBase * 0.00035).xy * 2.0) - 1.0;
    const float landscapeLodNormalZ = sqrt(
        1.0 - min(
            dot(landscapeLodNormalXY, landscapeLodNormalXY),
            1.0));
    const float3 landscapeLodNormal = float3(
        landscapeLodNormalXY,
        landscapeLodNormalZ);
    const float3 detailBitangentBase = normalize(
        cross(float3(1.0, 0.0, 0.0), detailNormal));
    const float3 detailTangent = normalize(
        cross(detailBitangentBase, detailNormal));
    const float3 detailBitangent = normalize(
        cross(detailNormal, detailTangent));
    const float3 transformedLandscapeLodNormal = float3(
        dot(detailTangent, landscapeLodNormal),
        dot(detailBitangent, landscapeLodNormal),
        dot(normalize(detailNormal), landscapeLodNormal));
    detailNormal = normalize(lerp(
        detailNormal,
        transformedLandscapeLodNormal,
        blendFactor));
    specular = lerp(specular, cb0[0].yx, blendFactor);
#endif

    diffuse *= input.vertexColor.xyz;

    const float3 sourceNormal = normalize(input.normal);
    const float3 tangentNormal = float3(
        detailNormal.xy,
        input.isFrontFace ? detailNormal.z : -detailNormal.z);
    float3 projectedNormal;
    projectedNormal.x = dot(normalize(input.tangent), tangentNormal);
    projectedNormal.y = dot(normalize(input.bitangent), tangentNormal);
    projectedNormal.z = min(dot(sourceNormal, tangentNormal), 0.0);
    projectedNormal = normalize(projectedNormal);
    const float normalPackScale = sqrt(
        (projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    float4 layerProperties =
        max(cb2[4], 0.0) * input.layerWeights.x +
        max(cb2[5], 0.0) * input.layerWeights.y +
        max(cb2[6], 0.0) * input.layerWeights.z;
    layerProperties =
        layerProperties.zxyw +
        (max(cb2[7], 0.0).zxyw * input.layerWeights.w);

    float2 materialXY =
        ((cb2[2].xy - cb2[0].xy) * cb12[50].xx) + cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (cb2[2].xy >= 0.0) ? materialXY : cb2[0].xy;
    const float2 propertyBlend =
        (layerProperties.zy == -1.0) ? 0.0 : cb12[50].xx;
    materialXY = lerp(materialXY, layerProperties.zy, propertyBlend);

    const float layerPropertyX = saturate(layerProperties.x);
    const float specularBlend =
        (specular.x * (1.0 - (cb12[50].x * layerPropertyX))) +
        (cb12[50].x * layerPropertyX);
    output.target3.x = materialXY.x * specular.y;
    output.target3.y = materialXY.y * specularBlend;
    output.target3.z = cb2[0].w * 0.01;
    output.target3.w = 1.0;

    const float depthSwitch = (cb2[8].w < 0.0) ? 0.0 : cb12[50].x;
    const float depthRange = cb2[8].w - cb2[8].z;
    const float depthValue = (cb2[8].y != 0.0) ?
        (depthSwitch * depthRange) + cb2[8].z :
        depthSwitch * cb2[8].w;
    output.target2.x =
        (cb12[50].x != 0.0 && cb2[3].x != 0.0) ? cb2[3].x : 0.0;
    output.target2.y = cb2[8].x * 0.003922;
    output.target2.z = sqrt(depthValue * 0.02);
    output.target2.w = saturate(cb2[8].x);

    const float fade = (layerProperties.w == -1.0) ?
        1.0 : saturate(1.0 - (cb12[50].x * layerProperties.w));
    output.target0.xyz = fade * LinearLightingDecodedDiffuse(diffuse);
    output.target0.w = cb2[0].z;
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);

    const uint matrixBase = input.eyeIndex * 4u;
    const float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    const float currentW = dot(
        cb12[matrixBase + 66u], currentPosition);
    const float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;
    const float4 previousPosition = float4(
        input.previousPosition.xyz, 1.0);
    const float previousW = dot(
        cb12[matrixBase + 54u], previousPosition);
    const float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], previousPosition),
        dot(cb12[matrixBase + 52u], previousPosition)) / previousW;
    output.target5.xy =
        (currentNdc - previousNdc) * float2(-0.5, 0.5);
#if LINEAR_LIGHTING_SURFACE_CLASSIFICATION
    // Every shader produced from this reconstruction is a landscape contract.
    // Preserve the exact terrain class when Complex Parallax owns the draw.
    output.surfaceClass = 4.0 / 255.0;
    output.pbrMaterial = 0.0;
#endif
    return output;
}
