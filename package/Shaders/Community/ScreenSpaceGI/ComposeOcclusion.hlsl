// Composes Screen Space GI into the deferred diffuse accumulators right before
// the native composite, the way upstream composes it there: every diffuse term
// (sun, local and tiled lights) takes the square root of the multibounce
// occlusion, ambient takes the full occlusion (the ambient pass applies the
// other root), and the bounced light is added unoccluded. Dual-source
// blending computes dest * Scale + Add, so no accumulator copy is needed.
// The accumulators are multiplied by 3 * albedo in the composite, which is
// why ScreenSpaceAmbient returns the bounce divided by 3.
cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[61];
};

cbuffer ComposeConstants : register(b0)
{
    uint AddIndirect;
    uint3 ComposePad;
};

Texture2D<float4> NativeNormal : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
Texture2D<float4> NativeDepth : register(t3);

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

ComposeOutput PSMain(float4 position : SV_Position)
{
    uint count;
    ScreenSpaceParameters.GetDimensions(count);
    uint eye = 0u;
    if (count >= ScreenSpaceParameterCount) {
        const float4 rect = ScreenSpaceParameters[1];
        eye = position.x - rect.x >= rect.z * 0.5f ? 1u : 0u;
    }
    const ScreenSpaceAmbientResult ssgi = ScreenSpaceAmbient(position.xy, eye);
    ComposeOutput output;
    output.Add = float4(AddIndirect != 0u ? ssgi.diffuse : 0.0f.xxx, 0.0f);
    output.Scale = float4(sqrt(ssgi.diffuseVisibility), 1.0f);
    return output;
}
