#include "VolumetricCommon.hlsli"

cbuffer VolumetricTemporal : register(b4)
{
    float4 PreviousEyeOrigin[2];
    row_major float4x4 PreviousViewProjection[2];
    float4 TemporalParams;
};

Texture2D<float> CurrentVolume : register(t0);
Texture2D<float> CurrentDepth : register(t1);
Texture2D<float> PreviousVolume : register(t2);
Texture2D<float> PreviousDepth : register(t3);
SamplerState HistorySampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelOutput
{
    float volume : SV_Target0;
    float depth : SV_Target1;
};

PixelOutput main(PixelInput input)
{
    uint width;
    uint height;
    CurrentDepth.GetDimensions(width, height);
    const int2 coordinate = min(
        int2(input.position.xy),
        int2(width - 1u, height - 1u));
    const uint eye = coordinate.x < int(width >> 1u) ? 0u : 1u;
    PixelOutput output;
    output.volume = max(CurrentVolume.Load(int3(coordinate, 0)), 0.0f);
    output.depth = CurrentDepth.Load(int3(coordinate, 0));
    if (TemporalParams.x < 0.5f || output.depth >= 0.999999f) {
        return output;
    }
    const float2 packedUv =
        (float2(coordinate) + 0.5f) / float2(width, height);

    const float3 currentRelative = ReconstructRelativePosition(
        packedUv, eye, output.depth);
    const float3 absolutePosition = currentRelative + EyeOrigin[eye].xyz;
    const float3 previousRelative =
        absolutePosition - PreviousEyeOrigin[eye].xyz;
    const float4 previousClip = mul(
        float4(previousRelative, 1.0f),
        PreviousViewProjection[eye]);
    if (!Finite3(previousRelative) || !Finite(previousClip.w) ||
        previousClip.w <= 1.0e-5f) {
        return output;
    }
    const float3 previousNdc = previousClip.xyz / previousClip.w;
    const float2 previousEyeUv = float2(
        previousNdc.x * 0.5f + 0.5f,
        0.5f - previousNdc.y * 0.5f);
    if (!Finite3(previousNdc) || any(previousEyeUv < 0.0f) ||
        any(previousEyeUv > 1.0f)) {
        return output;
    }
    float2 previousPackedUv = float2(
        ((float)eye + previousEyeUv.x) * 0.5f,
        previousEyeUv.y);
    const float2 halfTexel = 0.5f / float2(width, height);
    previousPackedUv.x = clamp(
        previousPackedUv.x,
        eye == 0u ? halfTexel.x : 0.5f + halfTexel.x,
        eye == 0u ? 0.5f - halfTexel.x : 1.0f - halfTexel.x);
    previousPackedUv.y = clamp(
        previousPackedUv.y, halfTexel.y, 1.0f - halfTexel.y);
    const int2 previousCoordinate = min(
        int2(previousPackedUv * float2(width, height)),
        int2(width - 1u, height - 1u));
    const float previousDepth = PreviousDepth.Load(
        int3(previousCoordinate, 0));
    if (!Finite(previousDepth) || previousDepth >= 0.999999f) {
        return output;
    }
    const float expectedDepth = previousNdc.z;
    const float observedDepth = NativeDepth(previousDepth);
    const float confidence = 1.0f - smoothstep(
        TemporalParams.z,
        TemporalParams.w,
        abs(expectedDepth - observedDepth));
    const float historyWeight = saturate(TemporalParams.y * confidence);
    const float history = max(
        PreviousVolume.SampleLevel(
            HistorySampler, previousPackedUv, 0.0f),
        0.0f);
    output.volume = lerp(output.volume, history, historyWeight);
    return output;
}
