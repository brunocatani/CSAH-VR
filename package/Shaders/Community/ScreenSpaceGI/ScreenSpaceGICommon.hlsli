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
    // Fewer samples per direction on flat or distant surfaces (TraceCS).
    uint AdaptiveSampling; uint3 SettingsReserved;
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
float4 ClipPosition(float2 pixel,float depth,uint eye)
{
    float2 uv=(pixel-ViewportOrigin)/ViewportExtent;
    return float4(((uv.x*2-1)+(eye==0?.5:-.5)*Stereo.x)*(Stereo.x+1),1-uv.y*2,Fo4VrProjectionDepth(depth),1);
}
float3 Unproject(float4 r0,float4 r1,float4 r2,float4 r3,float4 clip)
{
    float4 h=float4(dot(r0,clip),dot(r1,clip),dot(r2,clip),dot(r3,clip));
    return h.xyz/h.w;
}
float3 ViewPosition(float2 pixel,float depth,uint eye)
{
    uint row=Fo4VrInverseProjectionBase(depth,eye);
    return Unproject(Camera[row],Camera[row+1],Camera[row+2],Camera[row+3],ClipPosition(pixel,depth,eye));
}
// Internal geometry is one signed inverse view depth per texel: +1/z for
// world surfaces, -1/z for first-person surfaces, 0 where nothing valid was
// rendered. Positions are rebuilt as ray(pixel) * z. The native inverse
// projections are perspective, so x/z and y/z are affine in the pixel and do
// not depend on depth; inverse depth is affine across a plane in screen
// space, so averaging it keeps planes exact through the mip chain. One float
// per texel keeps the trace's 64 taps per pixel in cache, where four floats
// did not fit at headset resolution.
struct ViewRays {float2 origin;float2 dx;float2 dy;};
// q is an internal-resolution coordinate (a texel centre is index + 0.5).
float2 NativePixel(float2 q,uint eye)
{
    return ViewportOrigin+float2((q.x+Extent.x*eye)/(2.0*Extent.x),q.y/Extent.y)*ViewportExtent;
}
float2 RayAt(float4 r0,float4 r1,float4 r2,float4 r3,float2 q,float depth,uint eye)
{
    float3 v=Unproject(r0,r1,r2,r3,ClipPosition(NativePixel(q,eye),depth,eye));
    return v.xy/v.z;
}
ViewRays BuildRays(float4 r0,float4 r1,float4 r2,float4 r3,float depth,uint eye)
{
    // Spanning the whole eye keeps the affine terms at full precision.
    ViewRays rays;
    rays.origin=RayAt(r0,r1,r2,r3,0,depth,eye);
    rays.dx=(RayAt(r0,r1,r2,r3,float2(Extent.x,0),depth,eye)-rays.origin)/Extent.x;
    rays.dy=(RayAt(r0,r1,r2,r3,float2(0,Extent.y),depth,eye)-rays.origin)/Extent.y;
    return rays;
}
// Representative native depths of each domain select its inverse projection.
float DomainReferenceDepth(bool firstPerson) {return firstPerson?.005:.5;}
ViewRays CurrentRays(uint eye,bool firstPerson)
{
    const float d=DomainReferenceDepth(firstPerson);const uint r=Fo4VrInverseProjectionBase(d,eye);
    return BuildRays(Camera[r],Camera[r+1],Camera[r+2],Camera[r+3],d,eye);
}
ViewRays PreviousRays(uint eye,bool firstPerson)
{
    const float d=DomainReferenceDepth(firstPerson);const uint r=Fo4VrInverseProjectionBase(d,eye);
    return BuildRays(Previous[r],Previous[r+1],Previous[r+2],Previous[r+3],d,eye);
}
float3 RebuildPosition(ViewRays rays,float2 q,float geometry)
{
    const float z=1/abs(geometry);
    return float3((rays.origin+q.x*rays.dx+q.y*rays.dy)*z,z);
}
// View-space size of one internal pixel at unit depth (upstream:
// NDCToViewMul.x / OUT_FRAME_DIM.x).
float PixelViewScale(ViewRays rays) {return abs(rays.dx.x);}
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
// Internal-resolution coordinate of a mip texel's footprint centre.
float2 MipTexelCentre(int4 texel) {return (float2(texel.xy)+.5)*float(1u<<texel.w);}
#endif
