Texture2D<float4> NativeNormal : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
Texture2D<float4> NativeDepth : register(t3);
RWByteAddressBuffer SkylightingAmbientDiagnostic : register(u7);
SamplerState NativeDepthSampler : register(s3);

cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[61];
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    nointerpolation uint Eye : EYEINDEX;
};

struct PixelOutput
{
    float4 DiffuseVisibility : SV_Target0;
    float4 SpecularVisibility : SV_Target1;
};

#include "SkylightingSampling.hlsli"

float4 NativeClip(float2 screenUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = screenUv.x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    return float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) *
            (Stereo[0].x + 1.0f),
        1.0f - screenUv.y * 2.0f,
        nativeDepth,
        1.0f);
}

float3 ReconstructRelativeWorldPosition(
    float2 screenUv,
    float depth,
    uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(screenUv, depth, eye);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    const float3 viewPosition = homogeneous.xyz / homogeneous.w;
    const float3 worldRelativeToEye = float3(dot(Camera[20].xyz, viewPosition),
        dot(Camera[21].xyz, viewPosition), dot(Camera[22].xyz, viewPosition));
    return Camera[59u + eye].xyz + worldRelativeToEye - ProbeWorldOrigin.xyz;
}

float3 DecodeNativeNormal(float2 encoded)
{
    const float2 expanded = encoded * 4.0f - 2.0f;
    const float lengthSquared = dot(expanded, expanded);
    const float3 viewNormal = float3(expanded * sqrt(max(1.0f - lengthSquared * 0.25f, 0.0f)),
        -(1.0f - lengthSquared * 0.5f));
    return normalize(float3(dot(Camera[20].xyz, viewNormal),
        dot(Camera[21].xyz, viewNormal), dot(Camera[22].xyz, viewNormal)));
}

PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    output.DiffuseVisibility = 1.0f;
    output.SpecularVisibility = 1.0f;
    const uint2 diagnosticPixel = uint2(input.Position.xy);
    const bool diagnosticSample = Response.w > 0.5f &&
        all((diagnosticPixel & uint2(255u, 255u)) ==
            uint2(128u, 128u));
    uint ignored;
    if (diagnosticSample) {
        SkylightingAmbientDiagnostic.InterlockedAdd(0u, 1u, ignored);
    }
    if (Response.z > 0.5f) {
        uint depthWidth;
        uint depthHeight;
        NativeDepth.GetDimensions(depthWidth, depthHeight);
        const float2 screenUv = input.Position.xy /
            float2(depthWidth, depthHeight);
        const int2 depthPixel = clamp(
            int2(input.Position.xy),
            int2(0, 0),
            int2((int)depthWidth - 1, (int)depthHeight - 1));
        const float4 loadedDepth = NativeDepth.Load(
            int3(depthPixel, 0));
        if (diagnosticSample) {
            const float4 sampledDepth = NativeDepth.SampleLevel(
                NativeDepthSampler,
                screenUv,
                0.0f);
            SkylightingAmbientDiagnostic.InterlockedMin(
                56u, asuint(sampledDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                60u, asuint(sampledDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                64u, asuint(sampledDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                68u, asuint(sampledDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                72u, asuint(sampledDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                76u, asuint(sampledDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                80u, asuint(sampledDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                84u, asuint(sampledDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                88u, asuint(screenUv.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                92u, asuint(screenUv.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                96u, asuint(screenUv.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                100u, asuint(screenUv.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                104u, asuint(loadedDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                108u, asuint(loadedDepth.x), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                112u, asuint(loadedDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                116u, asuint(loadedDepth.y), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                120u, asuint(loadedDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                124u, asuint(loadedDepth.z), ignored);
            SkylightingAmbientDiagnostic.InterlockedMin(
                128u, asuint(loadedDepth.w), ignored);
            SkylightingAmbientDiagnostic.InterlockedMax(
                132u, asuint(loadedDepth.w), ignored);
        }
        const float depth = loadedDepth.x;
        if (depth > 1.0e-6f) {
            if (diagnosticSample) {
                SkylightingAmbientDiagnostic.InterlockedAdd(
                    4u, 1u, ignored);
            }
            const float3 relativeWorldPosition =
                ReconstructRelativeWorldPosition(
                    screenUv,
                    depth,
                    input.Eye);
            if (all(abs(relativeWorldPosition) <= 1.0e8f)) {
                if (diagnosticSample) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        8u, 1u, ignored);
                }
                uint normalWidth;
                uint normalHeight;
                NativeNormal.GetDimensions(normalWidth, normalHeight);
                const int2 normalPixel = clamp(
                    int2(screenUv * float2(normalWidth, normalHeight)),
                    int2(0, 0),
                    int2((int)normalWidth - 1, (int)normalHeight - 1));
                const float3 normal = DecodeNativeNormal(
                    NativeNormal.Load(int3(normalPixel, 0)).xy);
                float4 visibilitySh;
                float fade;
                bool insideVolume;
                bool weightedSample;
                const bool sampled = SampleSkylighting(
                    relativeWorldPosition,
                    normal,
                    visibilitySh,
                    fade,
                    insideVolume,
                    weightedSample);
                if (diagnosticSample && insideVolume) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        12u, 1u, ignored);
                }
                if (diagnosticSample && weightedSample) {
                    SkylightingAmbientDiagnostic.InterlockedAdd(
                        16u, 1u, ignored);
                }
                if (sampled) {
                    const float diffuseVisibility = lerp(
                        1.0f,
                        saturate(dot(
                            visibilitySh,
                            CosineLobe(normal)) / kPi),
                        fade);
                    const float3 viewDirection = normalize(
                        Camera[59u + input.Eye].xyz - ProbeWorldOrigin.xyz - relativeWorldPosition);
                    uint materialWidth;
                    uint materialHeight;
                    NativeMaterial.GetDimensions(
                        materialWidth,
                        materialHeight);
                    const int2 materialPixel = clamp(
                        int2(screenUv *
                            float2(materialWidth, materialHeight)),
                        int2(0, 0),
                        int2(
                            (int)materialWidth - 1,
                            (int)materialHeight - 1));
                    const float nativeGloss = saturate(
                        NativeMaterial.Load(int3(materialPixel, 0)).x);
                    const float roughness = saturate(1.0f - nativeGloss);
                    const float specularVisibility = lerp(
                        1.0f,
                        saturate(dot(
                            visibilitySh,
                            FauxSpecularLobe(
                                normal,
                                viewDirection,
                                roughness))),
                        fade);
                    output.DiffuseVisibility = lerp(
                        Response.x,
                        1.0f,
                        diffuseVisibility);
                    output.SpecularVisibility = lerp(
                        Response.y,
                        1.0f,
                        specularVisibility);
                    if (diagnosticSample) {
                        if (fade > 1.0e-3f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                20u, 1u, ignored);
                        }
                        if (output.DiffuseVisibility.x < 0.999f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                24u, 1u, ignored);
                        }
                        if (output.SpecularVisibility.x < 0.999f) {
                            SkylightingAmbientDiagnostic.InterlockedAdd(
                                28u, 1u, ignored);
                        }
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            32u,
                            asuint(output.DiffuseVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            36u,
                            asuint(output.DiffuseVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            40u,
                            asuint(output.SpecularVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            44u,
                            asuint(output.SpecularVisibility.x),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMin(
                            48u,
                            asuint(fade),
                            ignored);
                        SkylightingAmbientDiagnostic.InterlockedMax(
                            52u,
                            asuint(fade),
                            ignored);
                    }
                }
            }
        }
    }
    return output;
}
