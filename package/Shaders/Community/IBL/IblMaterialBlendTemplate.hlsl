#include "../Common/Fo4VrDepth.hlsli"

// Shared with the directional DFLight transform so direct and environment
// material response use the same GGX/Fresnel approximations.
#include "../PBR/PbrCommon.hlsli"

TextureCubeArray<float4> VanillaEnvironment : register(t8);
Texture2D<float3> DFLightAlbedo : register(t29);
TextureCube<float3> PublishedEnvironment : register(t30);
TextureCube<float> PublishedValidity : register(t31);
TextureCube<float3> PreviousPublishedEnvironment : register(t32);
TextureCube<float> PreviousPublishedValidity : register(t33);
TextureCube<float4> PublishedPosition : register(t34);
TextureCube<float4> PreviousPublishedPosition : register(t35);
Texture2D<float4> GBufferMaterial : register(t36);
Texture2D<float4> CubemapStyle : register(t37);
Texture2D<float4> AuthoredPbrMaterial : register(t45);
Texture2D<float> SurfaceClass : register(t47);
Texture2D<float> SceneDepth : register(t7);
#ifndef PBR_NATIVE_OCCLUSION
#define PBR_NATIVE_OCCLUSION 1
#endif

#if PBR_NATIVE_OCCLUSION
Texture2D<float4> NativeOcclusion : register(t9);
#endif
SamplerState EnvironmentSampler : register(s8);
SamplerState MaterialSampler : register(s3);
#if PBR_NATIVE_OCCLUSION
SamplerState OcclusionSampler : register(s9);
#endif

cbuffer IblMaterialConstants : register(b5)
{
    float IblWeight : packoffset(c0.x);
    float ComplexMaterialWeight : packoffset(c0.y);
    float EnvironmentTransitionWeight : packoffset(c0.z);
    float PreviousEnvironmentAvailable : packoffset(c0.w);
    float3 PublishedProbeOrigin : packoffset(c1.x);
    float PublishedProbeOriginValid : packoffset(c1.w);
    float3 PreviousPublishedProbeOrigin : packoffset(c2.x);
    float PreviousPublishedProbeOriginValid : packoffset(c2.w);
    // c3: enabled, legacy materials, direct GGX, grass GGX.
    float4 PbrFeatureParams0 : packoffset(c3);
    // c4: environment Fresnel, energy conservation, multiscatter,
    // specular occlusion.
    float4 PbrFeatureParams1 : packoffset(c4);
    // c5: roughness multiplier, specular roughness blend, base F0
    // multiplier, minimum F0.
    float4 PbrMaterialParams0 : packoffset(c5);
    // c6: cubemap-to-F0 multiplier, complex-material F0 multiplier,
    // direct-light scale, reserved.
    float4 PbrMaterialParams1 : packoffset(c6);
};

// Every exact FO4VR DFComposite material contract owns t7 and b12. The
// generator extends its declaration to 82 rows. Rows 32..39 are the two
// inverse projections, rows 59/60 are the live camera-relative eye origins,
// rows 0..2 transform world offsets to view space, rows 20..22 transform view
// vectors back to the environment convention, and c80/c81 provide the
// persistent per-eye position adjustment.
cbuffer Fo4VrSceneConstants : register(b12)
{
    float4 Scene[77];
};

cbuffer BasicWetnessSettings : register(b9)
{
    // x=enabled, y=wetness, z=diffuse darkening, w=specular multiplier.
    float4 BasicWetnessParams;
    // x=IBL roughness/LOD scale. Remaining values are reserved.
    float4 BasicWetnessMaterialParams;
};

struct PixelInput
{
    float4 DirectionAndArray : TEXCOORD0;
    float Lod : TEXCOORD1;
    float EncodedMaterialTag : TEXCOORD2;
    float2 ScreenUv : TEXCOORD3;
};

struct PixelOutput
{
    float4 Reflection : SV_Target0;
    float4 ReflectionLobe : SV_Target1;
};

