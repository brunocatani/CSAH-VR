#define HEIGHT_FOG_ANALYTIC
#include "HeightFog.hlsli"
Texture2D<float3> Scene : register(t0);
Texture2D<float> Depth : register(t1);
struct Input {float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 main(Input input):SV_Target0
{
    float2 uv=(input.position.xy-FogOutputViewport.xy)/FogOutputViewport.zw;
    int2 pixel=int2(input.position.xy);
    float3 scene=Scene.Load(int3(pixel,0));
    float depth=Depth.Load(int3(int2(FogDepthViewport.xy+uv*FogDepthViewport.zw),0));
    return float4(ApplyHeightFog(scene,uv,depth,uv.x<.5?0:1,1),1);
}
