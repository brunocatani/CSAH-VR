#ifndef LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 0
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

#ifndef LINEAR_LIGHTING_MENU_SCREEN
#define LINEAR_LIGHTING_MENU_SCREEN 0
#endif

#ifndef LINEAR_LIGHTING_PIPBOY_SCREEN
#define LINEAR_LIGHTING_PIPBOY_SCREEN 0
#endif

#ifndef LINEAR_LIGHTING_FACE_DETAIL
#define LINEAR_LIGHTING_FACE_DETAIL 0
#endif

#ifndef LINEAR_LIGHTING_SKIN_TINT
#define LINEAR_LIGHTING_SKIN_TINT 0
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

#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && LINEAR_LIGHTING_LANDSCAPE_LOD
#error Additional alpha masking and landscape LOD use incompatible t15 contracts.
#endif

#if LINEAR_LIGHTING_GRADIENT_REMAP && LINEAR_LIGHTING_LANDSCAPE_LOD
#error Combined gradient-remap and landscape-LOD layouts require separate verified contracts.
#endif

#if LINEAR_LIGHTING_MENU_SCREEN && LINEAR_LIGHTING_PIPBOY_SCREEN
#error Menu-screen and Pip-Boy-screen shaders use distinct material contracts.
#endif

#if LINEAR_LIGHTING_PIPBOY_SCREEN && LINEAR_LIGHTING_LANDSCAPE_LOD
#error Pip-Boy-screen and landscape-LOD shaders use incompatible b0 contracts.
#endif

#if LINEAR_LIGHTING_SKIN_TINT && LINEAR_LIGHTING_GRADIENT_REMAP
#error Skin-tint and gradient-remap shaders use distinct cb2[2] contracts.
#endif

#if LINEAR_LIGHTING_BONE_TINTING && \
    (LINEAR_LIGHTING_LANDSCAPE_LOD || \
    LINEAR_LIGHTING_MENU_SCREEN || LINEAR_LIGHTING_PIPBOY_SCREEN)
#error Six-MRT bone tinting cannot share these material layouts.
#endif

#if LINEAR_LIGHTING_GRADIENT_HAIR && \
    (!LINEAR_LIGHTING_HAIR || !LINEAR_LIGHTING_GRADIENT_REMAP)
#error Six-MRT gradient hair requires both hair and gradient remapping.
#endif

#if LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_GRADIENT_REMAP && \
    !LINEAR_LIGHTING_GRADIENT_HAIR
#error Six-MRT gradient-remap hair requires its verified output-alpha contract.
#endif

#if LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_BONE_TINTING && \
    !LINEAR_LIGHTING_GRADIENT_REMAP
#error Six-MRT hair and bone tinting require the verified gradient-remap layout.
#endif

#if LINEAR_LIGHTING_HAIR && \
    (LINEAR_LIGHTING_LANDSCAPE_LOD || \
    LINEAR_LIGHTING_MENU_SCREEN || LINEAR_LIGHTING_PIPBOY_SCREEN || \
    LINEAR_LIGHTING_FACE_DETAIL || LINEAR_LIGHTING_SKIN_TINT || \
    LINEAR_LIGHTING_TEXTURED_EMISSION)
#error Six-MRT hair cannot share these material contracts.
#endif

cbuffer PerMaterial : register(b2)
{
#if LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)
    float4 cb2[9];
#elif (LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK) || \
    (LINEAR_LIGHTING_BONE_TINTING && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)) || \
    (LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT || \
    LINEAR_LIGHTING_HAIR))
    float4 cb2[8];
#elif LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK || LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_BONE_TINTING || LINEAR_LIGHTING_HAIR
    float4 cb2[7];
#else
    float4 cb2[6];
#endif
};

