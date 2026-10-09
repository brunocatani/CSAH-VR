#include "PbrCommon.hlsli"

Texture2D<float4> GBufferAlbedo : register(t0);
Texture2D<float4> GBufferMaterial : register(t2);
Texture2D<float4> AuthoredPbrMaterial : register(t45);
Texture2D<float> SurfaceClass : register(t47);

cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer PbrSettings : register(b7)
{
    float4 PbrFeatureParams0;
    float4 PbrFeatureParams1;
    float4 PbrMaterialParams0;
    float4 PbrMaterialParams1;
};

cbuffer BasicWetnessSettings : register(b9)
{
    float4 BasicWetnessParams;
    float4 BasicWetnessMaterialParams;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    uint Eye : EYEINDEX;
    float4 VanillaDiffuse : TEXCOORD0;
    float4 VanillaSpecular : TEXCOORD1;
    float4 NormalCarrier : TEXCOORD2;
    float4 ViewCarrier : TEXCOORD3;
};

struct PixelOutput
{
    float4 Diffuse : SV_Target0;
    float4 Specular : SV_Target1;
    float4 DiffuseScale : SV_Target2;
};

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    output.Diffuse = input.VanillaDiffuse;
    output.Specular = input.VanillaSpecular;
    output.DiffuseScale = 1.0f;

    [branch]
    if (PbrFeatureParams0.x > 1.0f / 255.0f &&
        PbrFeatureParams0.z > 0.5f)
    {
        const int2 pixel = int2(input.Position.xy);
        const float4 material = GBufferMaterial.Load(int3(pixel, 0));
        const float4 authoredMaterial =
            AuthoredPbrMaterial.Load(int3(pixel, 0));
        const float authoredActive = step(0.5f, authoredMaterial.x);
        const float authoredRoughness = saturate(
            authoredMaterial.x * 2.0f - 1.0f);
        const float surfaceCode =
            SurfaceClass.Load(int3(pixel, 0)) * 255.0f;
        const float ordinary = 1.0f - step(0.5f, abs(surfaceCode));
        const float terrain = 1.0f - step(
            0.5f, abs(surfaceCode - 4.0f));
        const float grass = 1.0f - step(
            0.5f, abs(surfaceCode - 1.0f));
        const float supportedSurface = saturate(
            ordinary + terrain + grass * saturate(PbrFeatureParams0.w));
        const float wettableSurface = saturate(ordinary + terrain + grass);
        const float wetness = saturate(
            BasicWetnessParams.x * BasicWetnessParams.y * wettableSurface);
        const float ordinaryMaterialTag = step(0.5f, material.w);
        const float legacyMetalness = PbrDecodeMetalness(material.w);
        const float metalness = lerp(
            legacyMetalness,
            saturate(authoredMaterial.y),
            authoredActive);
        const float complexMaterial = step(
            1.0f / 255.0f, legacyMetalness);
        const float legacyActive = ordinaryMaterialTag * saturate(
            complexMaterial + saturate(PbrFeatureParams0.y));
        const float active = supportedSurface * saturate(
            authoredActive + legacyActive);

        [branch]
        if (active > 1.0f / 255.0f)
        {
            const float3 retainedDiffuse = max(
                GBufferAlbedo.Load(int3(pixel, 0)).rgb,
                0.0f);
            const float3 legacyBaseColour = retainedDiffuse /
                max(1.0f - metalness, 1.0f / 255.0f);
            // The legacy complex-material producer already removes the
            // metallic diffuse share before this pass. Authored RMAOS is an
            // independent material overlay, so its retained G-buffer albedo
            // is still the unattenuated base colour.
            const float3 baseColour = lerp(
                legacyBaseColour,
                retainedDiffuse,
                authoredActive);
            const float dielectricF0 = PbrDielectricF0(
                lerp(material.y, authoredMaterial.w, authoredActive),
                PbrMaterialParams0.z,
                PbrMaterialParams0.w,
                PbrMaterialParams1.x);
            const float3 f0 = lerp(
                dielectricF0.xxx,
                saturate(baseColour * PbrMaterialParams1.y),
                metalness);
            float roughness = PbrMaterialRoughness(
                PbrRoughnessFromPhong(material.x) * 7.0f,
                material.x,
                material.y,
                PbrMaterialParams0.x,
                PbrMaterialParams0.y);
            roughness = lerp(
                roughness,
                clamp(
                    authoredRoughness *
                        max(PbrMaterialParams0.x, 0.0f),
                    PbrMinimumRoughness,
                    1.0f),
                authoredActive);
            roughness = clamp(
                roughness * lerp(
                    1.0f,
                    saturate(BasicWetnessMaterialParams.x),
                    wetness),
                PbrMinimumRoughness,
                1.0f);
            const float3 normal = PbrSafeNormalize(
                input.NormalCarrier.xyz,
                float3(0.0f, 0.0f, 1.0f));
            const float3 viewDirection = PbrSafeNormalize(
                input.ViewCarrier.xzw,
                normal);
            // Keep both native stereo constants immediate-indexed. The DXBC
            // transform remaps top-level temporaries, but a relative
            // constant-buffer index owns a nested operand; retaining that
            // template register would index b2 with view-direction bits.
            const float3 eyeLightDirection = input.Eye == 0u ?
                DFLight[1].xyz : DFLight[2].xyz;
            const float3 lightDirection = PbrSafeNormalize(
                eyeLightDirection,
                normal);
            const float3 halfDirection = PbrSafeNormalize(
                viewDirection + lightDirection,
                normal);
            const float normalDotLight = saturate(
                dot(normal, lightDirection));
            const float normalDotView = max(
                saturate(abs(dot(normal, viewDirection))), PbrEpsilon);
            const float normalDotHalf = saturate(
                dot(normal, halfDirection));
            const float viewDotHalf = saturate(
                dot(viewDirection, halfDirection));
            const float3 fresnel = PbrFresnelSchlick(f0, viewDotHalf);
            const float distribution = PbrDistributionGgx(
                roughness, normalDotHalf);
            const float visibility = PbrVisibilitySmithJointApprox(
                roughness, normalDotView, normalDotLight);
            float3 ggxSpecular = distribution * visibility * fresnel *
                normalDotLight * max(DFLight[3].rgb, 0.0f) *
                max(PbrMaterialParams1.z, 0.0f) *
                lerp(
                    1.0f,
                    max(BasicWetnessParams.w, 0.0f),
                    wetness);
            [branch]
            if (PbrFeatureParams1.z > 0.5f)
            {
                const float2 environmentBrdf = PbrEnvironmentBrdf(
                    roughness, normalDotView);
                ggxSpecular *= 1.0f + f0 *
                    (1.0f / max(
                        environmentBrdf.x + environmentBrdf.y,
                        PbrEpsilon) - 1.0f);
            }

            output.Specular.xyz = lerp(
                input.VanillaSpecular.xyz,
                ggxSpecular,
                active);
            output.Diffuse.xyz *= lerp(
                1.0f.xxx,
                lerp(
                    1.0f.xxx,
                    (1.0f - metalness).xxx,
                    authoredActive),
                active);
            output.DiffuseScale.xyz *= lerp(
                1.0f.xxx,
                lerp(
                    1.0f.xxx,
                    (1.0f - metalness).xxx,
                    authoredActive),
                active);
            [branch]
            if (PbrFeatureParams1.y > 0.5f)
            {
                output.Diffuse.xyz *= lerp(
                    1.0f.xxx,
                    saturate(1.0f.xxx - fresnel),
                    active);
                output.DiffuseScale.xyz *= lerp(
                    1.0f.xxx,
                    saturate(1.0f.xxx - fresnel),
                    active);
            }
        }
    }
    return output;
}
