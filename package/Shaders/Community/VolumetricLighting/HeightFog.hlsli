#include "HeightFogMath.hlsli"
cbuffer HeightFogSettings : register(b4)
{
    // density per game unit, scale height, base altitude, clear start distance
    float4 HeightFogMedium;
    // enabled exterior, weather-density mix, opacity cap, volumetric visibility mix
    float4 HeightFogControl;
};
cbuffer HeightFogCamera : register(b2) {float4 FogCamera[77];};
#ifndef HEIGHT_FOG_ANALYTIC
cbuffer HeightFogStereo : register(b3) {float4 FogStereo;};
#else
cbuffer HeightFogStereo : register(b1) {float4 FogStereo;};
#endif
float3 HeightFogViewPosition(float2 uv,float depth,uint eye)
{
    float clipX=(uv.x*2-1+(eye==0?.5:-.5)*FogStereo.x)*(FogStereo.x+1);
    float4 clip=float4(clipX,1-uv.y*2,depth*1.01-.01,1);
    uint row=32+eye*4;
    float4 v=float4(dot(FogCamera[row],clip),dot(FogCamera[row+1],clip),dot(FogCamera[row+2],clip),dot(FogCamera[row+3],clip));
    return v.xyz/v.w;
}
float3 ApplyHeightFog(float3 scene,float2 uv,float depth,uint eye,float volumeVisibility)
{
    if(HeightFogControl.x<.5 || depth<=.01) return scene;
    float3 view=HeightFogViewPosition(uv,min(depth,.99999),eye);
    float lengthToReceiver=length(view);
    if(!all(isfinite(view)) || !(lengthToReceiver>HeightFogMedium.w)) return scene;
    float distance=min(lengthToReceiver,200000);
    float3 worldDirection=normalize(float3(dot(FogCamera[20].xyz,view),dot(FogCamera[21].xyz,view),dot(FogCamera[22].xyz,view)));
    float eyeHeight=FogCamera[59+eye].z;
    float weather=lerp(1,clamp(FogCamera[76].x*10000,.1,4),HeightFogControl.y);
    float alpha=HeightFogOpacity(eyeHeight+worldDirection.z*HeightFogMedium.w,eyeHeight+worldDirection.z*distance,
        max(0,distance-HeightFogMedium.w),HeightFogMedium.x*weather,HeightFogMedium.z,HeightFogMedium.y,HeightFogControl.z);
    // Native per-frame far fog colour already follows weather/time transitions.
    float3 colour=max(FogCamera[73].xyz,0);
    if(!all(isfinite(colour))) return scene;
    colour*=lerp(1,saturate(volumeVisibility),HeightFogControl.w);
    return lerp(scene,colour,alpha);
}