#if LINEAR_LIGHTING_LANDSCAPE_LOD
cbuffer LandscapeLodGlobals : register(b0)
{
    float4 cb0[1];
};
#elif LINEAR_LIGHTING_PIPBOY_SCREEN
cbuffer PipboyScreenGlobals : register(b0)
{
    float4 cb0[1];
};
#endif

#include "../LinearLighting/LinearLighting.hlsli"

#ifndef LINEAR_LIGHTING_COMPLEX_ENVIRONMENT
#define LINEAR_LIGHTING_COMPLEX_ENVIRONMENT 0
#endif

#if LINEAR_LIGHTING_COMPLEX_ENVIRONMENT
#if LINEAR_LIGHTING_HAIR || LINEAR_LIGHTING_FACE_DETAIL || \
    LINEAR_LIGHTING_SKIN_TINT || LINEAR_LIGHTING_PIPBOY_SCREEN
#error Complex environment response requires the ordinary opaque material tag.
#endif
#include "../ComplexMaterials/ComplexEnvironmentMaterials.hlsli"
#endif

cbuffer PerGeometry : register(b12)
{
    float4 cb12[71];
};

#ifndef LINEAR_LIGHTING_INSTANCED
#define LINEAR_LIGHTING_INSTANCED 0
#endif

#if LINEAR_LIGHTING_INSTANCED
cbuffer PerInstance : register(b13)
{
    float4 cb13[900];
};
#endif

Texture2D<float4> TexDiffuse : register(t0);
Texture2D<float4> TexNormal : register(t1);
#if LINEAR_LIGHTING_HAIR
Texture2D<float4> TexHairDirection : register(t3);
#else
Texture2D<float4> TexSpecular : register(t2);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
Texture2D<float4> TexAdditionalAlpha : register(t12);
Texture2D<float4> TexAdditionalAlphaNoise : register(t15);
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD
Texture2D<float4> TexLandscapeLodDiffuse : register(t13);
Texture2D<float4> TexLandscapeLodNormal : register(t15);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
Texture2D<float4> TexGradientRemap : register(t5);
#endif
#if LINEAR_LIGHTING_MENU_SCREEN || LINEAR_LIGHTING_PIPBOY_SCREEN
Texture2D<float4> TexScreen : register(t4);
#endif
#if LINEAR_LIGHTING_FACE_DETAIL
Texture2D<float4> TexFaceDetail : register(t8);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
Texture2D<float4> TexBoneTintLookup : register(t13);
Texture2D<float4> TexBoneTintPalette : register(t14);
#endif

#if LINEAR_LIGHTING_TEXTURED_EMISSION
Texture2D<float4> TexGlow : register(t3);
#endif

SamplerState SampDiffuse : register(s0);
SamplerState SampNormal : register(s1);
#if LINEAR_LIGHTING_HAIR
SamplerState SampHairDirection : register(s3);
#else
SamplerState SampSpecular : register(s2);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
SamplerState SampAdditionalAlpha : register(s12);
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD
SamplerState SampLandscapeLodDiffuse : register(s13);
SamplerState SampLandscapeLodNormal : register(s15);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
SamplerState SampGradientRemap : register(s5);
#endif
#if LINEAR_LIGHTING_MENU_SCREEN || LINEAR_LIGHTING_PIPBOY_SCREEN
SamplerState SampScreen : register(s4);
#endif
#if LINEAR_LIGHTING_FACE_DETAIL
SamplerState SampFaceDetail : register(s8);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
SamplerState SampBoneTintLookup : register(s13);
SamplerState SampBoneTintPalette : register(s14);
#endif
#if LINEAR_LIGHTING_TEXTURED_EMISSION
SamplerState SampGlow : register(s3);
#endif

#ifndef LINEAR_LIGHTING_VERTEX_COLOR
#define LINEAR_LIGHTING_VERTEX_COLOR 0
#endif

#ifndef LINEAR_LIGHTING_VERTEX_ALPHA
#define LINEAR_LIGHTING_VERTEX_ALPHA LINEAR_LIGHTING_VERTEX_COLOR
#endif

