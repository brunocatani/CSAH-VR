// Visibility-bitmask slice integration after upstream Community Shaders
// ScreenSpaceGI/gi.cs.hlsl (FiveLimbedCat/ProfJack), derived from XeGTAO.
// Copyright (C) 2016-2021 Intel Corporation. SPDX-License-Identifier: MIT
// Bounce light is stored as L1 spherical-harmonic luma plus YCoCg chroma so
// the consumer evaluates it against the receiving normal. CSAH keeps three
// VR rules: first-person and world depth never occlude each other, samples
// on the receiver's own plane are skipped, and AO is temporally accumulated
// with the bounce (upstream leaves AO per-frame).
#include "ScreenSpaceGICommon.hlsli"
Texture2DArray<float> Position : register(t0);
Texture2DArray<float4> Normal : register(t1);
Texture2DArray<float4> Radiance : register(t2);
Texture2D<float2> Noise : register(t3);
Texture2DArray<float4> HistoryLuma : register(t4);
Texture2DArray<float4> HistoryChroma : register(t5);
Texture2DArray<float4> HistorySpecular : register(t6);
Texture2D<float4> NativeEmissive : register(t7);
RWTexture2DArray<float4> Luma : register(u0);
RWTexture2DArray<float4> Chroma : register(u1);
RWTexture2DArray<float4> Specular : register(u2);
float DistributionGGX(float roughness,float nh)
{
    float a=roughness*roughness,a2=a*a;
    float d=max((nh*a2-nh)*nh+1,1e-5);
    return a2/(Pi*d*d);
}
float VisibilitySmithJoint(float roughness,float nv,float nl)
{
    float a=roughness*roughness;
    float v=nl*(nv*(1-a)+a)+nv*(nl*(1-a)+a);
    return v>0?.5/v:0;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
    if(any(id.xy>=Extent)||id.z>=2) return;
    const uint eye=id.z;
    const float geometry=Position.Load(int4(id,0));
    const float domain=sign(geometry);
    const ViewRays rays=CurrentRays(eye,geometry<0);
    const float3 center=geometry!=0?RebuildPosition(rays,id.xy+.5,geometry):0;
    float4 surface=Normal.Load(int4(id,0));
    float3 n=surface.xyz,view=SafeNormal(-center);
    float occlusion=0,specularOcclusion=0;float4 luma=0;float2 chroma=0;float3 specular=0;
    const float fadeEnd=DepthFadeStart+1/DepthFadeScale;
    const bool traced=geometry!=0 && center.z<fadeEnd;
    if(traced)
    {
        const bool gi=EnableGI!=0 && geometry>0;
        const bool specularGI=gi && EnableSpecular!=0;
        const float roughness=max(.2,saturate(surface.w));
        const float nv=clamp(dot(view,n),1e-5,1);
        const float pixelSize=center.z*PixelViewScale(rays);
        const float screenRadius=max(MinScreenRadius,EffectRadius/max(pixelSize,1e-6));
        const float minS=1.3/screenRadius;
        const float rcpSlices=1.0/NumSlices,rcpSteps=1.0/NumSteps;
        // EA fastnoise spatiotemporal blue noise, 128x128 x 64 frames.
        const float2 noise=Noise.Load(int3(id.xy%128+uint2(0,((EnableTemporal!=0?Frame:0)%64)*128),0));
        const float minimumRadianceMip=min(1+ResolutionMode,MipCount-1);
        [loop] for(uint slice=0;slice<NumSlices;slice++)
        {
            float phi=Pi*rcpSlices*(slice+noise.x);
            float3 direction=float3(cos(phi),sin(phi),0);
            float2 omega=float2(direction.x,-direction.y)*screenRadius;
            float logLengthOmega=.5*log2(max(dot(omega,omega),1e-8));
            float3 ortho=direction-dot(direction,view)*view;
            float3 axis=SafeNormal(cross(ortho,view));
            float3 projected=n-axis*dot(n,axis);
            float angle=sign(dot(ortho,projected))*acos(saturate(dot(projected,view)*rsqrt(max(dot(projected,projected),1e-8))));
            float dominantAngle=0;
            if(specularGI)
            {
                float3 dominant=SpecularDominantDirection(n,view,roughness);
                float3 projectedDominant=SafeNormal(dominant-axis*dot(dominant,axis));
                dominantAngle=sign(dot(ortho,projectedDominant))*acos(saturate(dot(projectedDominant,view)));
            }
            uint mask=0,giMask=0,specularMask=0;
            float stepNoise=frac(noise.y+slice*.6180339887498948);
            [unroll] for(int side=-1;side<=1;side+=2)
            [loop] for(uint step=0;step<NumSteps;step++)
            {
                float s=(step+stepNoise)*rcpSteps;s*=s;s+=minS;
                float2 pixel=float2(id.xy)+.5+s*omega*side;
                if(any(pixel<0)||any(pixel>=float2(Extent))) continue;
                float mip=clamp(log2(s)+logLengthOmega-3.3,0,MipCount-1);
                const int4 texel=MipTexel(pixel,eye,(uint)round(mip));
                const float sampleGeometry=Position.Load(texel);
                if(sign(sampleGeometry)!=domain) continue;
                const float3 sample=RebuildPosition(rays,MipTexelCentre(texel),sampleGeometry);
                float3 delta=sample-center;
                if(dot(delta,delta)<1e-6) continue;
                float3 horizon=SafeNormal(delta);
                // Integer sampling can leave the analytic slice plane. A
                // coplanar/below-surface point cannot occlude this hemisphere.
                if(dot(n,horizon)<=1e-4) continue;
                float front=acos(clamp(dot(horizon,view),-1,1));
                float back=acos(clamp(dot(SafeNormal(delta-view*Thickness),view),-1,1));
                float2 range=-side*(side==-1?float2(front,back):float2(back,front));
                uint covered=s<AORadius?AngleMask(smoothstep(0,1,(range+angle)/Pi+.5)):0;
                uint giCovered=0,specularCovered=0;
                if(gi)
                {
                    // Bounce light uses a deeper surface (upstream: 300 units).
                    float backGI=acos(clamp(dot(SafeNormal(delta-view*300),view),-1,1));
                    float2 rangeGI=-side*(side==-1?float2(front,backGI):float2(backGI,front));
                    if(specularGI)
                    {
                        float cone=max(5e-2,SpecularLobeHalfAngle(roughness));
                        float2 rangeSpecular=clamp((rangeGI+dominantAngle)*.5/cone,-1,1)*.5+.5;
                        specularCovered=s<GIRadius?AngleMask(rangeSpecular):0;
                    }
                    giCovered=s<GIRadius?AngleMask(smoothstep(0,1,(rangeGI+angle)/Pi+.5)):0;
                    uint fresh=giCovered&~giMask,freshSpecular=specularCovered&~specularMask;
                    if(fresh!=0 || freshSpecular!=0)
                    {
                        float3 sampleNormal=Normal.Load(int4(clamp(int2(pixel),0,int2(Extent)-1),eye,0)).xyz;
                        if(dot(sample,sampleNormal)>0) sampleNormal=-sampleNormal;
                        float facing=max(0,-dot(sampleNormal,horizon));
                        if(facing>0)
                        {
                            float boost=4*Pi*(1+GIDistanceCompensation*smoothstep(0,GICompensationMaxDist,s*EffectRadius));
                            float3 light=max(0,Radiance.Load(MipTexel(pixel,eye,(uint)round(max(mip,minimumRadianceMip)))).rgb)*facing*boost*countbits(fresh)*.03125;
                            float3 ycocg=RGBToYCoCg(light);
                            luma+=ycocg.x*SHEvaluate(SafeNormal(WorldRelative(horizon)));
                            chroma+=ycocg.yz;
                            if(specularGI)
                            {
                                float nh=clamp(dot(n,SafeNormal(view+horizon)),1e-2,1),nl=clamp(dot(n,horizon),1e-2,1);
                                specular+=max(0,light*countbits(freshSpecular)*.03125*DistributionGGX(roughness,nh)*VisibilitySmithJoint(roughness,nv,nl));
                            }
                        }
                    }
                }
                mask|=covered;giMask|=giCovered;specularMask|=specularCovered;
            }
            occlusion+=countbits(mask)*.03125;
            specularOcclusion+=countbits(specularMask)*.03125;
        }
        const float fade=saturate((center.z-DepthFadeStart)*DepthFadeScale);
        occlusion=lerp(saturate(occlusion*rcpSlices),0,fade);
        luma=lerp(luma*rcpSlices,0,fade);
        chroma*=rcpSlices*GISaturation;
        specular=lerp(specular*rcpSlices,0,fade);
        specularOcclusion=lerp(saturate(specularOcclusion*rcpSlices),0,fade);
    }
    float visibility=pow(saturate(1-occlusion),AOPower);
    // Native vertex AO already darkens the lit surface; divide it out so
    // SSGI does not occlude the same crease twice (upstream Masks2.x).
    uint w,h;NativeEmissive.GetDimensions(w,h);
    float vertexAO=1-NativeEmissive.Load(int3(clamp(int2(PackedPixel(id)),0,int2(w,h)-1),0)).a;
    if(w!=0) visibility=saturate(visibility/max(vertexAO,1e-3));
    float4 historyChroma=HistoryChroma.Load(int4(id,0));
    float frames=traced?max(historyChroma.w,1):1;
    float blend=1/frames;
    float4 previousLuma=HistoryLuma.Load(int4(id,0));
    float3 previousChroma=historyChroma.xyz;
    float3 current=float3(chroma,visibility);
    // Clamp history to the local neighbourhood (SVGF, Schied 2017) while it
    // still weighs in strongly; saturated history is self-stabilised.
    [branch] if(blend<1 && blend>=.15)
    {
        float4 lowL=luma,highL=luma;float3 lowC=current,highC=current;
        [unroll] for(uint tap=0;tap<4;tap++)
        {
            int2 o=tap==0?int2(-1,0):tap==1?int2(1,0):tap==2?int2(0,-1):int2(0,1);
            int4 t=int4(clamp(int2(id.xy)+o,0,int2(Extent)-1),eye,0);
            float4 l=HistoryLuma.Load(t);float3 c=HistoryChroma.Load(t).xyz;
            lowL=min(lowL,l);highL=max(highL,l);lowC=min(lowC,c);highC=max(highC,c);
        }
        previousLuma=clamp(previousLuma,lowL,highL);previousChroma=clamp(previousChroma,lowC,highC);
    }
    luma=lerp(previousLuma,luma,blend);
    current=lerp(previousChroma,current,blend);
    float4 specularOut=lerp(HistorySpecular.Load(int4(id,0)),float4(specular,specularOcclusion),blend);
    // Upstream filterNaN: a non-finite sample must not enter the history.
    if(any(!isfinite(luma))) luma=0;
    if(any(!isfinite(current))) current=float3(0,0,1);
    if(any(!isfinite(specularOut))) specularOut=0;
    Luma[id]=min(luma,60000);
    Chroma[id]=float4(clamp(current.xy,-60000,60000),current.z,frames);
    Specular[id]=float4(min(specularOut.rgb,60000),specularOut.w);
}
