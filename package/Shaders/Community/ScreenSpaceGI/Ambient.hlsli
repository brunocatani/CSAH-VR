#ifndef CSAH_SCREEN_SPACE_GI_AMBIENT
#define CSAH_SCREEN_SPACE_GI_AMBIENT
// Full-resolution SSGI consumer for the ambient pass and ComposeOcclusion.
// Four bilateral taps upsample the per-eye internal-resolution history in
// place of upstream's upsample pass; the bounce is then evaluated against the
// receiver's own normal, after upstream ScreenSpaceGIConsumer.hlsli.
#include "../PBR/PbrCommon.hlsli"
#include "../PBR/PbrIndirectMaterial.hlsli"
#include "../ScreenSpaceGI/ScreenSpaceGIMath.hlsli"
Texture2D<float> ScreenSpaceSurface : register(t64);
Texture2DArray<float4> ScreenSpaceLuma : register(t57);
Texture2DArray<float4> ScreenSpaceSpecular : register(t58);
Buffer<float4> ScreenSpaceParameters : register(t59);
Texture2D<float4> ScreenSpaceAlbedo : register(t60);
Texture2D<float4> ScreenSpaceAuthoredPbr : register(t61);
Texture2DArray<float4> ScreenSpaceGeometry : register(t62);
Texture2DArray<float4> ScreenSpaceNormal : register(t63);
Texture2DArray<float4> ScreenSpaceChroma : register(t65);
// The parameter buffer holds the SSGI constants block as float4 rows:
// [0] extent, [1] viewport rect, [3] feature switches, [8..11] PBR rows.
static const uint ScreenSpaceParameterCount=12;
struct ScreenSpaceAmbientResult
{
    float3 diffuseVisibility;
    float3 diffuse;
    float specularVisibility;
    float3 specular;
};
ScreenSpaceAmbientResult ScreenSpaceAmbient(float2 pixel,uint eye)
{
    ScreenSpaceAmbientResult result;
    result.diffuseVisibility=1;result.diffuse=0;result.specularVisibility=1;result.specular=0;
    uint count;ScreenSpaceParameters.GetDimensions(count);
    if(count<ScreenSpaceParameterCount) return result;
    float4 rect=ScreenSpaceParameters[1];
    uint2 extent=asuint(ScreenSpaceParameters[0].xy);
    uint enableGI=asuint(ScreenSpaceParameters[3].x),enableSpecular=asuint(ScreenSpaceParameters[3].y);
    float2 local=(pixel-rect.xy)/rect.zw;
    local.x=local.x*2-eye;
    if(any(local<0)||any(local>=1)) return result;
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
    float4 luma=0,chroma=0,specular=0;float total=0;
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
        luma+=ScreenSpaceLuma.Load(location)*weight;chroma+=ScreenSpaceChroma.Load(location)*weight;
        specular+=ScreenSpaceSpecular.Load(location)*weight;total+=weight;
    }
    if(total<=1e-6) return result;
    luma/=total;chroma/=total;specular/=total;
    float3 albedo=saturate(ScreenSpaceAlbedo.Load(int3(nativePixel,0)).rgb);
    float v=saturate(chroma.z);
    // Jimenez multibounce approximation preserves saturated albedo.
    float3 a=2.0404*albedo-.3324,b=-4.7951*albedo+.6417,c=2.7552*albedo+.6903;
    result.diffuseVisibility=max(v,((v*a+b)*v+c)*v);
    float4 authored=ScreenSpaceAuthoredPbr.Load(int3(nativePixel,0));
    float4 material=NativeMaterial.Load(int3(nativePixel,0));
    PbrIndirectMaterial response=DecodePbrIndirectMaterial(material,authored,albedo,
        ScreenSpaceSurface.Load(int3(nativePixel,0)),
        ScreenSpaceParameters[8],ScreenSpaceParameters[9],ScreenSpaceParameters[10],ScreenSpaceParameters[11]);
    float3 viewDirection=SafeNormal(-float3(dot(Camera[20].xyz,receiver),dot(Camera[21].xyz,receiver),dot(Camera[22].xyz,receiver)));
    float nv=saturate(dot(normal,viewDirection));
    // Upstream ComposeSpecular: specular occlusion from the same AO, reduced
    // by the experimental specular trace's own visibility.
    float specularVisibility=saturate(pow(abs(nv+v),response.roughness*response.roughness)-1+v);
    if(enableGI!=0 && enableSpecular!=0) specularVisibility*=1-saturate(specular.w);
    result.specularVisibility=specularVisibility;
    if(enableGI!=0)
    {
        // The composite multiplies its accumulators by 3 * albedo, so a
        // bounce of irradiance * albedo enters them divided by 3.
        float3 bounce=max(0,YCoCgToRGB(float3(SHHallucinateZH3Irradiance(luma,normal),chroma.xy)));
        result.diffuse=bounce*response.diffuseShare/3;
        float lobe=dot(luma,SHFauxSpecularLobe(normal,viewDirection,response.roughness));
        float3 reflected=max(0,YCoCgToRGB(float3(lobe,chroma.xy/Pi)));
        if(enableSpecular!=0) reflected+=specular.rgb;
        result.specular=reflected*response.f0;
    }
    return result;
}
#endif
