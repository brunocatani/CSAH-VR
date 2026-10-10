#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float4> Position : register(t0);
Texture2DArray<float4> Normal : register(t1);
Texture2DArray<float4> PreviousPosition : register(t2);
Texture2DArray<float4> PreviousNormal : register(t3);
Texture2DArray<float4> CurrentDiffuse : register(t4);
Texture2DArray<float4> CurrentSpecular : register(t5);
Texture2DArray<float4> PreviousDiffuse : register(t6);
Texture2DArray<float4> PreviousSpecular : register(t7);
RWTexture2DArray<float4> Diffuse : register(u0);
RWTexture2DArray<float4> Specular : register(u1);
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    float4 center=Position.Load(int4(id,0));float3 n=Normal.Load(int4(id,0)).xyz;
    float4 d=0,s=0,low=1e30,high=-1e30,lowS=1e30,highS=-1e30;float total=0;
    [unroll] for(int y=-1;y<=1;y++)
    [unroll] for(int x=-1;x<=1;x++)
    {
        int3 p=int3(clamp(int2(id.xy)+int2(x,y),0,int2(Extent)-1),id.z);
        float4 position=Position.Load(int4(p,0));float3 normal=Normal.Load(int4(p,0)).xyz;
        float weight=exp(-abs(dot(position.xyz-center.xyz,n))/max(1,center.z*.005))*pow(saturate(dot(n,normal)),16);
        weight*=position.w==center.w?1:0;
        if(x==0 && y==0) weight=1;
        float4 sampleD=CurrentDiffuse.Load(int4(p,0)),sampleS=float4(0,0,0,1);
        // Trace writes neutral specular when either contribution is disabled.
        // Keep PreviousSpecular's sample count below: AO temporal history uses it.
        [branch] if(EnableGI!=0 && EnableSpecular!=0)
            sampleS=CurrentSpecular.Load(int4(p,0));
        d+=sampleD*weight;s+=sampleS*weight;total+=weight;
        if(weight>.1) {low=min(low,sampleD);high=max(high,sampleD);lowS=min(lowS,sampleS);highS=max(highS,sampleS);}
    }
    d/=total;s/=total;
    int3 previousPixel;float3 expected;float count=1;
    if(EnableTemporal!=0 && center.w>0 && PreviousPixel(center.xyz,id.z,previousPixel,expected))
    {
        float4 old=PreviousPosition.Load(int4(previousPixel,0));
        float3 worldNormal=WorldRelative(n);
        float3 previousN=float3(dot(Previous[0].xyz,worldNormal),dot(Previous[1].xyz,worldNormal),dot(Previous[2].xyz,worldNormal));
        float3 oldN=PreviousNormal.Load(int4(previousPixel,0)).xyz;
        if(old.w>0 && distance(old.xyz,expected)<max(1,abs(expected.z)*.015) && dot(previousN,oldN)>.85)
        {
            float4 oldD=PreviousDiffuse.Load(int4(previousPixel,0));
            float4 oldS=PreviousSpecular.Load(int4(previousPixel,0));
            count=min(16,oldS.w+1);
            d=lerp(clamp(oldD,low,high),d,1/count);
            s.rgb=lerp(clamp(oldS.rgb,lowS.rgb,highS.rgb),s.rgb,1/count);
        }
    }
    Diffuse[id]=d;Specular[id]=float4(s.rgb,count);
}
