#include "VolumetricCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture2DArray<float> DirectionalShadow : register(t1);
Texture3D<float> IntegratedVolume : register(t2);
SamplerComparisonState ShadowComparison : register(s0);
SamplerState VolumeSampler : register(s1);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelOutput
{
    float glare : SV_Target0;
    float depth : SV_Target1;
};

static const uint OpticalSampleCount = 64u;

float DirectionNoise(float3 direction)
{
    return frac(52.9829189f * frac(dot(
        direction * 4096.0f,
        float3(0.06711056f, 0.00583715f, 0.07121243f))));
}

float OpticalSampleEdge(uint edge)
{
    const float fraction = (float)edge / (float)OpticalSampleCount;
    const float extinctionDistance = max(VolumeParams.y, 1.0f);
    const float terminal = exp(-VolumeParams.x / extinctionDistance);
    const float transmittance = lerp(1.0f, terminal, fraction);
    return -extinctionDistance * log(max(transmittance, 1.0e-7f));
}

float IntegrateDetailedVisibility(
    uint eye,
    float3 viewDirection,
    float rayLength)
{
    float integrated = 0.0f;
    [loop]
    for (uint sampleIndex = 0u;
         sampleIndex < OpticalSampleCount;
         ++sampleIndex) {
        const float intervalStart = OpticalSampleEdge(sampleIndex);
        if (intervalStart >= rayLength) {
            break;
        }
        const float intervalEnd = min(
            OpticalSampleEdge(sampleIndex + 1u), rayLength);
        const float extinctionDistance = max(VolumeParams.y, 1.0f);
        const float startTransmittance = exp(
            -intervalStart / extinctionDistance);
        const float endTransmittance = exp(
            -intervalEnd / extinctionDistance);
        const float jitter = frac(
            DirectionNoise(viewDirection) + FrameParams.x +
            (float)sampleIndex * 0.61803398875f);
        const float sampledTransmittance = lerp(
            startTransmittance, endTransmittance, jitter);
        const float distance = -extinctionDistance * log(
            max(sampledTransmittance, 1.0e-7f));
        if (!Finite(distance)) {
            continue;
        }
        const float3 relativePosition = viewDirection * distance;
        const float visibility = saturate(SampleDirectionalShadow(
            DirectionalShadow,
            ShadowComparison,
            eye,
            relativePosition));
        const float density = max(SampleDensity(eye, relativePosition), 0.0f);
        integrated += visibility * density * max(
            startTransmittance - endTransmittance,
            0.0f);
    }
    return integrated;
}

float SampleFroxelLighting(float2 packedUv, uint eye, float rayLength)
{
    const float extinctionDistance = max(VolumeParams.y, 1.0f);
    const float terminal = exp(-VolumeParams.x / extinctionDistance);
    const float receiver = exp(-rayLength / extinctionDistance);
    const float opticalFraction = saturate(
        (1.0f - receiver) / max(1.0f - terminal, 1.0e-7f));
    uint width;
    uint height;
    uint depth;
    IntegratedVolume.GetDimensions(width, height, depth);
    const float3 texel = 0.5f / float3(width, height, depth);
    const float eyeMinimumX = eye == 0u ? texel.x : 0.5f + texel.x;
    const float eyeMaximumX = eye == 0u ?
        0.5f - texel.x : 1.0f - texel.x;
    const float z = clamp(
        opticalFraction - texel.z,
        texel.z,
        1.0f - texel.z);
    const float value = IntegratedVolume.SampleLevel(
        VolumeSampler,
        float3(
            clamp(packedUv.x, eyeMinimumX, eyeMaximumX),
            clamp(packedUv.y, texel.y, 1.0f - texel.y),
            z),
        0.0f);
    const float firstSliceScale = saturate(
        opticalFraction / max(texel.z * 2.0f, 1.0e-7f));
    return Finite(value) ? max(value, 0.0f) * firstSliceScale : 0.0f;
}

PixelOutput main(PixelInput input)
{
    const float2 packedUv = input.uv;
    const uint2 fullCoordinate = uint2(DepthViewport.xy + packedUv * DepthViewport.zw);
    const float depth = SceneDepth.Load(int3(fullCoordinate, 0));
    PixelOutput output;
    output.glare = 0.0f;
    output.depth = Finite(depth) ? depth : 0.0f;
    if (!Finite(depth)) {
        return output;
    }
    const uint eye = packedUv.x < 0.5f ? 0u : 1u;
    const float3 relativePosition = ReconstructRelativePosition(
        packedUv,
        eye,
        depth);
    const float reconstructedDistance = length(relativePosition);
    if (!Finite3(relativePosition) || !Finite(reconstructedDistance) ||
        reconstructedDistance <= 1.0e-4f) {
        return output;
    }
    const float rayLength = min(reconstructedDistance, VolumeParams.x);
    const float3 viewDirection = relativePosition / reconstructedDistance;
    const float detailed = IntegrateDetailedVisibility(
        eye, viewDirection, rayLength);
    const float froxel = SampleFroxelLighting(packedUv, eye, rayLength);
    const float hybrid = lerp(froxel, detailed, saturate(FrameParams.w));
    output.glare = Finite(hybrid) ? max(hybrid, 0.0f) : 0.0f;
    return output;
}
