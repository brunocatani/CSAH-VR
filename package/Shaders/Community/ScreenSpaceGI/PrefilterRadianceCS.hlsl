// Radiance mip chain, after upstream prefilterRadiance.cs.hlsl: a plain
// average per level so distant horizon samples read pre-integrated light.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float4> Radiance0 : register(t0);
RWTexture2DArray<float4> Radiance1 : register(u0);
RWTexture2DArray<float4> Radiance2 : register(u1);
RWTexture2DArray<float4> Radiance3 : register(u2);
RWTexture2DArray<float4> Radiance4 : register(u3);
groupshared float3 Scratch[8][8];
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID)
{
    const uint eye=id.z;
    const int2 limit=MipDimensions(0)-1;
    const int2 p=int2(id.xy)*2;
    float3 m1=(Radiance0.Load(int4(min(p,limit),eye,0)).rgb+Radiance0.Load(int4(min(p+int2(1,0),limit),eye,0)).rgb+
        Radiance0.Load(int4(min(p+int2(0,1),limit),eye,0)).rgb+Radiance0.Load(int4(min(p+1,limit),eye,0)).rgb)*.25;
    Radiance1[uint3(id.xy,eye)]=float4(m1,1);
    Scratch[local.x][local.y]=m1;
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%2==0))
    {
        float3 m2=(Scratch[local.x][local.y]+Scratch[local.x+1][local.y]+Scratch[local.x][local.y+1]+Scratch[local.x+1][local.y+1])*.25;
        Radiance2[uint3(id.xy/2,eye)]=float4(m2,1);Scratch[local.x][local.y]=m2;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%4==0))
    {
        float3 m3=(Scratch[local.x][local.y]+Scratch[local.x+2][local.y]+Scratch[local.x][local.y+2]+Scratch[local.x+2][local.y+2])*.25;
        Radiance3[uint3(id.xy/4,eye)]=float4(m3,1);Scratch[local.x][local.y]=m3;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%8==0))
        Radiance4[uint3(id.xy/8,eye)]=float4((Scratch[local.x][local.y]+Scratch[local.x+4][local.y]+Scratch[local.x][local.y+4]+Scratch[local.x+4][local.y+4])*.25,1);
}
