#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
#endif

#ifndef LINEAR_LIGHTING_LOD_OBJECT_ALPHA
#define LINEAR_LIGHTING_LOD_OBJECT_ALPHA 0
#endif

#ifndef LINEAR_LIGHTING_LANDSCAPE_LOD
#define LINEAR_LIGHTING_LANDSCAPE_LOD 0
#endif

#ifndef LINEAR_LIGHTING_GRADIENT_REMAP
#define LINEAR_LIGHTING_GRADIENT_REMAP 0
#endif

#ifndef LINEAR_LIGHTING_GRADIENT_HAIR
#define LINEAR_LIGHTING_GRADIENT_HAIR 0
#endif

#ifndef LINEAR_LIGHTING_BONE_TINTING
#define LINEAR_LIGHTING_BONE_TINTING 0
#endif

#ifndef LINEAR_LIGHTING_HAIR
#define LINEAR_LIGHTING_HAIR 0
#endif

#ifndef LINEAR_LIGHTING_TEXTURED_EMISSION
#define LINEAR_LIGHTING_TEXTURED_EMISSION 0
#endif

#ifndef LINEAR_LIGHTING_MENU_SCREEN
#define LINEAR_LIGHTING_MENU_SCREEN 0
#endif

#ifndef LINEAR_LIGHTING_SKIN_TINT
#define LINEAR_LIGHTING_SKIN_TINT 0
#endif

#ifndef LINEAR_LIGHTING_TESSELLATED_INPUTS
#define LINEAR_LIGHTING_TESSELLATED_INPUTS 0
#endif

#if LINEAR_LIGHTING_GRADIENT_HAIR && !LINEAR_LIGHTING_GRADIENT_REMAP
#error Gradient hair requires the verified gradient-remap material layout.
#endif

#if LINEAR_LIGHTING_SKIN_TINT && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_BONE_TINTING || \
    LINEAR_LIGHTING_HAIR || LINEAR_LIGHTING_LANDSCAPE_LOD || \
    LINEAR_LIGHTING_LOD_OBJECT_ALPHA || LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK)
#error Projected skin tint requires its verified standalone material layout.
#endif

#if LINEAR_LIGHTING_MENU_SCREEN && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_BONE_TINTING || \
    LINEAR_LIGHTING_HAIR || LINEAR_LIGHTING_LANDSCAPE_LOD || \
    LINEAR_LIGHTING_LOD_OBJECT_ALPHA || LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || \
    LINEAR_LIGHTING_TEXTURED_EMISSION)
#error Projected menu screen requires its verified standalone material layout.
#endif

#if LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && \
    LINEAR_LIGHTING_GRADIENT_REMAP
#error Combined projected bone tinting, additional alpha, and gradient remapping require a separately verified contract.
#endif

#if LINEAR_LIGHTING_LOD_OBJECT_ALPHA && LINEAR_LIGHTING_GRADIENT_REMAP
#error Combined LOD-object alpha and gradient remap require a separately verified contract.
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#error Landscape LOD and additional alpha use incompatible t15 contracts.
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_BONE_TINTING
#error Landscape LOD and bone tinting use incompatible t13 contracts.
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_GRADIENT_REMAP
#error Combined landscape LOD and gradient remap require a separately verified contract.
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_LOD_OBJECT_ALPHA
#error Landscape LOD and LOD-object alpha require a separately verified contract.
#endif

#if LINEAR_LIGHTING_HAIR && \
    (LINEAR_LIGHTING_LOD_OBJECT_ALPHA || LINEAR_LIGHTING_LANDSCAPE_LOD || \
    LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_GRADIENT_HAIR || \
    LINEAR_LIGHTING_BONE_TINTING || LINEAR_LIGHTING_TEXTURED_EMISSION)
#error Standalone projected hair requires its verified material contract.
#endif

cbuffer PerMaterial : register(b2)
{
#if (LINEAR_LIGHTING_GRADIENT_REMAP && \
    (LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || LINEAR_LIGHTING_BONE_TINTING)) || \
    (LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK) || \
    (LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK)
    float4 cb2[9];
#elif LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || \
    LINEAR_LIGHTING_BONE_TINTING || LINEAR_LIGHTING_HAIR || \
    LINEAR_LIGHTING_SKIN_TINT
    float4 cb2[8];
#else
    float4 cb2[7];
#endif
};

#if LINEAR_LIGHTING_LANDSCAPE_LOD
cbuffer LandscapeLodGlobals : register(b0)
{
    float4 cb0[1];
};
#endif

#include "../LinearLighting/LinearLighting.hlsli"

