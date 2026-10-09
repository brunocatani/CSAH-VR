// Triangular stochastic sampling adapted from community-shaders-shared
// features/Terrain Variation. Distributed under this repository's GPL-3.0.
// Original algorithm: Thomas Deliot and Eric Heitz, tiling and blending.
#ifndef CSAH_TERRAIN_VARIATION
#define CSAH_TERRAIN_VARIATION
Buffer<float4> TerrainVariationParameters : register(t56);
static const float HEIGHT_INFLUENCE=0.3;
static const float2x2 SKEW_MATRIX=float2x2(1,0,-0.57735027,1.15470054);
static const float WORLD_SCALE=332.54;
static const float3 LUMINANCE_WEIGHTS=float3(0.2126,0.7152,0.0722);
static const float2 HASH_MULTIPLIER=float2(1271.5151,3337.8237);
static const float TAP_SWAP_FADE_WIDTH=0.1;
static const float DOMINANCE_GUARD_WIDTH=0.02;
struct StochasticOffsets {float2 offset1;float2 offset2;float tap1Weight;};
struct StochasticCorner {float2 cell;float w;};
inline float2 hash2D2D(float2 s)
{
	s = frac(s * HASH_MULTIPLIER);
	s += dot(s, s.yx + 19.19);
	return frac((s.xx + s.yy) * s.yx);
}

inline float2 hashLOD(float2 p)
{
	p = frac(p * 0.318);
	return frac(float2(dot(p, float2(1.0, 17.0)), dot(p, float2(1.0, 23.0))));
}

// Two highest barycentric corners of the triangular grid.
// Near c1/c2 ties, fade w2 so the discarded corner cannot pop;
// near c0/c1 ties, disable that fade so the primary swap stays symmetric.
// Contrast exponent is 2, so weights are squared rather than pow()'d.
inline StochasticOffsets ComputeStochasticOffsetsScaled(float2 uv, float scale)
{
	float2 skewUV = mul(SKEW_MATRIX, uv * scale);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0 - f.x - f.y;

	StochasticCorner c0, c1, c2;
	if (bz > 0) {
		c0.cell = vxID;
		c0.w = bz;
		c1.cell = vxID + float2(0, 1);
		c1.w = f.y;
		c2.cell = vxID + float2(1, 0);
		c2.w = f.x;
	} else {
		c0.cell = vxID + 1.0;
		c0.w = -bz;
		c1.cell = vxID + float2(1, 0);
		c1.w = 1.0 - f.y;
		c2.cell = vxID + float2(0, 1);
		c2.w = 1.0 - f.x;
	}

	if (c1.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c1;
		c1 = t;
	}
	if (c2.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c2;
		c2 = t;
	}
	if (c2.w > c1.w) {
		StochasticCorner t = c1;
		c1 = c2;
		c2 = t;
	}

	float w1 = saturate(c0.w);
	w1 *= w1;
	float w2 = saturate(c1.w);
	w2 *= w2;
	float rankFade = smoothstep(0.0, TAP_SWAP_FADE_WIDTH, c1.w - c2.w);
	float dominanceGuard = smoothstep(0.0, DOMINANCE_GUARD_WIDTH, c0.w - c1.w);
	w2 *= lerp(1.0, rankFade, dominanceGuard);

	StochasticOffsets o;
	o.offset1 = hash2D2D(c0.cell);
	o.offset2 = hash2D2D(c1.cell);
	o.tap1Weight = w1 * rcp(max(w1 + w2, 1e-8));
	return o;
}

inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	return ComputeStochasticOffsetsScaled(landscapeUV, WORLD_SCALE);
}

inline float4 StochasticBlendTwoSamples(float4 s1, float4 s2, float tap1Weight, float blendFactor1, float blendFactor2)
{
	float w1 = tap1Weight * (1.0 + HEIGHT_INFLUENCE * blendFactor1);
	float w2 = (1.0 - tap1Weight) * (1.0 + HEIGHT_INFLUENCE * blendFactor2);
	return lerp(s2, s1, w1 * rcp(max(w1 + w2, 1e-8)));
}


