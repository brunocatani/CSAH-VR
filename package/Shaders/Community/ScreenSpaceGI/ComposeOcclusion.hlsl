// Composes Screen Space GI into the deferred diffuse accumulator right before
// the native composite, the way upstream composes it there: every diffuse term
// (sun, local and tiled lights) takes the square root of the multibounce
// occlusion, ambient takes the full occlusion (the ambient pass applies the
// other root), and the bounced light is added unoccluded. Dual-source
// blending computes dest * Scale + Add, so no accumulator copy is needed.
// The composite adds its two accumulators and multiplies them by 3 * albedo,
// which is why ScreenSpaceAmbient returns the bounce divided by 3 and why
// the tiled accumulator's light can be folded in here: the runtime then shows
// the composite a black tiled accumulator instead of scaling that one too.
cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[61];
};

Texture2D<float4> NativeNormal : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
Texture2D<float4> NativeDepth : register(t3);
// The native tiled accumulator, bound only when the composite reads one; an
// unbound texture loads as zero.
Texture2D<float4> TiledDiffuse : register(t4);

#include "../Skylighting/SkylightingSampling.hlsli"
#include "../Skylighting/SkylightingReceiver.hlsli"
#include "Ambient.hlsli"

struct ComposeOutput
{
    float4 Add : SV_Target0;
    float4 Scale : SV_Target1;
};

float4 VSMain(uint vertex : SV_VertexID) : SV_Position
{
    const float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

uint ComposeEye(float4 position)
{
    uint count;
    ScreenSpaceParameters.GetDimensions(count);
    if (count < ScreenSpaceParameterCount) {
        return 0u;
    }
    const float4 rect = ScreenSpaceParameters[1];
    return position.x - rect.x >= rect.z * 0.5f ? 1u : 0u;
}

ComposeOutput PSMain(float4 position : SV_Position)
{
    const ScreenSpaceAmbientResult ssgi = ScreenSpaceAmbient(position.xy, ComposeEye(position));
    const float3 scale = sqrt(ssgi.diffuseVisibility);
    const float3 tiled = TiledDiffuse.Load(int3(position.xy, 0)).rgb;
    ComposeOutput output;
    output.Add = float4(ssgi.diffuse + tiled * scale, 0.0f);
    output.Scale = float4(scale, 1.0f);
    return output;
}