cbuffer PerGeometry : register(b12)
{
    float4 cb12[51];
};

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
Texture2D<float4> TexSpecular : register(t2);
#if LINEAR_LIGHTING_LANDSCAPE_LOD
Texture2D<float4> TexLandscapeLodDiffuse : register(t13);
Texture2D<float4> TexLandscapeLodNormal : register(t15);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
Texture2D<float4> TexAdditionalAlpha : register(t12);
Texture2D<float4> TexAdditionalAlphaNoise : register(t15);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
Texture2D<float4> TexGradientRemap : register(t5);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
Texture2D<float4> TexBoneTintLookup : register(t13);
Texture2D<float4> TexBoneTintPalette : register(t14);
#endif

#if LINEAR_LIGHTING_TEXTURED_EMISSION
Texture2D<float4> TexGlow : register(t3);
#endif
#if LINEAR_LIGHTING_MENU_SCREEN
Texture2D<float4> TexScreen : register(t4);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
SamplerState SampSpecular : register(s2);
#if LINEAR_LIGHTING_LANDSCAPE_LOD
SamplerState SampLandscapeLodDiffuse : register(s13);
SamplerState SampLandscapeLodNormal : register(s15);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
SamplerState SampGradientRemap : register(s5);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
SamplerState SampBoneTintLookup : register(s13);
SamplerState SampBoneTintPalette : register(s14);
#endif
#if LINEAR_LIGHTING_TEXTURED_EMISSION
SamplerState SampGlow : register(s3);
#endif
#if LINEAR_LIGHTING_MENU_SCREEN
SamplerState SampScreen : register(s4);
#endif

#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_VERTEX_ALPHA
#define LINEAR_LIGHTING_VERTEX_ALPHA LINEAR_LIGHTING_VERTEX_COLOR
#endif

#ifndef LINEAR_LIGHTING_ALPHA_TEST
#define LINEAR_LIGHTING_ALPHA_TEST 0
#endif

#ifndef LINEAR_LIGHTING_NORMAL_XY
#define LINEAR_LIGHTING_NORMAL_XY 1
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#error Projected landscape LOD with model-space normals requires a separately verified contract.
#endif

#ifndef LINEAR_LIGHTING_FORCE_EARLY_DEPTH
#define LINEAR_LIGHTING_FORCE_EARLY_DEPTH 1
#endif

#if LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[6]
#define LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW cb2[7]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[8]
#elif LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[7]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[8]
#elif LINEAR_LIGHTING_HAIR
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]
#elif LINEAR_LIGHTING_GRADIENT_REMAP && LINEAR_LIGHTING_BONE_TINTING
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW cb2[7]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[8]
#elif LINEAR_LIGHTING_GRADIENT_REMAP && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[7]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[8]
#elif LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]
#elif LINEAR_LIGHTING_BONE_TINTING
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW cb2[6]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]
#elif LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[6]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]
#else
#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[6]
#endif

struct PSInput
{
    float4 position : SV_POSITION;
#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float2 uv : TEXCOORD0;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
#else
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 texCoord3 : TEXCOORD3;
    float4 texCoord4 : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#endif
#if LINEAR_LIGHTING_BONE_TINTING
    float4 boneTintColor : COLOR1;
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD
    float2 landscapeLodCoordinates : TEXCOORD9;
#endif
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

#if LINEAR_LIGHTING_FORCE_EARLY_DEPTH
[earlydepthstencil]
#endif
PSOutput PSMain(PSInput input)
{
    PSOutput output;

#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float2 uv = input.uv;
#else
    float2 uv = float2(input.texCoord3.w, input.texCoord4.w);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    if (LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.y != 0.0)
    {
        float2 coordinateSign = (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.y * (0.5 - noise)) +
            LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.z - 0.5);
    }
    if (LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.w != 0.0)
    {
        float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, uv).w;
        clip(LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.x - additionalAlpha);
    }
#endif
    float4 diffuse = AdjustMaterialLodAlbedo(TexDiffuse.Sample(SampDiffuse, uv));
#if LINEAR_LIGHTING_VERTEX_COLOR
#if LINEAR_LIGHTING_HAIR
#if LINEAR_LIGHTING_VERTEX_ALPHA
    diffuse.w *= input.vertexColor.w;
#endif
#elif LINEAR_LIGHTING_GRADIENT_REMAP
#if LINEAR_LIGHTING_VERTEX_ALPHA
    diffuse.w *= input.vertexColor.w;
#endif
#else
#if LINEAR_LIGHTING_VERTEX_ALPHA
    diffuse *= input.vertexColor;
#else
    diffuse.xyz *= input.vertexColor.xyz;
#endif
#endif
#endif
#if LINEAR_LIGHTING_ALPHA_TEST
    clip(diffuse.w - cb2[1].w);