bool ReconstructReceiverWorldPosition(
    float2 packedUv,
    out float3 receiverWorldPosition,
    out float3 viewDirection)
{
    receiverWorldPosition = 0.0f;
    viewDirection = float3(0.0f, 0.0f, 1.0f);
    const float depth = SceneDepth.SampleLevel(
        MaterialSampler,
        packedUv,
        0.0f);
    if (!(depth == depth && depth >= 0.0f && depth <= 1.0f))
    {
        return false;
    }

    const uint eye = packedUv.x >= 0.5f ? 1u : 0u;
    const float localX = frac(packedUv.x * 2.0f);
    const float mappedDepth = Fo4VrProjectionDepth(depth);
    const float4 clipPosition = float4(
        localX * 2.0f - 1.0f,
        1.0f - packedUv.y * 2.0f,
        mappedDepth,
        1.0f);
    const uint inverseProjectionBase = Fo4VrInverseProjectionBase(depth, eye);
    float4 homogeneousView;
    homogeneousView.x = dot(
        Scene[inverseProjectionBase], clipPosition);
    homogeneousView.y = dot(
        Scene[inverseProjectionBase + 1u], clipPosition);
    homogeneousView.z = dot(
        Scene[inverseProjectionBase + 2u], clipPosition);
    homogeneousView.w = dot(
        Scene[inverseProjectionBase + 3u], clipPosition);
    if (!(abs(homogeneousView.w) > 1.0e-5f))
    {
        return false;
    }

    const float3 leftOrigin = Scene[59].xyz;
    const float3 rightOrigin = Scene[60].xyz;
    const float3 midpointOrigin = (leftOrigin + rightOrigin) * 0.5f;
    const float3 eyeOrigin = eye == 0u ? leftOrigin : rightOrigin;
    const float3 eyeToMidpointWorld = eyeOrigin - midpointOrigin;
    const float3 eyeToMidpointView = float3(
        dot(Scene[0].xyz, eyeToMidpointWorld),
        dot(Scene[1].xyz, eyeToMidpointWorld),
        dot(Scene[2].xyz, eyeToMidpointWorld));
    const float3 midpointRelativeView =
        homogeneousView.xyz / homogeneousView.w + eyeToMidpointView;
    const float3 midpointRelativeWorld = float3(
        dot(Scene[20].xyz, midpointRelativeView),
        dot(Scene[21].xyz, midpointRelativeView),
        dot(Scene[22].xyz, midpointRelativeView));
    receiverWorldPosition = midpointOrigin + midpointRelativeWorld;
    const float3 eyeVector = eyeOrigin - receiverWorldPosition;
    const float eyeDistanceSquared = dot(eyeVector, eyeVector);
    if (!(all(receiverWorldPosition == receiverWorldPosition) &&
          all(abs(receiverWorldPosition) < 8000000.0f) &&
          eyeDistanceSquared > 1.0e-5f))
    {
        return false;
    }
    viewDirection = eyeVector * rsqrt(eyeDistanceSquared);
    return all(viewDirection == viewDirection);
}

