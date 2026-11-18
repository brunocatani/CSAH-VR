// Position mip chain for distance-scaled horizon sampling, after upstream
// prefilterDepths.cs.hlsl (linear filter). A texel averages the world
// surfaces it covers; only when none are present does it average first-person
// surfaces, so the two depth domains never blend.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float4> Position0 : register(t0);
RWTexture2DArray<float4> Position1 : register(u0);
RWTexture2DArray<float4> Position2 : register(u1);
RWTexture2DArray<float4> Position3 : register(u2);
RWTexture2DArray<float4> Position4 : register(u3);
float4 Filter(float4 a,float4 b,float4 c,float4 d)
{
    float4 world=0,firstPerson=0;
    world+=a.w>0?float4(a.xyz,1):0;world+=b.w>0?float4(b.xyz,1):0;world+=c.w>0?float4(c.xyz,1):0;world+=d.w>0?float4(d.xyz,1):0;
    firstPerson+=a.w<0?float4(a.xyz,1):0;firstPerson+=b.w<0?float4(b.xyz,1):0;firstPerson+=c.w<0?float4(c.xyz,1):0;firstPerson+=d.w<0?float4(d.xyz,1):0;
    if(world.w>0) return float4(world.xyz/world.w,1);
    if(firstPerson.w>0) return float4(firstPerson.xyz/firstPerson.w,-1);
    return 0;
}
groupshared float4 Scratch[8][8];
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID)
{
    const uint eye=id.z;
    const int2 limit=MipDimensions(0)-1;
    const int2 p=int2(id.xy)*2;
    float4 m1=Filter(Position0.Load(int4(min(p,limit),eye,0)),Position0.Load(int4(min(p+int2(1,0),limit),eye,0)),
        Position0.Load(int4(min(p+int2(0,1),limit),eye,0)),Position0.Load(int4(min(p+1,limit),eye,0)));
    Position1[uint3(id.xy,eye)]=m1;
    Scratch[local.x][local.y]=m1;
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%2==0))
    {
        float4 m2=Filter(Scratch[local.x][local.y],Scratch[local.x+1][local.y],Scratch[local.x][local.y+1],Scratch[local.x+1][local.y+1]);
        Position2[uint3(id.xy/2,eye)]=m2;Scratch[local.x][local.y]=m2;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%4==0))
    {
        float4 m3=Filter(Scratch[local.x][local.y],Scratch[local.x+2][local.y],Scratch[local.x][local.y+2],Scratch[local.x+2][local.y+2]);
        Position3[uint3(id.xy/4,eye)]=m3;Scratch[local.x][local.y]=m3;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%8==0))
        Position4[uint3(id.xy/8,eye)]=Filter(Scratch[local.x][local.y],Scratch[local.x+4][local.y],Scratch[local.x][local.y+4],Scratch[local.x+4][local.y+4]);
}
