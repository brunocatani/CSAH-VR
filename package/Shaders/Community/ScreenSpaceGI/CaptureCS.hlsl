#include "ScreenSpaceGICommon.hlsli"
Texture2D<float4> Albedo : register(t0);
Texture2D<float4> Emissive : register(t1);
Texture2D<float4> DiffuseLighting : register(t2);
Texture2D<float4> TiledDiffuse : register(t3);
Texture2DArray<float4> Position : register(t4);
Texture2DArray<float4> AppliedGI : register(t5);
Texture2D<float4> AuthoredPbr : register(t6);
RWTexture2DArray<float4> Radiance : register(u0);
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    uint w,h;Albedo.GetDimensions(w,h);int2 p=clamp(int2(PackedPixel(id)),0,int2(w,h)-1);
    float3 albedo=Albedo.Load(int3(p,0)).rgb;
    float3 lighting=DiffuseLighting.Load(int3(p,0)).rgb;
    if(TiledLighting!=0) lighting+=TiledDiffuse.Load(int3(p,0)).rgb;
    // Remove this frame's injected irradiance to prevent unbounded feedback.
    float4 authored=AuthoredPbr.Load(int3(p,0));
    float diffuseShare=authored.x>=.5?1-saturate(authored.y):1;
    lighting=max(0,lighting-AppliedGI.Load(int4(id,0)).rgb*diffuseShare/3);
    float3 colour=albedo*lighting*3+Emissive.Load(int3(p,0)).rgb;
    bool valid=Position.Load(int4(id,0)).w>0 && all(isfinite(colour));
    Radiance[id]=valid?float4(min(max(colour,0),60000),1):0;
}
