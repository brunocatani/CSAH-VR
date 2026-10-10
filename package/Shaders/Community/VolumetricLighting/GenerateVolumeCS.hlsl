#include "VolumetricCommon.hlsli"

Texture2DArray<float> DirectionalShadow : register(t0);
TextureCube<float> CloudOcclusion : register(t1);
SamplerComparisonState ShadowComparison : register(s0);
SamplerState CloudSampler : register(s1);
RWTexture3D<float> RawVolume : register(u0);

Buffer<float4> CloudField : register(t2);
#include "../CloudShadows/CloudProjection.hlsli"
float CloudVisibility(float3 viewPosition, float3 towardLight, uint eye)
{
    if (WindParams.w <= 0.5f) return 1.0f;
    return CapturedCloudVisibility(CloudWorldVector(viewPosition) + EyeOrigin[eye].xyz,
        towardLight, CloudParams.x, CloudParams.y, CloudParams.z);
}

float DirectionNoise(float3 direction)
{
    return frac(52.9829189f * frac(dot(
        direction * 4096.0f,
        float3(0.06711056f, 0.00583715f, 0.07121243f))));
}

[numthreads(8, 8, 4)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    uint depth;
    RawVolume.GetDimensions(width, height, depth);
    if (any(dispatchId >= uint3(width, height, depth)) || width < 2u) {
        return;
    }
    const uint eyeWidth = width >> 1u;
    const uint eye = min(dispatchId.x / max(eyeWidth, 1u), 1u);
    const uint eyeX = dispatchId.x - eye * eyeWidth;
    const float3 spatialStep[8] = {
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 0.0f, 0.001f),
        float3(0.0f, 0.001f, 0.0f),
        float3(0.0f, 0.001f, 0.001f),
        float3(0.001f, 0.0f, 0.0f),
        float3(0.001f, 0.0f, 0.001f),
        float3(0.001f, 0.001f, 0.0f),
        float3(0.001f, 0.001f, 0.001f)
    };
    const float3 step = spatialStep[(uint)FrameParams.y & 7u];
    const float2 eyeTexel = 0.5f / float2(eyeWidth, height);
    const float2 eyeUv = clamp(
        (float2(eyeX, dispatchId.y) + 0.5f) /
            float2(eyeWidth, height) + step.xy,
        eyeTexel,
        1.0f - eyeTexel);
    const float2 packedUv = float2(((float)eye + eyeUv.x) * 0.5f, eyeUv.y);

    const float3 rayEnd = ReconstructRelativePosition(
        packedUv, eye, 0.999f);
    const float rayLength = length(rayEnd);
    if (!Finite3(rayEnd) || !Finite(rayLength) || rayLength <= 1.0e-4f) {
        RawVolume[dispatchId] = 0.0f;
        return;
    }
    const float3 viewDirection = rayEnd / rayLength;
    const float maximumDistance = max(VolumeParams.x, 1.0f);
    const float extinctionDistance = max(VolumeParams.y, 1.0f);
    const float terminal = exp(-maximumDistance / extinctionDistance);
    const float inverseSlices = rcp((float)depth);
    const float startFraction = (float)dispatchId.z * inverseSlices;
    const float endFraction = (float)(dispatchId.z + 1u) * inverseSlices;
    const float startTransmittance = lerp(1.0f, terminal, startFraction);
    const float endTransmittance = lerp(1.0f, terminal, endFraction);
    const float jitter = frac(
        DirectionNoise(viewDirection) + FrameParams.x +
        (float)dispatchId.z * 0.61803398875f + step.z);
    const float sampledTransmittance = lerp(
        startTransmittance, endTransmittance, jitter);
    const float sampleDistance = -extinctionDistance * log(
        max(sampledTransmittance, 1.0e-7f));
    const float3 relativePosition = viewDirection * sampleDistance;
    const float3 towardLight = normalize(DFLight[eye + 1u].xyz);
    const float visibility = saturate(SampleDirectionalShadow(
        DirectionalShadow,
        ShadowComparison,
        eye,
        relativePosition)) * CloudVisibility(relativePosition, towardLight, eye);
    const float density = max(SampleDensity(eye, relativePosition), 0.0f);
    const float opticalWeight = max(
        startTransmittance - endTransmittance,
        0.0f);
    const float lighting = visibility * density * opticalWeight;
    RawVolume[dispatchId] = Finite(lighting) ? max(lighting, 0.0f) : 0.0f;
}