#endif
    float alphaMask = (cb2[2].y == 1.0) ? diffuse.w : 1.0;
    float alpha = alphaMask * cb2[2].x;

    clip((cb2[2].x * alphaMask) - 0.015686);

#if LINEAR_LIGHTING_GRADIENT_REMAP
    float gradientRemapRow = cb2[3].x;
#if LINEAR_LIGHTING_VERTEX_COLOR
    gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0;
#endif
    float4 gradientRemap = TexGradientRemap.SampleLevel(
        SampGradientRemap,
        float2(pow(diffuse.y, 0.454545), gradientRemapRow),
        0.0);
    float3 mappedDiffuse = gradientRemap.xyz;
#if LINEAR_LIGHTING_GRADIENT_HAIR
    mappedDiffuse *= diffuse.y * 1.8;
#endif
#else
    float3 mappedDiffuse = diffuse.xyz;
#endif

#if LINEAR_LIGHTING_SKIN_TINT
    float3 skinTintGamma = pow(abs(cb2[3].xyz), 0.454545);
    float3 skinBaseGamma = pow(abs(mappedDiffuse), 0.454545);
    float3 skinTintDark =
        (2.0 * skinBaseGamma * skinTintGamma) +
        (skinBaseGamma * skinBaseGamma * (1.0 - (2.0 * skinTintGamma)));
    float3 skinTintLight =
        (sqrt(skinBaseGamma) * ((2.0 * skinTintGamma) - 1.0)) +
        (2.0 * skinBaseGamma * (1.0 - skinTintGamma));
    float3 skinTintBlend =
        (skinTintGamma < 0.5) ? skinTintDark : skinTintLight;
    float3 tintedDiffuse = pow(abs(skinTintBlend), 2.2);
    mappedDiffuse = lerp(mappedDiffuse, tintedDiffuse, cb2[3].w);
#endif

#if LINEAR_LIGHTING_LANDSCAPE_LOD
    float2 landscapeLodBase = input.landscapeLodCoordinates + cb0[0].zw;
    float3 landscapeLodDiffuse = TexLandscapeLodDiffuse.Sample(
        SampLandscapeLodDiffuse, landscapeLodBase * 0.00025).xyz;
    landscapeLodDiffuse = (landscapeLodDiffuse * 3.777778) - 2.006;
    mappedDiffuse *= landscapeLodDiffuse;
#endif

#if LINEAR_LIGHTING_BONE_TINTING
    float4 boneTintLookup = TexBoneTintLookup.Sample(SampBoneTintLookup, uv);
    float4 boneTintPalette = TexBoneTintPalette.Sample(
        SampBoneTintPalette,
        float2(
            boneTintLookup.y,
            frac(LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW.x)));
    float3 boneTint = LinearLightingDiffuse(boneTintPalette.xyz) *
        boneTintPalette.w * boneTintLookup.w * input.boneTintColor.w * 4.0;
#endif

    float fade = (LINEAR_LIGHTING_PROJECTED_PROPERTIES.w == -1.0) ?
        1.0 :
        ((-LINEAR_LIGHTING_PROJECTED_PROPERTIES.w * cb12[50].x) + 1.0);
#if LINEAR_LIGHTING_HAIR
    output.target0.xyz = float3(0.0, 0.0, 0.0);
#elif LINEAR_LIGHTING_MENU_SCREEN
    float3 menuScreen = TexScreen.Sample(SampScreen, uv).xyz;
    output.target0.xyz = fade * (
        LinearLightingDecodedDiffuse(mappedDiffuse) +
        LinearLightingDiffuse(menuScreen));
#else
    output.target0.xyz = fade * LinearLightingDecodedDiffuse(mappedDiffuse);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
    output.target0.xyz += boneTint;
#endif
    output.target0.w = alpha;

    float3 sourceNormal = normalize(input.normal);
    float2 specularSample = TexSpecular.Sample(SampSpecular, uv).xy;
#if LINEAR_LIGHTING_LANDSCAPE_LOD
    float2 landscapeLodNormalXY =
        (TexLandscapeLodNormal.Sample(
            SampLandscapeLodNormal, landscapeLodBase * 0.00035).xy * 2.0) -
        1.0;
    float landscapeLodNormalZ = sqrt(
        1.0 - min(dot(landscapeLodNormalXY, landscapeLodNormalXY), 1.0));
    float3 landscapeLodNormal = float3(
        landscapeLodNormalXY, landscapeLodNormalZ);
    float2 detailNormalXY =
        (TexNormal.Sample(SampNormal, uv).xy * 2.0) - 1.0;
    float detailNormalZ = sqrt(
        1.0 - min(dot(detailNormalXY, detailNormalXY), 1.0));
    float3 detailNormal = float3(detailNormalXY, detailNormalZ);
    float3 detailBitangent = normalize(
        cross(float3(1.0, 0.0, 0.0), detailNormal));
    float3 detailTangent = normalize(cross(detailBitangent, detailNormal));
    float detailNormalContribution =
        dot(normalize(detailNormal), landscapeLodNormal);
    float3 tangentNormal = float3(
        dot(detailTangent, landscapeLodNormal),
        dot(detailBitangent, landscapeLodNormal),
        input.isFrontFace ?
            detailNormalContribution : -detailNormalContribution);
