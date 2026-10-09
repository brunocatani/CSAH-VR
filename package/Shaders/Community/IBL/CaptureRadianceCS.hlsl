#include "../Common/Fo4VrDepth.hlsli"
Texture2D<float4> NativeComposite : register(t127);
SamplerState NativeMarker : register(s15);
Texture2D<float4> SceneDepth : register(t7);
Texture2D<float> NativeCoverage : register(t31);
RWTexture2DArray<float4> CapturedRadiance : register(u0);
cbuffer NativeCamera : register(b12) { float4 Scene[77]; };
cbuffer Capture : register(b13) { uint2 SourceExtent; uint TargetExtent; uint Reserved; };
static const float PositionEpsilon = 1.0e-5f;
#include "CaptureProjection.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= TargetExtent) || id.z >= 12) return;
    const uint eye = id.z / 6;
    const float2 faceCoordinate = ((float2(id.xy) + 0.5f) / (float)TargetExtent) * 2.0f - 1.0f;
    const float3 direction = CubeDirection(id.z % 6, faceCoordinate);
    float2 localUv;
    float edge;
    float3 radiance = 0.0f;
    bool observed = ProjectEye(direction, eye, localUv, edge);
    if (observed)
    {
        const float2 halfTexel = 0.5f / float2(SourceExtent);
        float2 uv = float2((localUv.x + (float)eye) * 0.5f, localUv.y);
        uv.x = clamp(uv.x, eye * 0.5f + halfTexel.x, (eye + 1) * 0.5f - halfTexel.x);
        uv.y = clamp(uv.y, halfTexel.y, 1.0f - halfTexel.y);
        const float2 pixel = uv * float2(SourceExtent) - 0.5f;
        const int2 base = int2(floor(pixel));
        const float2 fraction = frac(pixel);
        // One native-body insertion, bounded to the same four bilinear taps
        // as UpdateEnvironmentCS. Each tap retains the R16 capture rounding.
        [loop] for (uint tap = 0; tap < 4; ++tap)
        {
            const uint x = tap & 1;
            const uint y = tap >> 1;
            const float weight = (x != 0 ? fraction.x : 1.0f - fraction.x) *
                (y != 0 ? fraction.y : 1.0f - fraction.y);
            if (weight > 0.0f)
            {
                const int2 coordinate = clamp(base + int2(x, y),
                    int2(eye * SourceExtent.x / 2, 0), int2((eye + 1) * SourceExtent.x / 2 - 1, SourceExtent.y - 1));
                if (NativeCoverage.Load(int3(coordinate, 0)) == 0 ||
                    Fo4VrFirstPersonDepth(SceneDepth.Load(int3(coordinate, 0)).x))
                {
                    observed = false;
                }
                else
                {
                    // Replaced at build time with the original FO4VR shader
                    // body, with only environment/SSLR samples neutralized.
                    float3 sample = NativeComposite.SampleLevel(NativeMarker, float2(coordinate) + 0.5f, (float)eye).rgb;
                    sample = f16tof32(f32tof16(sample));
                    if (!all(isfinite(sample)) || any(sample < 0.0f) || any(sample >= 65504.0f)) observed = false;
                    else radiance += sample * weight;
                }
            }
        }
    }
    CapturedRadiance[id] = float4(radiance, observed ? 1.0f : 0.0f);
}