#ifndef LINEAR_LIGHTING_FORCE_EARLY_DEPTH
#define LINEAR_LIGHTING_FORCE_EARLY_DEPTH 1
#endif

#ifndef LINEAR_LIGHTING_ALPHA_TEST
#define LINEAR_LIGHTING_ALPHA_TEST 0
#endif

#ifndef LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 0
#endif

#ifndef LINEAR_LIGHTING_TESSELLATED_INPUTS
#define LINEAR_LIGHTING_TESSELLATED_INPUTS 0
#endif

#if LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[6]
#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[7]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[8]
#elif LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_GRADIENT_REMAP && \
    LINEAR_LIGHTING_BONE_TINTING
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[6]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]
#elif LINEAR_LIGHTING_HAIR && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[6]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]
#elif LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[2]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[4]
#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[5]
#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[6]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]
#elif LINEAR_LIGHTING_HAIR
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]
#elif LINEAR_LIGHTING_BONE_TINTING && \
    (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[6]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]
#elif LINEAR_LIGHTING_BONE_TINTING
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[2]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[4]
#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[5]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]
#elif LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[6]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]
#elif LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[3]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[5]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]
#elif LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[2]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[4]
#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[5]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]
#else
#define LINEAR_LIGHTING_MATERIAL_INTERPOLATION cb2[2]
#define LINEAR_LIGHTING_MATERIAL_PROPERTIES cb2[4]
#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[5]
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
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
#else
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if LINEAR_LIGHTING_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#endif
#if LINEAR_LIGHTING_PIPBOY_SCREEN
    float3 screenDirection : TEXCOORD6;
#elif LINEAR_LIGHTING_FACE_DETAIL
    float faceFactor : TEXCOORD6;
#endif
#if LINEAR_LIGHTING_BONE_TINTING
    float4 boneTintColor : COLOR1;
#endif
#if LINEAR_LIGHTING_INSTANCED
    uint instanceDataIndex : COLOR2;
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD && !LINEAR_LIGHTING_INSTANCED
    float2 landscapeLodCoordinates : TEXCOORD9;
#endif
    uint eyeIndex : EYEINDEX;
#if LINEAR_LIGHTING_LANDSCAPE_LOD && LINEAR_LIGHTING_INSTANCED
    float2 landscapeLodCoordinates : TEXCOORD9;
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
    float2 uv = float2(input.currentPosition.w, input.previousPosition.w);
#endif
#if LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK
    if (LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.y != 0.0)
    {
        float2 coordinateSign = (input.position.xy >= -input.position.xy) ?
            float2(1.0, 1.0) : float2(-1.0, -1.0);
        int2 noiseCoordinate = int2(
            frac(input.position.xy * coordinateSign * 0.25) *
            coordinateSign * 4.0);
        float noise = TexAdditionalAlphaNoise.Load(
            int3(noiseCoordinate, 0)).x;
        clip((LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.y * (0.5 - noise)) +
            LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.z - 0.5);
    }
    if (LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.w != 0.0)
    {
        float additionalAlpha = TexAdditionalAlpha.Sample(
            SampAdditionalAlpha, uv).w;
        clip(LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.x - additionalAlpha);
    }
#endif
#if LINEAR_LIGHTING_ALPHA_TEST
    float4 diffuseSample = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, uv));
    float alpha = diffuseSample.w;
#if LINEAR_LIGHTING_VERTEX_ALPHA
    alpha *= input.vertexColor.w;
#endif
    clip(alpha - cb2[1].w);
#endif
#if LINEAR_LIGHTING_GRADIENT_REMAP
#if LINEAR_LIGHTING_ALPHA_TEST
    float gradientRemapSource = diffuseSample.y;
#else
    float gradientRemapSource = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, uv)).y;
#endif
    float gradientRemapRow = cb2[2].x;