#elif LINEAR_LIGHTING_MODEL_SPACE_NORMALS
    float3 modelNormal = (TexNormal.Sample(SampNormal, uv).xyz * 2.0) - 1.0;
    float3 tangentNormal = float3(
        modelNormal.x,
        modelNormal.z,
        input.isFrontFace ? modelNormal.y : -modelNormal.y);
#else
#if LINEAR_LIGHTING_NORMAL_XY
    float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;
#else
    float2 normalSample = TexNormal.Sample(SampNormal, uv).zw;
#endif
    float2 tangentNormalXY = (normalSample * 2.0) - 1.0;
    float tangentNormalZ = sqrt(1.0 - min(dot(tangentNormalXY, tangentNormalXY), 1.0));
    float3 tangentNormal = float3(tangentNormalXY, input.isFrontFace ? tangentNormalZ : -tangentNormalZ);
#endif

    float3 projectedNormal;
    projectedNormal.z = min(dot(sourceNormal, tangentNormal), 0.0);
    projectedNormal.x = dot(normalize(input.tangent), tangentNormal);
    projectedNormal.y = dot(normalize(input.bitangent), tangentNormal);
    projectedNormal = normalize(projectedNormal);

    float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;
    output.target1.z = -projectedNormal.z;
    output.target1.w = alpha;

    float depthRange = LINEAR_LIGHTING_PROJECTED_DEPTH.w -
        LINEAR_LIGHTING_PROJECTED_DEPTH.z;
    float depthSwitch = (LINEAR_LIGHTING_PROJECTED_DEPTH.w < 0.0) ?
        0.0 : cb12[50].x;
    float depthValue = (depthSwitch * depthRange) +
        LINEAR_LIGHTING_PROJECTED_DEPTH.z;
    float depthAlt = depthSwitch * LINEAR_LIGHTING_PROJECTED_DEPTH.w;
    depthValue = (LINEAR_LIGHTING_PROJECTED_DEPTH.y != 0.0) ?
        depthValue : depthAlt;
    output.target2.z = sqrt(depthValue * 0.02);

    bool materialFlag =
        (cb12[50].x != 0.0 && LINEAR_LIGHTING_PROJECTED_PROPERTIES.y != 0.0) ||
        LINEAR_LIGHTING_PROJECTED_PROPERTIES.x != 0.0;
    output.target2.x = materialFlag ? 1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_PROJECTED_DEPTH.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_PROJECTED_DEPTH.x);

#if LINEAR_LIGHTING_LOD_OBJECT_ALPHA
    output.target3.w = pow(alpha, 0.1);
#elif LINEAR_LIGHTING_GRADIENT_HAIR || LINEAR_LIGHTING_HAIR
    output.target3.w = saturate(max(alpha, 0.019608));
#elif LINEAR_LIGHTING_SKIN_TINT
    output.target3.w = 0.019608;
#else
    output.target3.w = alpha;
#endif
    output.target4.w = alpha;

    float specBlendA =
        cb12[50].x * LINEAR_LIGHTING_PROJECTED_PROPERTIES.z;
    float specBlendB =
        (-LINEAR_LIGHTING_PROJECTED_PROPERTIES.z * cb12[50].x) + 1.0;
    float specBlend = (specularSample.x * specBlendB) + specBlendA;

    float2 materialXY =
        ((LINEAR_LIGHTING_PROJECTED_INTERPOLATION.xy - cb2[0].xy) *
            cb12[50].xx) +
        cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (LINEAR_LIGHTING_PROJECTED_INTERPOLATION.xy >= 0.0) ?
        materialXY : cb2[0].xy;

    output.target3.y = materialXY.y * specBlend;
#if LINEAR_LIGHTING_GRADIENT_REMAP
    output.target3.x = materialXY.x * specularSample.y * gradientRemap.w;
#else
    output.target3.x = materialXY.x * specularSample.y;
#endif
    output.target3.z = cb2[0].w * 0.01;
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    float3 glow = LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz);
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(cb2[1].xyz);
#endif

    return output;
}
