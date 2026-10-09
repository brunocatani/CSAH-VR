#ifndef CSAH_SCREEN_SPACE_GI_COMMON
#define CSAH_SCREEN_SPACE_GI_COMMON
#include "../Common/Fo4VrDepth.hlsli"
cbuffer Settings : register(b13)
{
    uint2 Extent; uint Frame; uint HistoryValid;
    float2 ViewportOrigin; float2 ViewportExtent;
    float Radius; float Thickness; float AOPower; float GIStrength;
    uint EnableGI; uint EnableSpecular; uint EnableTemporal; uint TiledLighting;
};
cbuffer NativeCamera : register(b12) {float4 Camera[77];};
cbuffer PreviousCamera : register(b10) {float4 Previous[77];};
cbuffer NativePass : register(b2) {float4 NativeParameters[30];};
cbuffer NativeStereo : register(b8) {float4 Stereo;};
static const float Pi=3.14159265358979323846f;
float3 SafeNormal(float3 v) {return v*rsqrt(max(dot(v,v),1e-12));}
float3 DecodeNormal(float2 n)
{
    float2 e=n*4-2;float d=dot(e,e);
    return SafeNormal(float3(e*sqrt(saturate(1-d*.25)),d*.5-1));
}
float2 PackedPixel(uint3 p)
{
    return ViewportOrigin+float2((p.x+.5+Extent.x*p.z)/(2.0*Extent.x),(p.y+.5)/Extent.y)*ViewportExtent;
}
float3 ViewPosition(float2 pixel,float depth,uint eye)
{
    float2 uv=pixel*NativeParameters[0].xy;
    float4 clip=float4(((uv.x*2-1)+(eye==0?.5:-.5)*Stereo.x)*(Stereo.x+1),1-uv.y*2,Fo4VrProjectionDepth(depth),1);
    uint row=Fo4VrInverseProjectionBase(depth,eye);
    float4 h=float4(dot(Camera[row],clip),dot(Camera[row+1],clip),dot(Camera[row+2],clip),dot(Camera[row+3],clip));
    return h.xyz/h.w;
}
float3 WorldRelative(float3 view)
{
    return float3(dot(Camera[20].xyz,view),dot(Camera[21].xyz,view),dot(Camera[22].xyz,view));
}
bool PreviousPixel(float3 view,uint eye,out int3 pixel,out float3 expected)
{
    float3 relative=WorldRelative(view)+Camera[59+eye].xyz-Previous[59+eye].xyz;
    expected=float3(dot(Previous[0].xyz,relative),dot(Previous[1].xyz,relative),dot(Previous[2].xyz,relative));
    uint r=4+eye*4;
    float4 p=float4(expected,1);
    float4 clip=float4(dot(Previous[r],p),dot(Previous[r+1],p),dot(Previous[r+2],p),dot(Previous[r+3],p));
    float2 uv=clip.xy/clip.w*float2(.5,-.5)+.5;
    pixel=int3(uv*Extent,eye);
    return HistoryValid!=0 && clip.w>0 && all(isfinite(uv)) && all(uv>=0) && all(uv<1);
}
uint AngleMask(float2 angles)
{
    uint lo=(uint)clamp(round(min(angles.x,angles.y)*32),0,32);
    uint hi=(uint)clamp(round(max(angles.x,angles.y)*32),0,32);
    // DXBC shifts mask the shift count: shifting by 32 is NOT a full mask.
    uint lower=lo==32?0xffffffffu:((1u<<lo)-1u);
    uint upper=hi==32?0xffffffffu:((1u<<hi)-1u);
    return upper & ~lower;
}
#endif