#if LINEAR_LIGHTING_VERTEX_COLOR
    gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0;
#endif
    float4 gradientRemap = TexGradientRemap.SampleLevel(
        SampGradientRemap,
        float2(pow(gradientRemapSource, 0.454545), gradientRemapRow),
        0.0);
    float3 diffuse = gradientRemap.xyz;
#elif LINEAR_LIGHTING_ALPHA_TEST
    float3 diffuse = diffuseSample.xyz;
#else
    float3 diffuse = AdjustMaterialLodAlbedo(SampleMaterialTerrainLod(TexDiffuse,SampDiffuse, uv)).xyz;
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD
    float2 landscapeLodBase = input.landscapeLodCoordinates + cb0[0].zw;
    float3 landscapeLodDiffuse = TexLandscapeLodDiffuse.Sample(
        SampLandscapeLodDiffuse, landscapeLodBase * 0.00025).xyz;
    landscapeLodDiffuse = (landscapeLodDiffuse * 3.777778) - 2.006;
    diffuse *= landscapeLodDiffuse;
#endif
#if LINEAR_LIGHTING_VERTEX_COLOR && !LINEAR_LIGHTING_GRADIENT_REMAP
    diffuse *= input.vertexColor.xyz;
#endif
#if LINEAR_LIGHTING_SKIN_TINT
    float3 skinTintGamma = pow(abs(cb2[2].xyz), 0.454545);
    float3 skinBaseGamma = pow(abs(diffuse), 0.454545);
    float3 skinTintDark =
        (2.0 * skinBaseGamma * skinTintGamma) +
        (skinBaseGamma * skinBaseGamma * (1.0 - (2.0 * skinTintGamma)));
    float3 skinTintLight =
        (sqrt(skinBaseGamma) * ((2.0 * skinTintGamma) - 1.0)) +
        (2.0 * skinBaseGamma * (1.0 - skinTintGamma));
    float3 skinTintBlend =
        (skinTintGamma < 0.5) ? skinTintDark : skinTintLight;
    float3 tintedDiffuse = pow(abs(skinTintBlend), 2.2);
    diffuse = lerp(diffuse, tintedDiffuse, cb2[2].w);
#endif
#if LINEAR_LIGHTING_FACE_DETAIL
    float4 faceDetail = TexFaceDetail.Sample(SampFaceDetail, uv);
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
    float faceDiffuseMask = faceDetail.w * 2.0 - 1.0;
#else
    float faceDiffuseMask = faceDetail.w;
#endif
    diffuse *= 1.0 - ((1.0 - faceDiffuseMask) * input.faceFactor * 0.3);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
    float4 boneTintLookup = TexBoneTintLookup.Sample(SampBoneTintLookup, uv);
    float4 boneTintPalette = TexBoneTintPalette.Sample(
        SampBoneTintPalette,
        float2(
            boneTintLookup.y,
            frac(LINEAR_LIGHTING_BONE_TINT_ROW.x)));
    float3 boneTint = LinearLightingDiffuse(boneTintPalette.xyz) *
        boneTintPalette.w * boneTintLookup.w * input.boneTintColor.w * 4.0;
#endif
    float fade = (LINEAR_LIGHTING_MATERIAL_PROPERTIES.w == -1.0) ?
        1.0 :
        ((-LINEAR_LIGHTING_MATERIAL_PROPERTIES.w * cb12[50].x) + 1.0);
#if LINEAR_LIGHTING_MENU_SCREEN
    float3 menuScreen = TexScreen.Sample(SampScreen, uv).xyz;
    output.target0.xyz = fade * (
        LinearLightingDecodedDiffuse(diffuse) + LinearLightingDiffuse(menuScreen));
#elif !LINEAR_LIGHTING_PIPBOY_SCREEN
    output.target0.xyz = fade * LinearLightingDecodedDiffuse(diffuse);
