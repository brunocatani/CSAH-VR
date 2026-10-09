TextureCube<float3> WaterEnvironment : register(t69);
#define SKYLIGHTING_PARAMETERS_SRV
#include "../Skylighting/SkylightingSampling.hlsli"
TextureCube<float> WaterValidity : register(t70);
TextureCube<float4> WaterPosition : register(t71);
TextureCube<float3> WaterPreviousEnvironment : register(t72);
TextureCube<float> WaterPreviousValidity : register(t73);
TextureCube<float4> WaterPreviousPosition : register(t74);
Buffer<float4> WaterReflectionParameters : register(t75);
SamplerState WaterEnvironmentSampler : register(s14);
cbuffer NativeCamera : register(b12) {float4 Camera[61];};
struct Input
{
    float4 eyeVector:TEXCOORD0;
    nointerpolation uint eye:EYEPOS;
    float3 normal:TEXCOORD8;
    float3 nativeColour:TEXCOORD9;
};
float3 CorrectWaterDirection(TextureCube<float4> positions,float3 direction,float3 receiver,float4 origin)
{
    if(origin.w<.5) return direction;
    // The shared provider stores absolute hit positions. Match the material
    // consumer's confidence-weighted shell projection, with sharp water LOD.
    float4 hit=positions.SampleLevel(WaterEnvironmentSampler,direction,0);
    float radius=length(hit.xyz-origin.xyz);
    float3 fromProbe=receiver-origin.xyz;
    float distanceSquared=dot(fromProbe,fromProbe),projection=dot(fromProbe,direction);
    float discriminant=projection*projection-(distanceSquared-radius*radius);
    if(!(hit.w>1e-4 && radius>16.5 && distanceSquared<radius*radius && discriminant>1e-4)) return direction;
    float travel=-projection+sqrt(discriminant);
    return normalize(lerp(direction,normalize(fromProbe+direction*travel),saturate(hit.w)));
}
float3 PSMain(Input input):SV_Target0
{
    float3 result=input.nativeColour;
    uint rows;WaterReflectionParameters.GetDimensions(rows);
    [branch] if(rows>=4)
    {
        float4 control=WaterReflectionParameters[0];
        float maximumDistance=WaterReflectionParameters[3].x;
        float fade=1-saturate(input.eyeVector.w/max(maximumDistance,1));
        [branch] if(control.z>.5 && control.w>0 && fade>0 && input.eye<2 &&
            dot(input.eyeVector.xyz,input.eyeVector.xyz)>1e-6 && dot(input.normal,input.normal)>1e-6)
        {
            // Unlike the vanilla material cubemap coordinate, this is already
            // the outgoing world ray. Negating it would reflect the ground skyward.
            float3 direction=reflect(normalize(input.eyeVector.xyz),normalize(input.normal));
            float3 receiver=Camera[59+input.eye].xyz+input.eyeVector.xyz;
            float3 currentDirection=CorrectWaterDirection(WaterPosition,direction,receiver,WaterReflectionParameters[1]);
            float3 colour=WaterEnvironment.SampleLevel(WaterEnvironmentSampler,currentDirection,0);
            float confidence=saturate(WaterValidity.SampleLevel(WaterEnvironmentSampler,currentDirection,0));
            if(control.y>.5 && control.x<1)
            {
                float3 previousDirection=CorrectWaterDirection(WaterPreviousPosition,direction,receiver,WaterReflectionParameters[2]);
                float3 previous=WaterPreviousEnvironment.SampleLevel(WaterEnvironmentSampler,previousDirection,0);
                float previousConfidence=saturate(WaterPreviousValidity.SampleLevel(WaterEnvironmentSampler,previousDirection,0));
                colour=lerp(previous,colour,saturate(control.x));confidence=lerp(previousConfidence,confidence,saturate(control.x));
            }
            if(Response.z>.5) colour*=SkylightingReflectionVisibility(receiver,normalize(input.normal),-normalize(input.eyeVector.xyz),0);
            if(all(isfinite(colour))) result=lerp(result,max(colour,0),saturate(confidence*control.w*fade));
        }
    }
    return result;
}

