// Reprojects history and bounce radiance into the current grid once per pixel,
// after upstream radianceDisocc.cs.hlsl. CSAH traces before the native
// composite, so its radiance source is the previous frame's surface lighting
// (CaptureCS); upstream reads the current frame's. A bilinear tap survives
// only when the previous surface lies within DepthDisocclusion of the
// expected position. On disocclusion the frame count halves instead of
// resetting, and fast motion lowers the accumulation ceiling.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float> Position : register(t0);
Texture2DArray<float> PreviousPosition : register(t1);
Texture2DArray<float4> PreviousRadiance : register(t2);
Texture2DArray<float4> PreviousLuma : register(t3);
Texture2DArray<float4> PreviousChroma : register(t4);
Texture2DArray<float4> PreviousSpecular : register(t5);
RWTexture2DArray<float4> Radiance : register(u0);
RWTexture2DArray<float4> Luma : register(u1);
RWTexture2DArray<float4> Chroma : register(u2);
RWTexture2DArray<float4> Specular : register(u3);
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    const float geometry=Position.Load(int4(id,0));
    float4 luma=0,chroma=0,specular=0;float3 radiance=0;float wsum=0;
    float2 uv=(id.xy+.5)/Extent,previousUv=uv;
    const float domain=sign(geometry);
    if(geometry!=0 && 1/abs(geometry)<DepthFadeStart+1/DepthFadeScale)
    {
        const float3 center=RebuildPosition(CurrentRays(id.z,geometry<0),id.xy+.5,geometry);
        const ViewRays previousRays=PreviousRays(id.z,geometry<0);
        float3 expected;
        if(PreviousUV(center,id.z,previousUv,expected))
        {
            float2 coordinate=previousUv*Extent-.5;
            int2 base=int2(floor(coordinate));float2 f=coordinate-base;
            float threshold=abs(expected.z)*DepthDisocclusion;
            [unroll] for(uint tap=0;tap<4;tap++)
            {
                int2 o=int2(tap&1,tap>>1);int2 p=base+o;
                if(any(p<0)||any(p>=int2(Extent))) continue;
                float weight=(o.x?f.x:1-f.x)*(o.y?f.y:1-f.y);
                const float oldGeometry=PreviousPosition.Load(int4(p,id.z,0));
                if(sign(oldGeometry)!=domain) continue;
                const float3 old=RebuildPosition(previousRays,p+.5,oldGeometry);
                if(abs(old.z-expected.z)>threshold*3) continue;
                float3 delta=old-expected;
                if(dot(delta,delta)>=threshold*threshold) continue;
                int4 t=int4(p,id.z,0);
                luma+=PreviousLuma.Load(t)*weight;chroma+=PreviousChroma.Load(t)*weight;specular+=PreviousSpecular.Load(t)*weight;
                float4 light=PreviousRadiance.Load(t);
                radiance+=light.w>0?light.rgb*weight:0;
                wsum+=weight;
            }
            if(wsum>1e-2) {float r=1/wsum;luma*=r;chroma*=r;specular*=r;radiance*=r;}
        }
    }
    float previousFrames=chroma.w;
    if(wsum<1e-2) previousFrames*=.5;
    float motion=length(previousUv-uv);
    float ceiling=lerp((float)MaxAccumFrames,max(MaxAccumFrames*.25,4),saturate(motion*20));
    chroma.w=EnableTemporal!=0?max(1,min(previousFrames+1,ceiling)):1;
    Radiance[id]=float4(max(0,radiance)*GIStrength,1);
    Luma[id]=luma;Chroma[id]=chroma;Specular[id]=specular;
}
