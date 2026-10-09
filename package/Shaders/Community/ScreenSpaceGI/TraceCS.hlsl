// Visibility-bitmask slice integration adapted from community-shaders-shared
// ScreenSpaceGI/gi.cs.hlsl (FiveLimbedCat/ProfJack), derived from XeGTAO.
// Copyright (C) 2016-2021 Intel Corporation. SPDX-License-Identifier: MIT
// VR uses actual reconstructed positions for both native depth domains,
// isolated eye slices and reprojected prior-frame surface radiance.
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float4> Position : register(t0);
Texture2DArray<float4> Normal : register(t1);
Texture2DArray<float4> PreviousPosition : register(t2);
Texture2DArray<float4> PreviousNormal : register(t3);
Texture2DArray<float4> Radiance : register(t4);
RWTexture2DArray<float4> Diffuse : register(u0);
RWTexture2DArray<float4> Specular : register(u1);
bool HistoryGeometry(int3 p,float3 expected,float3 n)
{
    float4 previous=PreviousPosition.Load(int4(p,0));
    float3 previousNormal=PreviousNormal.Load(int4(p,0)).xyz;
    float3 worldNormal=WorldRelative(n);
    float3 expectedNormal=float3(dot(Previous[0].xyz,worldNormal),dot(Previous[1].xyz,worldNormal),dot(Previous[2].xyz,worldNormal));
    return previous.w>0 && distance(previous.xyz,expected)<max(1,abs(expected.z)*.015) && dot(previousNormal,expectedNormal)>.85;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    float4 center=Position.Load(int4(id,0));
    float4 surface=Normal.Load(int4(id,0));
    float3 n=surface.xyz,view=SafeNormal(-center.xyz);
    float visibility=0;float3 diffuse=0,specular=0;
    if(center.w!=0 && center.z<50000)
    {
        float pixelSize=max(length(Position.Load(int4(min(id.xy+uint2(1,0),Extent-1),id.z,0)).xyz-center.xyz),center.z/Extent.x);
        float radiusPixels=min(Radius/max(pixelSize,1e-4),max(Extent.x,Extent.y));
        float noise=frac(52.9829189*frac(dot(float2(id.xy),float2(.06711056,.00583715))+(EnableTemporal!=0?(Frame&63)*.006:0)));
        [loop] for(uint slice=0;slice<4;slice++)
        {
            float phi=Pi*(slice+noise)*.25;float2 direction=float2(cos(phi),sin(phi));
            float3 axis=SafeNormal(cross(float3(direction,0),view));
            float3 projected=n-axis*dot(n,axis);
            float angle=sign(dot(float3(direction,0),projected))*acos(saturate(dot(SafeNormal(projected),view)));
            uint mask=0,giMask=0;
            [unroll] for(int side=-1;side<=1;side+=2)
            [loop] for(uint step=0;step<8;step++)
            {
                float s=(step+.5+noise*.5)/8;
                int2 q=int2(float2(id.xy)+.5+direction*float2(1,-1)*side*max(1.3,s*s*radiusPixels));
                if(any(q<0)||any(q>=int2(Extent))) continue;
                float4 sample=Position.Load(int4(q,id.z,0));
                if(sample.w!=center.w) continue;
                float3 delta=sample.xyz-center.xyz;float distanceSq=dot(delta,delta);
                if(distanceSq<1e-6||distanceSq>Radius*Radius) continue;
                float3 horizon=SafeNormal(delta);
                // Integer sampling can leave the analytic slice plane. A
                // coplanar/below-surface point cannot occlude this hemisphere.
                if(dot(n,horizon)<=1e-4) continue;
                float front=acos(clamp(dot(horizon,view),-1,1));
                float back=acos(clamp(dot(SafeNormal(delta-view*Thickness),view),-1,1));
                float2 range=-side*(side==-1?float2(front,back):float2(back,front));
                uint covered=AngleMask(smoothstep(0,1,(range+angle)/Pi+.5));
                mask|=covered;
                uint fresh=covered&~giMask;giMask|=covered;
                if(EnableGI!=0 && fresh!=0 && center.w>0)
                {
                    float3 sampleNormal=Normal.Load(int4(q,id.z,0)).xyz;
                    int3 previousPixel;float3 expected;
                    if(PreviousPixel(sample.xyz,id.z,previousPixel,expected) && HistoryGeometry(previousPixel,expected,sampleNormal))
                    {
                        float3 light=max(0,Radiance.Load(int4(previousPixel,0)).rgb);
                        float weight=countbits(fresh)/32.0*saturate(-dot(sampleNormal,horizon));
                        diffuse+=light*weight*saturate(dot(n,horizon))*2;
                        if(EnableSpecular!=0)
                        {
                            float roughness=max(.2,surface.w),a=roughness*roughness;
                            float nh=saturate(dot(n,SafeNormal(view+horizon)));
                            float denominator=max(1e-3,nh*nh*(a*a-1)+1);
                            // Bounded GGX distribution; material Fresnel is applied by the ambient consumer.
                            float distribution=a*a/(Pi*denominator*denominator);
                            specular+=light*weight*min(distribution,8)*saturate(dot(n,horizon));
                        }
                    }
                }
            }
            visibility+=countbits(mask)/32.0;
        }
    }
    float fade=1-saturate((center.z-40000)/10000);
    Diffuse[id]=float4(min(diffuse*.25*GIStrength*fade,60000),pow(saturate(1-visibility*.25*fade),AOPower));
    Specular[id]=float4(min(specular*.25*GIStrength*fade,60000),1);
}
