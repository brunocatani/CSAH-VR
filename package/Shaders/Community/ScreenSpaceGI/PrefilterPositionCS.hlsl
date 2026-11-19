// Inverse-depth mip chain for distance-scaled horizon sampling, after upstream
// prefilterDepths.cs.hlsl (linear filter). A texel averages the world
// surfaces it covers; only when none are present does it average first-person
// surfaces, so the two depth domains never blend. Averaging inverse depth
// keeps a plane exact at the texel's footprint centre.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float> Position0 : register(t0);
RWTexture2DArray<float> Position1 : register(u0);
RWTexture2DArray<float> Position2 : register(u1);
RWTexture2DArray<float> Position3 : register(u2);
RWTexture2DArray<float> Position4 : register(u3);
float Filter(float a,float b,float c,float d)
{
    float2 world=0,firstPerson=0;
    world+=a>0?float2(a,1):0;world+=b>0?float2(b,1):0;world+=c>0?float2(c,1):0;world+=d>0?float2(d,1):0;
    firstPerson+=a<0?float2(a,1):0;firstPerson+=b<0?float2(b,1):0;firstPerson+=c<0?float2(c,1):0;firstPerson+=d<0?float2(d,1):0;
    if(world.y>0) return world.x/world.y;
    if(firstPerson.y>0) return firstPerson.x/firstPerson.y;
    return 0;
}
groupshared float Scratch[8][8];
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID)
{
    const uint eye=id.z;
    const int2 limit=MipDimensions(0)-1;
    const int2 p=int2(id.xy)*2;
    float m1=Filter(Position0.Load(int4(min(p,limit),eye,0)),Position0.Load(int4(min(p+int2(1,0),limit),eye,0)),
        Position0.Load(int4(min(p+int2(0,1),limit),eye,0)),Position0.Load(int4(min(p+1,limit),eye,0)));
    Position1[uint3(id.xy,eye)]=m1;
    Scratch[local.x][local.y]=m1;
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%2==0))
    {
        float m2=Filter(Scratch[local.x][local.y],Scratch[local.x+1][local.y],Scratch[local.x][local.y+1],Scratch[local.x+1][local.y+1]);
        Position2[uint3(id.xy/2,eye)]=m2;Scratch[local.x][local.y]=m2;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%4==0))
    {
        float m3=Filter(Scratch[local.x][local.y],Scratch[local.x+2][local.y],Scratch[local.x][local.y+2],Scratch[local.x+2][local.y+2]);
        Position3[uint3(id.xy/4,eye)]=m3;Scratch[local.x][local.y]=m3;
    }
    GroupMemoryBarrierWithGroupSync();
    [branch] if(all(local.xy%8==0))
        Position4[uint3(id.xy/8,eye)]=Filter(Scratch[local.x][local.y],Scratch[local.x+4][local.y],Scratch[local.x][local.y+4],Scratch[local.x+4][local.y+4]);
}
