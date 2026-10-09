#include "HeightFog.hlsli"
cbuffer ImageSpaceConstants : register(b0)
{
    float4 Reserved0;
    float4 TexelSize;
    float4 CameraParams;
    float4 SunParams;
    float4 GlareColor;
};

cbuffer VolumetricFrame : register(b1)
{
    float4 EyeOrigin[2];
    float4 VolumeParams;
    float4 ApplyParams;
    float4 FrameParams;
    float4 WindParams;
    float4 CloudParams;
};

Texture2D<float> FilteredVolume : register(t0);
Texture2D<float> ReceiverDepth : register(t1);
Texture2D<float> FullDepth : register(t2);
Texture2D<float3> SceneColor : register(t3);
Texture2D<float> StructuredVolume : register(t4);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

bool Finite3(float3 value)
{
    return all((asuint(value) & 0x7F800000u) != 0x7F800000u);
}

int2 ClampHalfCoordinate(int2 coordinate, int2 dimensions, uint eye)
{
    coordinate.y = clamp(coordinate.y, 0, dimensions.y - 1);
    const int eyeWidth = dimensions.x >> 1;
    coordinate.x = clamp(
        coordinate.x,
        eye == 0u ? 0 : eyeWidth,
        eye == 0u ? eyeWidth - 1 : dimensions.x - 1);
    return coordinate;
}

float4 main(PixelInput input) : SV_Target0
{
    uint halfWidth;
    uint halfHeight;
    FilteredVolume.GetDimensions(halfWidth, halfHeight);
    uint fullWidth;
    uint fullHeight;
    FullDepth.GetDimensions(fullWidth, fullHeight);
    const int2 fullCoordinate = min(
        int2(input.position.xy),
        int2(fullWidth - 1u, fullHeight - 1u));
    const uint eye = fullCoordinate.x < int(fullWidth >> 1u) ? 0u : 1u;
    const float2 halfPixel = input.position.xy * 0.5f - 0.5f;
    const int2 base = int2(floor(halfPixel));
    const float2 fraction = frac(halfPixel);
    const int2 dimensions = int2(halfWidth, halfHeight);
    const int2 coordinates[4] = {
        ClampHalfCoordinate(base, dimensions, eye),
        ClampHalfCoordinate(base + int2(1, 0), dimensions, eye),
        ClampHalfCoordinate(base + int2(0, 1), dimensions, eye),
        ClampHalfCoordinate(base + int2(1, 1), dimensions, eye)
    };
    const float weights[4] = {
        (1.0f - fraction.x) * (1.0f - fraction.y),
        fraction.x * (1.0f - fraction.y),
        (1.0f - fraction.x) * fraction.y,
        fraction.x * fraction.y
    };
    const float fullReceiverDepth = FullDepth.Load(int3(fullCoordinate, 0));
    const float depthScale = max(abs(1.0f - fullReceiverDepth), 1.0e-4f);
    float filtered = 0.0f;
    float structured = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (uint tap = 0u; tap < 4u; ++tap) {
        const float tapDepth = ReceiverDepth.Load(int3(coordinates[tap], 0));
        const float relativeDepth = abs(fullReceiverDepth - tapDepth) /
            depthScale;
        const float weight = weights[tap] / (0.01f + relativeDepth);
        filtered += max(
            FilteredVolume.Load(int3(coordinates[tap], 0)), 0.0f) * weight;
        structured += max(
            StructuredVolume.Load(int3(coordinates[tap], 0)), 0.0f) * weight;
        weightSum += weight;
    }
    filtered /= max(weightSum, 1.0e-5f);
    structured /= max(weightSum, 1.0e-5f);

    const float threshold = 1.0f / 128.0f;
    const float basePower = pow(
        max(filtered - threshold, 0.0f),
        1.8f) * max(ApplyParams.y, 0.0f);
    const float positiveContrast = max(
        structured - filtered - 0.01f,
        0.0f);
    const float contrastReference = max(filtered * 0.25f, 0.025f);
    const float shaftMask = saturate(
        positiveContrast / contrastReference);
    const float shaftPower = pow(
        max(structured - threshold, 0.0f),
        1.2f) * shaftMask * max(ApplyParams.z, 0.0f);
    const float3 lightColor = min(max(GlareColor.rgb, 0.0f.xxx), 4.0f.xxx);
    const float lightIntensity = min(max(SunParams.z, 0.0f), 4.0f);
    float3 radiance = (basePower + shaftPower) *
        lightColor * lightIntensity * max(ApplyParams.x, 0.0f);
    radiance = Finite3(radiance) ?
        min(max(radiance, 0.0f.xxx), 2.0f.xxx) : 0.0f.xxx;
    const float3 scene = max(
        SceneColor.Load(int3(fullCoordinate, 0)),
        0.0f.xxx);
    float volumeVisibility=1;
    if(HeightFogControl.x>.5 && HeightFogControl.w>0 && fullReceiverDepth>.01) {
        float distance=length(HeightFogViewPosition(input.position.xy/float2(fullWidth,fullHeight),min(fullReceiverDepth,.99999),eye));
        float unoccluded=1-exp(-min(distance,VolumeParams.x)/max(VolumeParams.y,1));
        volumeVisibility=saturate(filtered/max(unoccluded,1e-4));
    }
    return float4(ApplyHeightFog(scene+ radiance,input.position.xy/float2(fullWidth,fullHeight),fullReceiverDepth,eye,volumeVisibility),1);
}