#endif
#if LINEAR_LIGHTING_BONE_TINTING
    output.target0.xyz += boneTint;
#endif
#if LINEAR_LIGHTING_GRADIENT_HAIR
    output.target0.w = gradientRemapSource;
#elif LINEAR_LIGHTING_HAIR
    output.target0.w = 0.0;
#else
    output.target0.w = cb2[0].z;
#endif

    float3 sourceNormal = normalize(input.normal);
#if LINEAR_LIGHTING_HAIR
    float3 hairDirection = normalize(
        (TexHairDirection.Sample(SampHairDirection, uv).xyz * 2.0) - 1.0);
#else
    float4 specularMaterialSample =
        TexSpecular.Sample(SampSpecular, uv);
    float2 specularSample = specularMaterialSample.xy;
#if LINEAR_LIGHTING_COMPLEX_ENVIRONMENT
    ComplexEnvironmentMaterial complexEnvironmentMaterial =
        DecodeComplexEnvironmentMaterial(
            TexSpecular,
            SampSpecular,
            uv,
            specularMaterialSample);
    output.target0.xyz *= complexEnvironmentMaterial.diffuseScale;
#endif
#endif
#if LINEAR_LIGHTING_LANDSCAPE_LOD
    float2 landscapeLodNormalXY =
        (TexLandscapeLodNormal.Sample(
            SampLandscapeLodNormal, landscapeLodBase * 0.00035).xy * 2.0) -
        1.0;
    float landscapeLodNormalZ = sqrt(
        1.0 - min(dot(landscapeLodNormalXY, landscapeLodNormalXY), 1.0));
    float3 landscapeLodNormal = float3(
        landscapeLodNormalXY, landscapeLodNormalZ);
#if LINEAR_LIGHTING_MODEL_SPACE_NORMALS
    float3 detailNormal =
        (TexNormal.Sample(SampNormal, uv).xzy * 2.0) - 1.0;
#else
    float2 detailNormalXY =
        (TexNormal.Sample(SampNormal, uv).xy * 2.0) - 1.0;
    float detailNormalZ = sqrt(
        1.0 - min(dot(detailNormalXY, detailNormalXY), 1.0));
    float3 detailNormal = float3(detailNormalXY, detailNormalZ);
#endif
    float3 detailBitangent = normalize(cross(float3(1.0, 0.0, 0.0), detailNormal));
    float3 detailTangent = normalize(cross(detailBitangent, detailNormal));
    float detailNormalContribution = dot(normalize(detailNormal), landscapeLodNormal);
    float3 tangentNormal = float3(
        dot(detailTangent, landscapeLodNormal),
        dot(detailBitangent, landscapeLodNormal),
        input.isFrontFace ? detailNormalContribution : -detailNormalContribution);
#elif LINEAR_LIGHTING_MODEL_SPACE_NORMALS
#if LINEAR_LIGHTING_FACE_DETAIL
    float3 detailModelNormal =
        (TexNormal.Sample(SampNormal, uv).xzy * 2.0) - 1.0;
    float3 faceModelNormal = (faceDetail.xzy * 2.0) - 1.0;
    float3 modelNormal = lerp(
        detailModelNormal,
        faceModelNormal,
        input.faceFactor);
#else
    float3 modelNormal = (TexNormal.Sample(SampNormal, uv).xyz * 2.0) - 1.0;
#endif
    float3 tangentNormal = float3(
#if LINEAR_LIGHTING_FACE_DETAIL
        modelNormal.x,
        modelNormal.y,
        input.isFrontFace ? modelNormal.z : -modelNormal.z);
#else
        modelNormal.x,
        modelNormal.z,
        input.isFrontFace ? modelNormal.y : -modelNormal.y);
#endif
#else
    float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;
    float2 tangentNormalXY = (normalSample * 2.0) - 1.0;
    float tangentNormalZ = sqrt(1.0 - min(dot(tangentNormalXY, tangentNormalXY), 1.0));