float4 TerrainBlend(float4 a,float4 b,StochasticOffsets offsets,bool heightOnly)
{
    float h1=heightOnly?a.a:lerp(dot(a.rgb,LUMINANCE_WEIGHTS),a.a,step(0.001,a.a));
    float h2=heightOnly?b.a:lerp(dot(b.rgb,LUMINANCE_WEIGHTS),b.a,step(0.001,b.a));
    return StochasticBlendTwoSamples(a,b,offsets.tap1Weight,h1,h2);
}
float4 SampleVariedTerrain(Texture2D<float4> tex,SamplerState samp,float2 uv,
    float2 dx,float2 dy,StochasticOffsets offsets,bool explicitGradients,bool heightOnly)
{
    float4 result=0;
    [branch] if(TerrainVariationParameters[0].x>0.5) {
        result=TerrainBlend(tex.SampleGrad(samp,uv+offsets.offset1,dx,dy),
            tex.SampleGrad(samp,uv+offsets.offset2,dx,dy),offsets,heightOnly);
    } else {
        if(explicitGradients) result=tex.SampleGrad(samp,uv,dx,dy);
        else result=tex.Sample(samp,uv);
    }
    return result;
}
float4 SampleVariedTerrain(Texture2DArray<float4> tex,SamplerState samp,float3 uv,
    float2 dx,float2 dy,StochasticOffsets offsets,bool explicitGradients,bool heightOnly)
{
    float4 result=0;
    [branch] if(TerrainVariationParameters[0].x>0.5) {
        result=TerrainBlend(tex.SampleGrad(samp,float3(uv.xy+offsets.offset1,uv.z),dx,dy),
            tex.SampleGrad(samp,float3(uv.xy+offsets.offset2,uv.z),dx,dy),offsets,heightOnly);
    } else {
        if(explicitGradients) result=tex.SampleGrad(samp,uv,dx,dy);
        else result=tex.Sample(samp,uv);
    }
    return result;
}
// A world-UV-derived jitter is identical for both eyes and stable under head
// movement. Explicit original derivatives prevent random offsets from
// selecting a blurrier mip or changing the samp's anisotropy.
float4 SampleVariedLod(Texture2D<float4> tex,SamplerState samp,float2 uv)
{
    float4 result=0;
    [branch] if(TerrainVariationParameters[0].x>0.5 && TerrainVariationParameters[0].y>0.5) {
    float2 cell=floor(uv*255437.0);
    float2 jitter=hashLOD(cell+31.0)-0.5;
    float2 a=(hashLOD(cell)*0.08+jitter)*0.01;
    float2 b=(hashLOD(cell+127.0)*0.08+float2(jitter.y,-jitter.x))*0.01;
    result=lerp(tex.SampleGrad(samp,uv+b,ddx(uv),ddy(uv)),tex.SampleGrad(samp,uv+a,ddx(uv),ddy(uv)),0.65);
    result.a=tex.Sample(samp,uv).a;
    } else {result=tex.Sample(samp,uv);}
    return result;
}
float4 SampleVariedLod(Texture2DArray<float4> tex,SamplerState samp,float3 uv)
{
    float4 result=0;
    [branch] if(TerrainVariationParameters[0].x>0.5 && TerrainVariationParameters[0].y>0.5) {
    float2 cell=floor(uv.xy*255437.0);
    float2 jitter=hashLOD(cell+31.0)-0.5;
    float2 a=(hashLOD(cell)*0.08+jitter)*0.01;
    float2 b=(hashLOD(cell+127.0)*0.08+float2(jitter.y,-jitter.x))*0.01;
    result=lerp(tex.SampleGrad(samp,float3(uv.xy+b,uv.z),ddx(uv.xy),ddy(uv.xy)),
        tex.SampleGrad(samp,float3(uv.xy+a,uv.z),ddx(uv.xy),ddy(uv.xy)),0.65);
    result.a=tex.Sample(samp,uv).a;
    } else {result=tex.Sample(samp,uv);}
    return result;
}
float4 SampleMaterialTerrainLod(Texture2D<float4> tex,SamplerState samp,float2 uv)
{
#if LINEAR_LIGHTING_LANDSCAPE_LOD && !LINEAR_LIGHTING_BONE_TINTING
    return SampleVariedLod(tex,samp,uv);
#else
    return tex.Sample(samp,uv);
#endif
}
float4 SampleMaterialTerrainLod(Texture2DArray<float4> tex,SamplerState samp,float3 uv)
{
#if LINEAR_LIGHTING_LANDSCAPE_LOD && !LINEAR_LIGHTING_BONE_TINTING
    return SampleVariedLod(tex,samp,uv);
#else
    return tex.Sample(samp,uv);
#endif
}
#endif