float3 CorrectProbeDirection(
    TextureCube<float4> positionTexture,
    float3 direction,
    float3 receiverWorldPosition,
    float3 probeOrigin,
    float roughnessLod,
    float originValid)
{
    if (!(originValid > 0.5f))
    {
        return direction;
    }

    const float4 hit = positionTexture.SampleLevel(
        EnvironmentSampler,
        direction,
        0.0f);
    const float3 probeToHit = hit.xyz - probeOrigin;
    const float radius = length(probeToHit);
    const float3 probeToReceiver = receiverWorldPosition - probeOrigin;
    const float receiverDistanceSquared = dot(
        probeToReceiver,
        probeToReceiver);
    const float projection = dot(probeToReceiver, direction);
    const float discriminant = projection * projection -
        (receiverDistanceSquared - radius * radius);
    if (!(hit.w > 1.0e-4f && radius > 16.5f &&
          receiverDistanceSquared < radius * radius &&
          discriminant > 1.0e-4f))
    {
        return direction;
    }

    const float travel = -projection + sqrt(discriminant);
    const float3 corrected = normalize(
        probeToReceiver + direction * travel);
    const float correctionStrength = hit.w *
        (1.0f - saturate(roughnessLod / 7.0f));
    return normalize(lerp(direction, corrected, correctionStrength));
}

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    const float surfaceClass = SurfaceClass.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0);
    const float surfaceCode = surfaceClass * 255.0;
    const float ordinaryOrGrass = 1.0 - step(1.5, surfaceCode);
    const float ordinarySurface = 1.0 - step(
        0.5f, abs(surfaceCode));
    const float grassSurface = 1.0 - step(
        0.5f, abs(surfaceCode - 1.0f));
    const float terrain = 1.0 - step(0.5, abs(surfaceCode - 4.0));
    const float wetness = saturate(
        BasicWetnessParams.x * BasicWetnessParams.y *
        saturate(ordinaryOrGrass + terrain));
    const float wetLod = input.Lod * lerp(
        1.0,
        saturate(BasicWetnessMaterialParams.x),
        wetness);
    const float ordinaryMaterialTag = step(
        0.5f,
        input.EncodedMaterialTag);
    const float4 authoredMaterial = AuthoredPbrMaterial.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0f);
    const float authoredActive = step(0.5f, authoredMaterial.x);
    const float authoredRoughness = saturate(
        authoredMaterial.x * 2.0f - 1.0f);
    const float legacyMetalness = PbrDecodeMetalness(
        input.EncodedMaterialTag) * ComplexMaterialWeight;
    const float metalness = lerp(
        legacyMetalness,
        saturate(authoredMaterial.y),
        authoredActive);
    const float complexMaterial = step(
        1.0f / 255.0f, legacyMetalness);
    const float supportedSurface = saturate(
        ordinarySurface + terrain +
        grassSurface * saturate(PbrFeatureParams0.w));
    float4 material = 0.0f;
    float pbrActive = 0.0f;
    float pbrRoughness = saturate(wetLod / 7.0f);
    float environmentLod = wetLod;
    [branch]
    if (PbrFeatureParams0.x > 1.0f / 255.0f &&
        PbrFeatureParams1.x > 0.5f)
    {
        material = GBufferMaterial.SampleLevel(
            MaterialSampler,
            input.ScreenUv,
            0.0f);
        const float legacyActive = ordinaryMaterialTag * saturate(
            complexMaterial + saturate(PbrFeatureParams0.y));
        pbrActive = supportedSurface * saturate(
            authoredActive + legacyActive);
        pbrRoughness = PbrMaterialRoughness(
            wetLod,
            material.x,
            material.y,
            PbrMaterialParams0.x,
            PbrMaterialParams0.y);
        pbrRoughness = lerp(
            pbrRoughness,
            clamp(
                authoredRoughness * max(PbrMaterialParams0.x, 0.0f),
                PbrMinimumRoughness,
                1.0f),
            authoredActive);
        environmentLod = lerp(
            wetLod,
            pbrRoughness * 7.0f,
            pbrActive);
    }
    float3 receiverWorldPosition;
    float3 viewDirection;
    const bool receiverPositionValid =
        ReconstructReceiverWorldPosition(
            input.ScreenUv,
            receiverWorldPosition,
            viewDirection);
    float4 vanilla = VanillaEnvironment.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray,
        environmentLod);
    [branch]
    if (IblWeight > 1.0 / 255.0)
    {
        // FO4VR's vanilla cube coordinate is an incident lookup direction.
        // The view-derived environment stores outgoing world directions, so
        // dynamic specular consumption must use the opposite direction. This
        // sign is deliberately local to specular materials; Diffuse IBL keeps
        // the provider's proven world-direction convention.
        const float3 dynamicDirection = -input.DirectionAndArray.xyz;
        const float3 publishedDirection = receiverPositionValid ?
            CorrectProbeDirection(
                PublishedPosition,
                dynamicDirection,
                receiverWorldPosition,
                PublishedProbeOrigin,
                environmentLod,
                PublishedProbeOriginValid) : dynamicDirection;
        float3 published = PublishedEnvironment.SampleLevel(
            EnvironmentSampler,
            publishedDirection,
            environmentLod);
        float validity = PublishedValidity.SampleLevel(
            EnvironmentSampler,
            publishedDirection,
            environmentLod);
        [branch]
        if (PreviousEnvironmentAvailable > 0.5 &&
            EnvironmentTransitionWeight < 1.0)
        {
            const float3 previousDirection = receiverPositionValid ?
                CorrectProbeDirection(
                    PreviousPublishedPosition,
                    dynamicDirection,
                    receiverWorldPosition,
                    PreviousPublishedProbeOrigin,
                    environmentLod,
                    PreviousPublishedProbeOriginValid) : dynamicDirection;
            const float3 previousPublished =
                PreviousPublishedEnvironment.SampleLevel(
                    EnvironmentSampler,
                    previousDirection,
                    environmentLod);
            const float previousValidity =
                PreviousPublishedValidity.SampleLevel(
                    EnvironmentSampler,
                    previousDirection,
                    environmentLod);
            published = lerp(
                previousPublished,
                published,
                saturate(EnvironmentTransitionWeight));
            validity = lerp(
                previousValidity,
                validity,
                saturate(EnvironmentTransitionWeight));
        }
        uint styleWidth, styleHeight;
        CubemapStyle.GetDimensions(styleWidth, styleHeight);
        const uint cube = (uint)max(input.DirectionAndArray.w, 0.0f);
        const bool styleValid = styleWidth == 2u && cube < styleHeight;
        const float4 tint = CubemapStyle.Load(int3(0, cube, 0));
        const float intensity = CubemapStyle.Load(int3(1, cube, 0)).x;
        if (tint.w >= 0.0f)
        {
            const float3 weights = float3(0.2126f, 0.7152f, 0.0722f);
            const float luminance = dot(published, weights);
            float3 colored = published * tint.rgb;
            colored *= luminance / max(dot(colored, weights), 0.0001f);
            published = lerp(luminance.xxx, colored, saturate(tint.w)) * intensity;
        }
        const float styleWeight = styleValid && tint.w > -1.5f ? 1.0f : 0.0f;
        float weight = saturate(validity * IblWeight) * styleWeight;
        vanilla.xyz = lerp(vanilla.xyz, published, weight);
    }

    float3 retainedDiffuse = DFLightAlbedo.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0f);
    const float3 legacyBaseColour = retainedDiffuse /
        max(1.0 - metalness, 1.0 / 255.0);
    // Authored RMAOS overlays a vanilla material, whose retained albedo has
    // not yet been divided into metallic and diffuse shares.
    float3 baseColour = lerp(
        legacyBaseColour,
        retainedDiffuse,
        authoredActive);
    float3 reflectionLobe = 1.0f;
    [branch]
    if (pbrActive > 1.0f / 255.0f)
    {
        const float dielectricF0 = PbrDielectricF0(
            lerp(material.y, authoredMaterial.w, authoredActive),
            PbrMaterialParams0.z,
            PbrMaterialParams0.w,
            PbrMaterialParams1.x);
        const float3 f0 = lerp(
            dielectricF0.xxx,
            saturate(baseColour * PbrMaterialParams1.y),
            metalness);
        float normalDotView = 0.5f;
        if (receiverPositionValid)
        {
            const float3 reflectionDirection =
                -input.DirectionAndArray.xyz;
            const float3 reconstructedNormal = normalize(
                reflectionDirection + viewDirection);
            normalDotView = saturate(abs(dot(
                reconstructedNormal,
                viewDirection)));
        }
        const float2 environmentBrdf = PbrEnvironmentBrdf(
            pbrRoughness,
            normalDotView);
        reflectionLobe = f0 * environmentBrdf.x +
            environmentBrdf.y;
        if (PbrFeatureParams1.w > 0.5f)
        {
#if PBR_NATIVE_OCCLUSION
            const float nativeAmbientOcclusion = saturate(
                NativeOcclusion.SampleLevel(
                    OcclusionSampler,
                    input.ScreenUv,
                    0.0f).x);
            const float ambientOcclusion = lerp(
                nativeAmbientOcclusion,
                saturate(authoredMaterial.z),
                authoredActive);
#else
            const float ambientOcclusion = lerp(
                1.0f,
                saturate(authoredMaterial.z),
                authoredActive);
#endif
            reflectionLobe *= PbrSpecularOcclusion(
                normalDotView,
                pbrRoughness,
                ambientOcclusion);
        }
    }
    else if (metalness > 1.0f / 255.0f)
    {
        reflectionLobe = lerp(1.0f.xxx, baseColour, metalness);
    }
    reflectionLobe *= lerp(
        1.0,
        max(BasicWetnessParams.w, 0.0),
        wetness);
    vanilla.xyz *= max(reflectionLobe, 0.0f);
    output.Reflection = vanilla;
    output.ReflectionLobe = float4(max(reflectionLobe, 0.0f), 1.0f);
    return output;
}