#if LINEAR_LIGHTING_FACE_DETAIL
    float2 faceNormalXY = (faceDetail.xy * 2.0) - 1.0;
    float faceNormalZ = sqrt(
        1.0 - min(dot(faceNormalXY, faceNormalXY), 1.0));
    float3 faceNormal = normalize(lerp(
        float3(0.0, 0.0, 1.0),
        float3(faceNormalXY, faceNormalZ),
        input.faceFactor));
    float3 combinedNormal = normalize(float3(
        faceNormal.xy + tangentNormalXY,
        tangentNormalZ));
    float3 tangentNormal = float3(
        combinedNormal.xy,
        input.isFrontFace ? combinedNormal.z : -combinedNormal.z);
#else
    float3 tangentNormal = float3(tangentNormalXY, input.isFrontFace ? tangentNormalZ : -tangentNormalZ);
#endif
#endif

#if LINEAR_LIGHTING_PIPBOY_SCREEN
    float3 screenDirection = normalize(-input.screenDirection);
    float screenDepth = dot(screenDirection, tangentNormal);
    float2 screenOffset =
        (-screenDirection.xy / screenDepth) + (tangentNormal.xy * 2.0);
    screenOffset *= cb0[0].x;
    float2 screenUv = uv +
        (float2(screenOffset.x, -screenOffset.y) * cb0[0].y);
    float3 pipboyScreen = pow(TexScreen.Sample(SampScreen, screenUv).xyz, 2.2);
    output.target0.xyz =
        (fade * LinearLightingDecodedDiffuse(diffuse)) +
        (pipboyScreen * cb0[0].z);
#endif

    float3 projectedNormal;
    projectedNormal.z = min(dot(sourceNormal, tangentNormal), 0.0);
    projectedNormal.x = dot(normalize(input.tangent), tangentNormal);
    projectedNormal.y = dot(normalize(input.bitangent), tangentNormal);
    projectedNormal = normalize(projectedNormal);

    float normalPackScale = sqrt((projectedNormal.z * -8.0) + 8.0);
    output.target1.xy = (projectedNormal.xy / normalPackScale) + 0.5;

    float depthRange = LINEAR_LIGHTING_DEPTH_PARAMETERS.w -
        LINEAR_LIGHTING_DEPTH_PARAMETERS.z;
    float depthSwitch = (LINEAR_LIGHTING_DEPTH_PARAMETERS.w < 0.0) ?
        0.0 : cb12[50].x;
    float depthValue = (depthSwitch * depthRange) +
        LINEAR_LIGHTING_DEPTH_PARAMETERS.z;
    float depthAlt = depthSwitch * LINEAR_LIGHTING_DEPTH_PARAMETERS.w;
    depthValue = (LINEAR_LIGHTING_DEPTH_PARAMETERS.y != 0.0) ?
        depthValue : depthAlt;
    output.target2.z = sqrt(depthValue * 0.02);

    bool materialFlag =
        (cb12[50].x != 0.0 && LINEAR_LIGHTING_MATERIAL_PROPERTIES.y != 0.0) ||
        LINEAR_LIGHTING_MATERIAL_PROPERTIES.x != 0.0;
    output.target2.x = materialFlag ? 1.0 : 0.0;
    output.target2.y = LINEAR_LIGHTING_DEPTH_PARAMETERS.x * 0.003922;
    output.target2.w = saturate(LINEAR_LIGHTING_DEPTH_PARAMETERS.x);

#if LINEAR_LIGHTING_HAIR
    output.target3.x = dot(normalize(input.tangent), hairDirection);
    output.target3.y = dot(normalize(input.bitangent), hairDirection);
    output.target3.z = dot(sourceNormal, hairDirection);
    float3 emitColor = cb2[1].xyz;
