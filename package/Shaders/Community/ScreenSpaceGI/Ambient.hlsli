#ifndef CSAH_SCREEN_SPACE_GI_AMBIENT
#define CSAH_SCREEN_SPACE_GI_AMBIENT
#include "../PBR/PbrCommon.hlsli"
#include "../PBR/PbrIndirectMaterial.hlsli"
Texture2D<float> ScreenSpaceSurface : register(t64);
Texture2DArray<float4> ScreenSpaceDiffuse : register(t57);
Texture2DArray<float4> ScreenSpaceSpecular : register(t58);
Buffer<float4> ScreenSpaceParameters : register(t59);
Texture2D<float4> ScreenSpaceAlbedo : register(t60);
Texture2D<float4> ScreenSpaceAuthoredPbr : register(t61);
Texture2DArray<float4> ScreenSpaceGeometry : register(t62);
Texture2DArray<float4> ScreenSpaceNormal : register(t63);
void ScreenSpaceAmbient(float2 pixel,uint eye,out float3 visibility,out float3 diffuse,out float3 specular)
{
    visibility=1;diffuse=0;specular=0;
    uint count;ScreenSpaceParameters.GetDimensions(count);
    if(count>=4)
    {
        float4 rect=ScreenSpaceParameters[1];
        uint2 extent=asuint(ScreenSpaceParameters[0].xy);
        float2 local=(pixel-rect.xy)/rect.zw;
        local.x=local.x*2-eye;
        if(all(local>=0)&&all(local<1))
        {
            float2 coordinate=local*extent-.5;
            int2 base=int2(floor(coordinate));float2 fraction=frac(coordinate);
            uint depthWidth,depthHeight;NativeDepth.GetDimensions(depthWidth,depthHeight);
            int2 nativePixel=clamp(int2(pixel),0,int2(depthWidth,depthHeight)-1);
            float depth=NativeDepth.Load(int3(nativePixel,0)).x;
            float4 clip=NativeClip((pixel-rect.xy)/rect.zw,depth,eye);
            uint row=eye*4+(depth<=.01?40:32);
            float4 homogeneous=float4(dot(Camera[row],clip),dot(Camera[row+1],clip),dot(Camera[row+2],clip),dot(Camera[row+3],clip));
            float3 receiver=homogeneous.xyz/homogeneous.w;
            // The four taps are bilateral, stay in this eye, and never mix
            // compressed first-person depth with world surfaces.
            float3 normal=DecodeNativeNormal(NativeNormal.Load(int3(nativePixel,0)).xy);
            float3 sum=0,sumS=0;float ao=0,total=0;
            [unroll] for(uint tap=0;tap<4;tap++)
            {
                int2 p=clamp(base+int2(tap&1,tap>>1),0,int2(extent)-1);
                int4 location=int4(p,eye,0);
                float4 geometry=ScreenSpaceGeometry.Load(location);
                float3 n=ScreenSpaceNormal.Load(location).xyz;
                // Normal texture is stored in view space; the receiver helper
                // returns world normals, so rotate the tap by the same camera.
                n=normalize(float3(dot(Camera[20].xyz,n),dot(Camera[21].xyz,n),dot(Camera[22].xyz,n)));
                float weight=(tap&1?fraction.x:1-fraction.x)*(tap&2?fraction.y:1-fraction.y);
                weight*=pow(saturate(dot(normal,n)),16);
                weight*=geometry.w==(depth<=.01?-1:1)?1:0;
                weight*=exp(-abs(geometry.z-receiver.z)/max(1,abs(receiver.z)*.01));
                float4 d=ScreenSpaceDiffuse.Load(location);
                sum+=d.rgb*weight;ao+=d.w*weight;sumS+=ScreenSpaceSpecular.Load(location).rgb*weight;total+=weight;
            }
            if(total>1e-6)
            {
                float3 albedo=saturate(ScreenSpaceAlbedo.Load(int3(nativePixel,0)).rgb);
                float v=saturate(ao/total);
                // Jimenez multibounce approximation preserves saturated albedo.
                float3 a=2.0404*albedo-.3324,b=-4.7951*albedo+.6417,c=2.7552*albedo+.6903;
                visibility=max(v,((v*a+b)*v+c)*v);
                float4 authored=ScreenSpaceAuthoredPbr.Load(int3(nativePixel,0));
                float4 material=NativeMaterial.Load(int3(nativePixel,0));
                PbrIndirectMaterial response=DecodePbrIndirectMaterial(material,authored,albedo,
                    ScreenSpaceSurface.Load(int3(nativePixel,0)),
                    count>=8?ScreenSpaceParameters[4]:0, count>=8?ScreenSpaceParameters[5]:0,
                    count>=8?ScreenSpaceParameters[6]:0, count>=8?ScreenSpaceParameters[7]:0);
                diffuse=sum/total*response.diffuseShare/3;
                specular=sumS/total*response.f0;
            }
        }
    }
}
#endif
