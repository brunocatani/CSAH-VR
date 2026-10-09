#ifndef CSAH_WATER_CAUSTICS
#define CSAH_WATER_CAUSTICS
// Adapted from community-shaders-shared WaterEffects/WaterCaustics.hlsli.
Texture2D<float> WaterCausticTexture : register(t65);
Buffer<float4> WaterParameters : register(t66);
SamplerState WaterCausticSampler : register(s15);
float3 CausticDispersion(float2 uv,float2 dispersion,float2 dx,float2 dy)
{
    float center=WaterCausticTexture.SampleGrad(WaterCausticSampler,uv,dx,dy);
    float3 dispersed=float3(WaterCausticTexture.SampleGrad(WaterCausticSampler,uv-dispersion*.75,dx,dy),center,
        WaterCausticTexture.SampleGrad(WaterCausticSampler,uv+dispersion,dx,dy));
    return lerp(center,dispersed,.5);
}
float3 WaterCausticMultiplier(float3 worldPosition)
{
    float3 result=1;
    uint count;WaterParameters.GetDimensions(count);
    [branch] if(count>=28 && WaterParameters[1].x>.5 && WaterParameters[1].w>.5 && all(isfinite(worldPosition)))
    {
        const int2 cell=int2(floor(worldPosition.xy/4096));
        float waterHeight=0;bool found=false;
        const int2 index=cell-int2(WaterParameters[2].xy)+2;
        if(all(index>=0) && all(index<5)) {
            float4 record=WaterParameters[3+index.x*5+index.y];
            if(record.w>.5 && all(int2(record.xy)==cell)) {waterHeight=record.z;found=true;}
        }
        float depth=waterHeight-worldPosition.z;
        [branch] if(found && depth>0)
        {
            float shore=saturate(depth/64),fade=1-saturate(depth/1024);fade*=fade;
            float2 uv=worldPosition.xy*.005,dx=ddx(uv),dy=ddy(uv);
            float2 dispersion=float2(.6,.8)*(.025*shore*saturate(depth/256));
            float time=WaterParameters[1].z;
            float3 high=min(CausticDispersion(uv+float2(time*.1,0),dispersion,dx,dy),
                CausticDispersion(uv*-.5+float2(time*.2,0),dispersion,dx*-.5,dy*-.5))*4;
            float3 low=min(CausticDispersion(uv*.5+float2(time*.05,0),dispersion*.5,dx*.5,dy*.5),
                CausticDispersion(uv*-.25+float2(time*.1,0),dispersion*.5,dx*-.25,dy*-.25))*4;
            result=lerp(1,lerp(low,high,fade),shore*saturate(WaterParameters[1].y));
        }
    }
    return result;
}
#endif
