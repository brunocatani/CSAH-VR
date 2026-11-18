// Edge-aware bounce denoise after upstream blur.cs.hlsl ("Fast Denoising
// with Self-Stabilizing Recurrent Blurs"): an 8-tap Poisson disc whose
// normal tolerance narrows as history accumulates, weighted by distance from
// the receiver's plane. CSAH places the disc in screen pixels (upstream
// projects a view-space tangent disc), keeps depth domains apart and also
// denoises AO, which upstream leaves raw. With blur disabled it forwards the
// trace output unchanged.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float4> Position : register(t0);
Texture2DArray<float4> Normal : register(t1);
Texture2DArray<float4> TracedLuma : register(t2);
Texture2DArray<float4> TracedChroma : register(t3);
RWTexture2DArray<float4> Luma : register(u0);
RWTexture2DArray<float4> Chroma : register(u1);
static const float3 Poisson8[8]={
    float3(-.4706069,-.4427112,.6461146),float3(-.9057375,.3003471,.9542373),
    float3(-.3487388,.4037880,.5335386),float3(.1023042,.6439373,.6520134),
    float3(.5699277,.3513750,.6695386),float3(.2939128,-.1131226,.3149309),
    float3(.7836658,-.4208784,.8895339),float3(.1564120,-.8198990,.8346850)};
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    const int4 self=int4(id,0);
    float4 luma=TracedLuma.Load(self),chroma=TracedChroma.Load(self);
    float4 center=Position.Load(self);
    if(EnableBlur==0 || center.w==0) {Luma[id]=luma;Chroma[id]=chroma;return;}
    float3 n=Normal.Load(self).xyz;
    float frames=chroma.w;
    float radius=lerp(BlurRadius,2,1/(1+frames));
    float halfAngle=Pi*.5*(1-lerp(0,.8,sqrt(saturate(frames/max(1.0,MaxAccumFrames)))));
    float wsum=1;
    [unroll] for(uint i=0;i<8;i++)
    {
        float2 offset=Poisson8[i].xy*radius;
        int2 p=int2(float2(id.xy)+.5+offset);
        if(any(p<0)||any(p>=int2(Extent))) continue;
        int4 t=int4(p,id.z,0);
        float4 position=Position.Load(t);
        if(position.w!=center.w) continue;
        float3 normal=Normal.Load(t).xyz;
        float w=exp(-.66*Poisson8[i].z*Poisson8[i].z);
        w*=saturate(1-abs(dot(n,position.xyz-center.xyz))*DistanceNormalisation);
        w*=1-saturate(acos(saturate(dot(normal,n)))/halfAngle);
        w=max(w,.01);
        luma+=TracedLuma.Load(t)*w;chroma+=TracedChroma.Load(t)*w;wsum+=w;
    }
    Luma[id]=luma/wsum;Chroma[id]=chroma/wsum;
}
