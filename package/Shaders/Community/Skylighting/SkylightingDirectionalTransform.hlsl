// Native DFLight cb2[0].xy is the active viewport reciprocal, as used
// by the original shader before stereo clip reconstruction.
cbuffer NativeLightViewport : register(b2) {float4 NativeViewportTexel;}
#define SKYLIGHTING_PARAMETERS_SRV
#include "SkylightingSampling.hlsli"
Texture2D<float> NativeDepth : register(t3);
Texture2D<float> SurfaceClass : register(t47);
cbuffer NativeStereo : register(b8) { float4 Stereo[1]; };
cbuffer NativeCamera : register(b12) { float4 Camera[61]; };
cbuffer BasicWetnessSettings : register(b9) { float4 BasicWetnessParams; float4 BasicWetnessMaterialParams; };
#include "SkylightingReceiver.hlsli"
#include "../WaterEffects/WaterCaustics.hlsli"
#include "../TerrainShadows/TerrainShadows.hlsli"

struct PixelInput
{
    float4 Position : SV_Position;
    nointerpolation uint Eye : EYEINDEX;
    float4 NormalCarrier : TEXCOORD0;
    float4 RimDiffuse : TEXCOORD1;
    float4 TransmissionDiffuse : TEXCOORD2;
    float4 PbrDiffuseScale : TEXCOORD3;
    float4 NativeVisibility : TEXCOORD4;
    float4 ContactVisibility : TEXCOORD5;
};

struct DirectionalOutput {float4 diffuseDelta:SV_Target0;float4 caustics:SV_Target1;};
DirectionalOutput PSMain(PixelInput input)
{
    float softVisibility = input.NativeVisibility.x;
    if (Response.z > 0.5f) {
        uint width, height; NativeDepth.GetDimensions(width, height);
        const float depth = NativeDepth.Load(int3(int2(input.Position.xy), 0));
        if (depth > 1.0e-6f && width > 0 && height > 0) {
            const float3 position = ReconstructRelativeWorldPosition(input.Position.xy * NativeViewportTexel.xy, depth, input.Eye);
            const float3 normal = normalize(float3(dot(Camera[20].xyz, input.NormalCarrier.xyz),
                dot(Camera[21].xyz, input.NormalCarrier.xyz), dot(Camera[22].xyz, input.NormalCarrier.xyz)));
            if (all(isfinite(position)) && all(isfinite(normal)))
                softVisibility = SkylightingSunVisibility(position + ProbeWorldOrigin.xyz, normal, input.NativeVisibility.x);
        }
    }
    const float front = input.ContactVisibility.x;
    const float back = input.ContactVisibility.y;
    const float3 delta = input.RimDiffuse.xyz * ((softVisibility - input.NativeVisibility.x) * front) +
        input.TransmissionDiffuse.xyz * (softVisibility * back - input.NativeVisibility.x * front);
    const float code = SurfaceClass.Load(int3(int2(input.Position.xy), 0)) * 255.0f;
    const float eligible = saturate(1.0f - step(1.5f, code) + 1.0f - step(0.5f, abs(code - 4.0f)));
    const float wetness = saturate(BasicWetnessParams.x * BasicWetnessParams.y * eligible);
    const float wetScale = 1.0f - saturate(BasicWetnessParams.z) * wetness;
    // The native final /3 still owns DFLight accumulation units. Only the
    // soft diffuse terms change; native hard shadow and specular stay intact.
    DirectionalOutput output;
    output.diffuseDelta=float4(delta * input.PbrDiffuseScale.xyz * wetScale, 0);
    output.caustics=1;
    uint waterRows;WaterParameters.GetDimensions(waterRows);
    if(waterRows>=28 && WaterParameters[1].x>.5 && WaterParameters[1].w>.5) {
        uint width,height;NativeDepth.GetDimensions(width,height);
        float depth=NativeDepth.Load(int3(int2(input.Position.xy),0));
        if(depth>0 && depth<1 && width>0 && height>0) {
            float3 world=ReconstructRelativeWorldPosition(input.Position.xy * NativeViewportTexel.xy,depth,input.Eye)+ProbeWorldOrigin.xyz;
            output.caustics=float4(WaterCausticMultiplier(world),1);
        }
    }
    uint terrainRows;TerrainShadowParameters.GetDimensions(terrainRows);
    if(terrainRows>=2) {
        uint width,height;NativeDepth.GetDimensions(width,height);
        float depth=NativeDepth.Load(int3(int2(input.Position.xy),0));
        if(depth>.01 && depth<1 && width>0 && height>0) {
            float3 world=ReconstructRelativeWorldPosition(input.Position.xy * NativeViewportTexel.xy,depth,input.Eye)+ProbeWorldOrigin.xyz;
            output.caustics.xyz*=TerrainVisibility(world);
        }
    }
    return output;
}