#else
    float specBlendA =
        cb12[50].x * LINEAR_LIGHTING_MATERIAL_PROPERTIES.z;
    float specBlendB =
        (-LINEAR_LIGHTING_MATERIAL_PROPERTIES.z * cb12[50].x) + 1.0;
    float specBlend = (specularSample.x * specBlendB) + specBlendA;
#if LINEAR_LIGHTING_GRADIENT_REMAP
    float specularMask = specularSample.y * gradientRemap.w;
#else
    float specularMask = specularSample.y;
#endif

    float3 emitColor;
#if LINEAR_LIGHTING_INSTANCED
    uint instanceBase = input.instanceDataIndex * 6u;
    float4 instanceMaterial = cb13[instanceBase + 4u];
    output.target0.w = instanceMaterial.z;
    output.target3.x = instanceMaterial.x * specularMask;
    output.target3.y = instanceMaterial.y * specBlend;
    output.target3.z = instanceMaterial.w * 0.01;
    emitColor = cb13[instanceBase + 5u].xyz;
#else
    float2 materialXY =
        ((LINEAR_LIGHTING_MATERIAL_INTERPOLATION.xy - cb2[0].xy) *
            cb12[50].xx) +
        cb2[0].xy;
    materialXY *= cb2[0].xy;
    materialXY = (LINEAR_LIGHTING_MATERIAL_INTERPOLATION.xy >= 0.0) ?
        materialXY :
        cb2[0].xy;

    output.target3.y = materialXY.y * specBlend;
    output.target3.x = materialXY.x * specularMask;
    output.target3.z = cb2[0].w * 0.01;
    emitColor = cb2[1].xyz;
#endif
#endif
#if LINEAR_LIGHTING_HAIR
    output.target3.w = 0.003922;
#elif LINEAR_LIGHTING_FACE_DETAIL || LINEAR_LIGHTING_SKIN_TINT
    output.target3.w = 0.019608;
#elif LINEAR_LIGHTING_PIPBOY_SCREEN
    output.target3.w = 0.015686;
#else
#if LINEAR_LIGHTING_COMPLEX_ENVIRONMENT
    output.target3.w = complexEnvironmentMaterial.encodedTag;
#else
    output.target3.w = 1.0;
#endif
#endif
#if LINEAR_LIGHTING_PIPBOY_SCREEN
#if LINEAR_LIGHTING_TEXTURED_EMISSION
    float3 glow = LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz);
    output.target4.xyz =
        (pipboyScreen * cb0[0].w) +
        (LinearLightingEmitColor(emitColor) * glow);
#else
    output.target4.xyz =
        (pipboyScreen * cb0[0].w) + LinearLightingEmitColor(emitColor);
#endif
#elif LINEAR_LIGHTING_TEXTURED_EMISSION
    float3 glow = LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz);
    output.target4.xyz = LinearLightingEmitColor(emitColor) * glow;
#else
    output.target4.xyz = LinearLightingEmitColor(emitColor);
#endif

    uint matrixBase = input.eyeIndex * 4u;
    float4 currentPosition = float4(input.currentPosition.xyz, 1.0);
    float currentW = dot(cb12[matrixBase + 66u], currentPosition);
    float2 currentNdc = float2(
        dot(cb12[matrixBase + 63u], currentPosition),
        dot(cb12[matrixBase + 64u], currentPosition)) / currentW;

#if LINEAR_LIGHTING_TESSELLATED_INPUTS
    float4 previousPosition = currentPosition;
#else
    float4 previousPosition = float4(input.previousPosition.xyz, 1.0);
#endif
    float previousW = dot(cb12[matrixBase + 54u], previousPosition);
    float2 previousNdc = float2(
        dot(cb12[matrixBase + 51u], previousPosition),
        dot(cb12[matrixBase + 52u], previousPosition)) / previousW;

    output.target5.xy = (currentNdc - previousNdc) * float2(-0.5, 0.5);
    return output;
}
