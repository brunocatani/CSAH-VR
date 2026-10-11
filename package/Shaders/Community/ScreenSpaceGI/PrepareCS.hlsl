#include "ScreenSpaceGICommon.hlsli"
#include "../PBR/PbrCommon.hlsli"
#include "../PBR/PbrIndirectMaterial.hlsli"
Texture2D<float4> Authored : register(t3);
Texture2D<float> Surface : register(t4);
Texture2D<float4> NativeNormal : register(t0);
Texture2D<float4> NativeDepth : register(t1);
Texture2D<float4> NativeMaterial : register(t2);
RWTexture2DArray<float4> Position : register(u0);
RWTexture2DArray<float4> Normal : register(u1);
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    float2 pixel=PackedPixel(id);
    // PackedPixel is already in the native texture's active viewport.
    uint w,h;NativeDepth.GetDimensions(w,h);
    int2 p=clamp(int2(pixel),0,int2(w,h)-1);
    float d=NativeDepth.Load(int3(p,0)).x;
    float3 v=ViewPosition(pixel,d,id.z);
    bool valid=d>0 && d<1 && all(isfinite(v)) && v.z>0;
    Position[id]=valid?float4(v,Fo4VrFirstPersonDepth(d)?-1:1):0;
    NativeNormal.GetDimensions(w,h);p=clamp(int2(pixel),0,int2(w,h)-1);
    float3 n=DecodeNormal(NativeNormal.Load(int3(p,0)).xy);
    PbrIndirectMaterial material=DecodePbrIndirectMaterial(NativeMaterial.Load(int3(p,0)),Authored.Load(int3(p,0)),0,Surface.Load(int3(p,0)),PbrFeature0,PbrFeature1,PbrParams0,PbrParams1);
    Normal[id]=float4(n,material.roughness);
}
