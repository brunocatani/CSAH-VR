Texture2D<float2> TerrainShadowHeight : register(t76);
Buffer<float4> TerrainShadowParameters : register(t77);
float TerrainVisibility(float3 world)
{
    uint rows;TerrainShadowParameters.GetDimensions(rows);
    if(rows<2) return 1;
    float4 map=TerrainShadowParameters[0],control=TerrainShadowParameters[1];
    if(control.w<.5) return 1;
    float2 pixel=(world.xy-map.xy)/map.z;
    if(any(pixel<0) || any(pixel>control.xy-1)) return 1;
    int2 lo=int2(floor(pixel)),hi=min(lo+1,int2(control.xy)-1);float2 f=frac(pixel);
    float2 h=lerp(lerp(TerrainShadowHeight.Load(int3(lo,0)),TerrainShadowHeight.Load(int3(hi.x,lo.y,0)),f.x),
        lerp(TerrainShadowHeight.Load(int3(lo.x,hi.y,0)),TerrainShadowHeight.Load(int3(hi,0)),f.x),f.y)-map.w;
    if(!all(isfinite(h))) return 1;
    float visibility=saturate((world.z-h.y)/max(h.x-h.y,1));
    return lerp(1,visibility,saturate(control.z));
}
