#define HEIGHT_FOG_ANALYTIC
#include "HeightFog.hlsli"
Texture2D<float3> Scene : register(t0);
Texture2D<float> Depth : register(t1);
struct Input {float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 main(Input input):SV_Target0
{
    uint width,height;Depth.GetDimensions(width,height);
    int2 pixel=min(int2(input.position.xy),int2(width,height)-1);
    float3 scene=Scene.Load(int3(pixel,0));
    float depth=Depth.Load(int3(pixel,0));
    return float4(ApplyHeightFog(scene,input.position.xy/float2(width,height),depth,pixel.x<int(width/2)?0:1,1),1);
}
