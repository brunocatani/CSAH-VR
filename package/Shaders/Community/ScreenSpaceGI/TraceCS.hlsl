// Visibility-bitmask slice integration after upstream Community Shaders
// ScreenSpaceGI/gi.cs.hlsl (FiveLimbedCat/ProfJack), derived from XeGTAO.
// Copyright (C) 2016-2021 Intel Corporation. SPDX-License-Identifier: MIT
// Bounce light is stored as L1 spherical-harmonic luma plus YCoCg chroma so
// the consumer evaluates it against the receiving normal. CSAH keeps three
// VR rules: first-person and world depth never occlude each other, samples
// on the receiver's own plane are skipped, and AO is temporally accumulated
// with the bounce (upstream leaves AO per-frame).
#include "ScreenSpaceGICommon.hlsli"
// The runtime embeds three builds of this kernel.
//
// CSAH_SSGI_SPECULAR_TRACE 1 (the experimental specular trace) is view
// dependent, so it traces both eyes in one dispatch. Without the specular
// cone, NDF and its masks the kernel needs fewer registers, so more of it
// runs at once.
//
// With the specular trace off, occlusion and bounce are properties of the
// surface point (the luma is world-space SH), so the two eyes share them:
// CSAH_SSGI_STEREO 1 traces the frame's primary eye (Frame parity) and also
// stores its estimate before temporal accumulation; CSAH_SSGI_STEREO 2 then
// resolves the other eye, taking that estimate wherever the primary eye sees
// the same surface and tracing only where it does not. The primary eye
// alternates, so every pixel receives a fresh estimate every frame and each
// eye keeps its own history on its own grid.
#ifndef CSAH_SSGI_SPECULAR_TRACE
#define CSAH_SSGI_SPECULAR_TRACE 1
#endif
#ifndef CSAH_SSGI_STEREO
#define CSAH_SSGI_STEREO 0
#endif
Texture2DArray<float> Position : register(t0);
Texture2DArray<float4> Normal : register(t1);
Texture2DArray<float4> Radiance : register(t2);
Texture2D<float2> Noise : register(t3);
Texture2DArray<float4> HistoryLuma : register(t4);
Texture2DArray<float4> HistoryChroma : register(t5);
Texture2D<float4> NativeEmissive : register(t7);
RWTexture2DArray<float4> Luma : register(u0);
RWTexture2DArray<float4> Chroma : register(u1);
#if CSAH_SSGI_SPECULAR_TRACE
Texture2DArray<float4> HistorySpecular : register(t6);
RWTexture2DArray<float4> Specular : register(u2);
#endif
// The primary eye's estimate of this frame: luma SH, then chroma and the
// visibility before vertex AO is divided out.
#if CSAH_SSGI_STEREO==1
RWTexture2DArray<float4> EstimateLuma : register(u2);
RWTexture2DArray<float4> EstimateChroma : register(u3);
#elif CSAH_SSGI_STEREO==2
Texture2DArray<float4> PrimaryLuma : register(t8);
Texture2DArray<float4> PrimaryChroma : register(t9);
#endif
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
#if CSAH_SSGI_STEREO==2
// The primary eye's estimate at a world surface point of this eye: four
// bilinear taps, each kept only when the primary eye holds the same surface
// there (within half a percent of its depth of the receiver's plane, facing
// the same way). First-person surfaces are never shared: their depth range
// has its own projection. False when under half the footprint qualifies.
bool PrimaryEstimate(float3 center,float3 n,uint eye,out float4 luma,out float3 chroma)
{
    luma=0;chroma=0;
    const uint primary=1-eye;
    const float3 relative=WorldRelative(center)+Camera[59+eye].xyz-Camera[59+primary].xyz;
    const float4 expected=float4(dot(Camera[0].xyz,relative),dot(Camera[1].xyz,relative),dot(Camera[2].xyz,relative),1);
    const uint r=4+primary*4;
    const float4 clip=float4(dot(Camera[r],expected),dot(Camera[r+1],expected),dot(Camera[r+2],expected),dot(Camera[r+3],expected));
    if(!(clip.w>0)) return false;
    const float2 coordinate=(clip.xy/clip.w*float2(.5,-.5)+.5)*Extent-.5;
    if(!all(isfinite(coordinate))) return false;
    const int2 base=int2(floor(coordinate));const float2 f=coordinate-base;
    const ViewRays rays=CurrentRays(primary,false);
    const float tolerance=expected.z*.005;
    float total=0;
    [unroll] for(uint tap=0;tap<4;tap++)
    {
        const int2 o=int2(tap&1,tap>>1);const int2 p=base+o;
        if(any(p<0)||any(p>=int2(Extent))) continue;
        const int4 t=int4(p,primary,0);
        const float geometry=Position.Load(t);
        if(!(geometry>0)) continue;
        if(abs(dot(n,RebuildPosition(rays,p+.5,geometry)-expected.xyz))>tolerance) continue;
        if(dot(Normal.Load(t).xyz,n)<.7) continue;
        const float weight=(o.x?f.x:1-f.x)*(o.y?f.y:1-f.y);
        luma+=PrimaryLuma.Load(t)*weight;chroma+=PrimaryChroma.Load(t).xyz*weight;total+=weight;
    }
    if(total<.5) {luma=0;chroma=0;return false;}
    luma/=total;chroma/=total;
    return true;
}
#endif
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID)
{
#if CSAH_SSGI_STEREO==0
    if(any(id.xy>=Extent)||id.z>=2) return;
    const uint eye=id.z;
#else
    if(any(id.xy>=Extent)||id.z!=0) return;
    const uint eye=(Frame&1)^(CSAH_SSGI_STEREO-1);
#endif
    const uint3 texel=uint3(id.xy,eye);
    const float geometry=Position.Load(int4(texel,0));
    const float domain=sign(geometry);
    const ViewRays rays=CurrentRays(eye,geometry<0);
    const float3 center=geometry!=0?RebuildPosition(rays,id.xy+.5,geometry):0;
    float4 surface=Normal.Load(int4(texel,0));
    float3 n=surface.xyz,view=SafeNormal(-center);
    float occlusion=0,specularOcclusion=0;float4 luma=0;float2 chroma=0;float3 specular=0;
    const float fadeEnd=DepthFadeStart+1/DepthFadeScale;
    const bool traced=geometry!=0 && center.z<fadeEnd;
    bool trace=traced;
#if CSAH_SSGI_STEREO==2
    float3 primaryEstimate=float3(0,0,1);
    [branch] if(traced && geometry>0) trace=!PrimaryEstimate(center,n,eye,luma,primaryEstimate);
#endif
    [branch] if(trace)
    {
        const bool gi=EnableGI!=0 && geometry>0;
        const bool specularGI=CSAH_SSGI_SPECULAR_TRACE && gi && EnableSpecular!=0;
        const float roughness=max(.2,saturate(surface.w));
        const float nv=clamp(dot(view,n),1e-5,1);
        const float pixelSize=center.z*PixelViewScale(rays);
        const float screenRadius=max(MinScreenRadius,EffectRadius/max(pixelSize,1e-6));
        const float minS=1.3/screenRadius;
        // Adaptive sampling after upstream gi.cs.hlsl: a pixel whose four
        // neighbours share its depth and normal has nothing nearby to
        // occlude it, so it takes down to 55% of the samples per direction,
        // and pixels inside the distance fade down to 45%. A neighbour in
        // the other depth domain, or with nothing rendered, counts as an edge.
        uint stepCount=NumSteps;
        [branch] if(AdaptiveSampling!=0)
        {
            float depthVariance=0,normalVariance=0;
            [unroll] for(uint neighbour=0;neighbour<4;neighbour++)
            {
                const int2 o=neighbour==0?int2(-1,0):neighbour==1?int2(1,0):neighbour==2?int2(0,-1):int2(0,1);
                const int4 t=int4(clamp(int2(id.xy)+o,0,int2(Extent)-1),eye,0);
                const float neighbourGeometry=Position.Load(t);
                depthVariance+=sign(neighbourGeometry)==domain?abs(1/abs(neighbourGeometry)-center.z):center.z;
                normalVariance+=1-saturate(dot(n,Normal.Load(t).xyz));
            }
            const float variance=saturate(depthVariance*.25/max(center.z,1)*6+normalVariance*.25*2);
            const float farScale=lerp(1,.45,saturate((center.z-DepthFadeStart)*DepthFadeScale));
            stepCount=clamp((uint)round(NumSteps*min(farScale,lerp(.55,1,variance))),1,NumSteps);
        }
        const float rcpSlices=1.0/NumSlices,rcpSteps=1.0/stepCount;
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
            [loop] for(uint step=0;step<stepCount;step++)
            {
                float s=(step+stepNoise)*rcpSteps;s*=s;s+=minS;
                float2 pixel=float2(id.xy)+.5+s*omega*side;
                if(any(pixel<0)||any(pixel>=float2(Extent))) continue;
                float mip=clamp(log2(s)+logLengthOmega-3.3,0,MipCount-1);
                const int4 sampleTexel=MipTexel(pixel,eye,(uint)round(mip));
                const float sampleGeometry=Position.Load(sampleTexel);
                if(sign(sampleGeometry)!=domain) continue;
                const float3 sample=RebuildPosition(rays,MipTexelCentre(sampleTexel),sampleGeometry);
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
#if CSAH_SSGI_STEREO==2
    if(traced && !trace) {chroma=primaryEstimate.xy;visibility=primaryEstimate.z;}
#elif CSAH_SSGI_STEREO==1
    // A non-finite estimate must not reach the other eye either.
    const float4 estimateLuma=luma;const float3 estimate=float3(chroma,visibility);
    EstimateLuma[texel]=all(isfinite(estimateLuma))?clamp(estimateLuma,-60000,60000):0;
    EstimateChroma[texel]=all(isfinite(estimate))?float4(clamp(estimate.xy,-60000,60000),estimate.z,1):float4(0,0,1,1);
#endif
    // Native vertex AO already darkens the lit surface; divide it out so
    // SSGI does not occlude the same crease twice (upstream Masks2.x).
    uint w,h;NativeEmissive.GetDimensions(w,h);
    float vertexAO=1-NativeEmissive.Load(int3(clamp(int2(PackedPixel(texel)),0,int2(w,h)-1),0)).a;
    if(w!=0) visibility=saturate(visibility/max(vertexAO,1e-3));
    float4 historyChroma=HistoryChroma.Load(int4(texel,0));
    float frames=traced?max(historyChroma.w,1):1;
    float blend=1/frames;
    float4 previousLuma=HistoryLuma.Load(int4(texel,0));
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
    // Upstream filterNaN: a non-finite sample must not enter the history.
    if(any(!isfinite(luma))) luma=0;
    if(any(!isfinite(current))) current=float3(0,0,1);
    Luma[texel]=min(luma,60000);
    Chroma[texel]=float4(clamp(current.xy,-60000,60000),current.z,frames);
#if CSAH_SSGI_SPECULAR_TRACE
    float4 specularOut=lerp(HistorySpecular.Load(int4(texel,0)),float4(specular,specularOcclusion),blend);
    if(any(!isfinite(specularOut))) specularOut=0;
    Specular[texel]=float4(min(specularOut.rgb,60000),specularOut.w);
#endif
}
