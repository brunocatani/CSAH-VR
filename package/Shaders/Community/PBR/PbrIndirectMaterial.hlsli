#ifndef CSAH_PBR_INDIRECT_MATERIAL
#define CSAH_PBR_INDIRECT_MATERIAL
struct PbrIndirectMaterial { float3 f0; float roughness; float diffuseShare; };
PbrIndirectMaterial DecodePbrIndirectMaterial(float4 material, float4 authored, float3 retainedAlbedo,
    float surface, float4 feature0, float4 feature1, float4 params0, float4 params1)
{
    PbrIndirectMaterial result;
    result.f0=saturate(material.y*.32).xxx;
    result.roughness=saturate(1-material.x);
    result.diffuseShare=authored.x>=.5?1-saturate(authored.y):1;
    float authoredActive=step(.5,authored.x);
    float legacyMetal=PbrDecodeMetalness(material.w);
    float legacy=step(.5,material.w)*saturate(step(1.0/255,legacyMetal)+feature0.y);
    float code=surface*255;
    float supported=(abs(code)<.5 || abs(code-4)<.5 || (abs(code-1)<.5 && feature0.w>.5))?1:0;
    if(feature0.x>1.0/255 && feature1.x>.5 && supported>0 && authoredActive+legacy>0) {
        float metal=lerp(legacyMetal,saturate(authored.y),authoredActive);
        float3 base=lerp(retainedAlbedo/max(1-legacyMetal,1.0/255),retainedAlbedo,authoredActive);
        float dielectric=PbrDielectricF0(lerp(material.y,authored.w,authoredActive),params0.z,params0.w,params1.x);
        result.f0=lerp(dielectric.xxx,saturate(base*params1.y),metal);
        result.roughness=PbrMaterialRoughness(PbrRoughnessFromPhong(material.x)*7,material.x,material.y,params0.x,params0.y);
        if(authoredActive>.5) result.roughness=clamp((authored.x*2-1)*max(params0.x,0),PbrMinimumRoughness,1);
        // Legacy metal diffuse is already removed in retainedAlbedo.
        result.diffuseShare=lerp(1,1-metal,authoredActive);
    }
    return result;
}
#endif
