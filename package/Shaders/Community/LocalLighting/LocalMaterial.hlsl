#include "../PBR/PbrCommon.hlsli"
Buffer<float4> LocalLightingControl : register(t78);
Texture2D<float4> LocalAlbedo : register(t81);
Texture2D<float4> LocalMaterial : register(t82);
Texture2D<float4> AuthoredMaterial : register(t45);
Texture2D<float> SurfaceClass : register(t47);
cbuffer PbrSettings : register(b7) {float4 Feature0;float4 Feature1;float4 Material0;float4 Material1;};
cbuffer WetnessSettings : register(b9) {float4 Wetness;float4 WetMaterial;};
cbuffer HairSettings : register(b10) {float4 Hair;};
struct MaterialData {float4 f0Roughness:SV_Target0;float4 traits:SV_Target1;float4 response:SV_Target2;};
MaterialData InitMain(float4 position:SV_Position)
{
    MaterialData result;result.f0Roughness=float4(0,0,0,1);result.traits=0;result.response=float4(1,1,1,1);
    [branch] if(LocalLightingControl[0].w>.5) {
        int2 pixel=int2(position.xy);
        float code=SurfaceClass.Load(int3(pixel,0))*255;
        float ordinary=1-step(.5,abs(code)),grass=1-step(.5,abs(code-1)),terrain=1-step(.5,abs(code-4));
        float hair=1-step(.5,abs(code-2));
        float wet=saturate(Wetness.x*Wetness.y*saturate(ordinary+grass+terrain));
        result.response=float4(1-saturate(Wetness.z)*wet,lerp(1,max(Wetness.w,0),wet),lerp(1,max(Hair.y,0),saturate(Hair.x)*hair),max(Material1.z,0));
        [branch] if(Feature0.x>1.0/255 && Feature0.z>.5) {
            float4 material=LocalMaterial.Load(int3(pixel,0)),authored=AuthoredMaterial.Load(int3(pixel,0));
            float authoredActive=step(.5,authored.x),legacyMetal=PbrDecodeMetalness(material.w);
            float metal=lerp(legacyMetal,saturate(authored.y),authoredActive);
            float legacy=step(.5,material.w)*saturate(step(1.0/255,legacyMetal)+saturate(Feature0.y));
            float active=saturate(ordinary+terrain+grass*saturate(Feature0.w))*saturate(authoredActive+legacy);
            float3 retained=max(LocalAlbedo.Load(int3(pixel,0)).rgb,0);
            float3 base=lerp(retained/max(1-metal,1.0/255),retained,authoredActive);
            float dielectric=PbrDielectricF0(lerp(material.y,authored.w,authoredActive),Material0.z,Material0.w,Material1.x);
            float rough=PbrMaterialRoughness(PbrRoughnessFromPhong(material.x)*7,material.x,material.y,Material0.x,Material0.y);
            rough=lerp(rough,clamp(saturate(authored.x*2-1)*max(Material0.x,0),PbrMinimumRoughness,1),authoredActive);
            rough=clamp(rough*lerp(1,saturate(WetMaterial.x),wet),PbrMinimumRoughness,1);
            result.f0Roughness=float4(lerp(dielectric.xxx,saturate(base*Material1.y),metal),rough);
            result.traits=float4(active,lerp(1,1-metal,authoredActive),Feature1.y,Feature1.z);
        }
    }
    return result;
}
struct LightInput {
    float4 diffuse:TEXCOORD0;float4 specular:TEXCOORD1;
    float4 normal:TEXCOORD2;float4 view:TEXCOORD3;float4 light:TEXCOORD4;
    float4 colour:TEXCOORD5;float4 control:TEXCOORD6;
    float4 f0Roughness:TEXCOORD7;float4 traits:TEXCOORD8;float4 response:TEXCOORD9;
};
struct LightOutput {float4 diffuse:SV_Target0;float4 specular:SV_Target1;};
LightOutput LightMain(LightInput input)
{
    LightOutput result;result.diffuse=input.diffuse;result.specular=input.specular;
    [branch] if(LocalLightingControl[0].w>.5) {
        result.diffuse.rgb*=input.response.x;
        result.specular.rgb*=input.response.y*input.response.z;
        // Native bit 8 selects unlit/ambient local lighting; bit 16 enables specular.
        uint flags=asuint(input.control.x);
        [branch] if(input.traits.x>1.0/255 && (flags&8)==0) {
            float3 n=PbrSafeNormalize(input.normal.xyz,float3(0,0,1));
            float3 v=PbrSafeNormalize(input.view.xyz,n),l=PbrSafeNormalize(input.light.xyz,n);
            float3 h=PbrSafeNormalize(v+l,n);float nl=saturate(dot(n,l)),nv=max(saturate(abs(dot(n,v))),PbrEpsilon);
            float3 f=PbrFresnelSchlick(input.f0Roughness.rgb,saturate(dot(v,h)));
            result.diffuse.rgb*=lerp(1.0.xxx,input.traits.y.xxx*(input.traits.z>.5?saturate(1-f):1),input.traits.x);
            [branch] if((flags&16)!=0) {
                float3 spec=PbrDistributionGgx(input.f0Roughness.w,saturate(dot(n,h)))*PbrVisibilitySmithJointApprox(input.f0Roughness.w,nv,nl)*f*nl*max(input.colour.rgb,0)*input.response.w*input.response.y;
                if(input.traits.w>.5) {float2 brdf=PbrEnvironmentBrdf(input.f0Roughness.w,nv);spec*=1+input.f0Roughness.rgb*(1/max(brdf.x+brdf.y,PbrEpsilon)-1);}
                result.specular.rgb=lerp(result.specular.rgb,spec,input.traits.x);
            }
        }
    }
    return result;
}
