#ifndef CSAH_LOD_BLENDING
#define CSAH_LOD_BLENDING
// Typed SRV avoids native b13 PerInstance data in the instanced prepass.
Buffer<float4> LodBlendingParameters : register(t55);
float4 AdjustLodAlbedo(float4 color, bool terrain)
{
    const float4 flags=LodBlendingParameters[1];
    if(flags.x<=0.5f || (!terrain && flags.y<=0.5f)) return color;
    const float4 parameters=LodBlendingParameters[0];
    const float2 response=terrain?parameters.xy:parameters.zw;
    color.rgb=pow(abs(color.rgb),response.y)*response.x;
    return color;
}
float4 AdjustMaterialLodAlbedo(float4 color)
{
#if LINEAR_LIGHTING_LANDSCAPE_LOD && !LINEAR_LIGHTING_BONE_TINTING
    return AdjustLodAlbedo(color,true);
#else
    return AdjustLodAlbedo(color,false);
#endif
}
#endif
