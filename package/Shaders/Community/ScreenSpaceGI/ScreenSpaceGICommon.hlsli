#ifndef CSAH_SCREEN_SPACE_GI_COMMON
#define CSAH_SCREEN_SPACE_GI_COMMON
// Shared definitions for the stereo SSGI pipeline. The passes follow upstream
// Community Shaders ScreenSpaceGI (XeGTAO visibility bitmask by
// FiveLimbedCat/ProfJack, Intel SPDX MIT); every texture is a two-slice array,
// one slice per eye, at the internal resolution given by Extent.
#include "../Common/Fo4VrDepth.hlsli"
#include "ScreenSpaceGIMath.hlsli"
cbuffer Settings : register(b13)
{
    uint2 Extent; uint Frame; uint HistoryValid;
    float2 ViewportOrigin; float2 ViewportExtent;
    float AORadius; float Thickness; float AOPower; float GIStrength;
    uint EnableGI; uint EnableSpecular; uint EnableTemporal; uint TiledLighting;
    // EffectRadius is max(AO, GI) radius in game units; AORadius and GIRadius
    // are fractions of it, as upstream compares them with the step fraction.
    float EffectRadius; float GIRadius; float MinScreenRadius; float GISaturation;
    float GIDistanceCompensation; float GICompensationMaxDist; float DepthFadeStart; float DepthFadeScale;
    float DepthDisocclusion; float BlurRadius; float DistanceNormalisation; uint MaxAccumFrames;
    uint NumSlices; uint NumSteps; uint EnableBlur; uint ResolutionMode;
    float4 PbrFeature0, PbrFeature1, PbrParams0, PbrParams1;
};
cbuffer NativeCamera : register(b12) {float4 Camera[77];};
cbuffer PreviousCamera : register(b10) {float4 Previous[77];};
cbuffer NativeStereo : register(b8) {float4 Stereo;};
static const uint MipCount=5;
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
    float2 uv=(pixel-ViewportOrigin)/ViewportExtent;
    float4 clip=float4(((uv.x*2-1)+(eye==0?.5:-.5)*Stereo.x)*(Stereo.x+1),1-uv.y*2,Fo4VrProjectionDepth(depth),1);
    uint row=Fo4VrInverseProjectionBase(depth,eye);
    float4 h=float4(dot(Camera[row],clip),dot(Camera[row+1],clip),dot(Camera[row+2],clip),dot(Camera[row+3],clip));
    return h.xyz/h.w;
}
// View-space size of one internal pixel at unit depth, from the native inverse
// projection of this eye (upstream: NDCToViewMul.x / OUT_FRAME_DIM.x).
float PixelViewScale(uint eye,bool firstPerson)
{
    const float depth=firstPerson?.005:.5;
    float2 pixel=ViewportOrigin+float2(.25+.5*eye,.5)*ViewportExtent;
    float3 a=ViewPosition(pixel,depth,eye);
    float3 b=ViewPosition(pixel+float2(ViewportExtent.x/(2.0*Extent.x),0),depth,eye);
    return abs(b.x-a.x)/max(abs(a.z),1e-6);
}
float3 WorldRelative(float3 view)
{
    return float3(dot(Camera[20].xyz,view),dot(Camera[21].xyz,view),dot(Camera[22].xyz,view));
}
// Previous-frame eye-local UV and previous-view position of a current
// view-space point, through the world-relative camera origins.
bool PreviousUV(float3 view,uint eye,out float2 uv,out float3 expected)
{
    float3 relative=WorldRelative(view)+Camera[59+eye].xyz-Previous[59+eye].xyz;
    expected=float3(dot(Previous[0].xyz,relative),dot(Previous[1].xyz,relative),dot(Previous[2].xyz,relative));
    uint r=4+eye*4;
    float4 p=float4(expected,1);
    float4 clip=float4(dot(Previous[r],p),dot(Previous[r+1],p),dot(Previous[r+2],p),dot(Previous[r+3],p));
    uv=clip.xy/clip.w*float2(.5,-.5)+.5;
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
int2 MipDimensions(uint mip) {return max(int2(Extent>>mip),1);}
int4 MipTexel(float2 pixel,uint eye,uint mip) {return int4(clamp(int2(pixel)>>mip,0,MipDimensions(mip)-1),eye,mip);}
#endif
