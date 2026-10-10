#include "HeightFogMath.hlsli"
cbuffer HeightFogSettings : register(b4)
{
    // density per game unit, scale height, base altitude, clear start distance
    float4 HeightFogMedium;
    // enabled exterior, weather-density mix, opacity cap, volumetric visibility mix
    float4 HeightFogControl;
    // Output and depth can have different active regions during upscaling.
    float4 FogOutputViewport;
    float4 FogDepthViewport;
};
cbuffer HeightFogCamera : register(b2) {float4 FogCamera[77];};
float3 HeightFogViewPosition(float2 uv,float depth,uint eye)
{
    // This host is a packed stereo image, irrespective of the preceding
    // geometry pass's b8 stereo flag. Each inverse projection takes local NDC.
    float clipX=(uv.x-(eye==0?0:.5))*4-1;
    float4 clip=float4(clipX,1-uv.y*2,depth*1.01-.01,1);
    uint row=32+eye*4;
    float4 v=float4(dot(FogCamera[row],clip),dot(FogCamera[row+1],clip),dot(FogCamera[row+2],clip),dot(FogCamera[row+3],clip));
    return v.xyz/v.w;
}
float3 ApplyHeightFog(float3 scene,float2 uv,float depth,uint eye,float volumeVisibility)
{
    if(HeightFogControl.x<.5 || depth<=.01) return scene;
    // Cleared sky depth is a direction, not a finite receiver. Reconstruct the
    // angular ray at a stable clip plane; never divide by its far-plane w.
    float3 ray=HeightFogViewPosition(uv,.5,eye);
    float3 view=depth>=1 ? normalize(ray)*200000 : HeightFogViewPosition(uv,depth,eye);
    float lengthToReceiver=length(view);
    if(!all(isfinite(view)) || !(lengthToReceiver>HeightFogMedium.w)) return scene;
    float distance=min(lengthToReceiver,200000);
    float3 worldDirection=normalize(float3(dot(FogCamera[20].xyz,ray),dot(FogCamera[21].xyz,ray),dot(FogCamera[22].xyz,ray)));
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
