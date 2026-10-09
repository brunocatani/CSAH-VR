/*
MIT License

Copyright (c) 2022 Ilya Perapechka

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
Texture2D<float> TexHeight : register(t0);
RWTexture2D<float2> RWTexShadowHeights : register(u0);

// DDA scan adapted from the approved community-shaders-shared reference.
// Native directional b2 and camera b12 are copied from this same draw.
cbuffer TerrainUpdate : register(b0) {uint Slice;float Spacing;float2 Padding;};
cbuffer NativeSun : register(b2) {float4 Sun[2];};
cbuffer NativeCamera : register(b12) {float4 Camera[23];};
static float2 LightPxDir;
static float2 LightDeltaZ;
static uint StartPxCoord;
float2 GetInterpolatedHeightRW(float2 pxCoord, bool isVertical)
{
	uint2 dims;
	RWTexShadowHeights.GetDimensions(dims.x, dims.y);

	int2 lerpPxCoordA = int2(floor(pxCoord - .5 * float2(isVertical, !isVertical)));
	int2 lerpPxCoordB = lerpPxCoordA + int2(isVertical, !isVertical);
	lerpPxCoordA = clamp(lerpPxCoordA, int2(0, 0), int2(dims) - 1);
	lerpPxCoordB = clamp(lerpPxCoordB, int2(0, 0), int2(dims) - 1);
	float2 heightA = RWTexShadowHeights[lerpPxCoordA];
	float2 heightB = RWTexShadowHeights[lerpPxCoordB];

	return lerp(heightA, heightB, frac((isVertical ? pxCoord.x : pxCoord.y) - .5));
}

#define NTHREADS 128
groupshared float2 g_shadowHeight[NTHREADS];

// Offsets can span more than one dimension on small heightmaps, so wrap by modulo rather than a single step.
uint GetWrappedCoord(int coord, uint dimension)
{
	uint magnitude = uint(abs(coord)) % dimension;
	return coord < 0 ? (dimension - magnitude) % dimension : magnitude;
}

[numthreads(NTHREADS, 1, 1)] void main(const uint gtid : SV_GroupThreadID, const uint gid : SV_GroupID) {
	uint2 dims;
	TexHeight.GetDimensions(dims.x, dims.y);

    float3 light=float3(dot(Camera[20].xyz,Sun[1].xyz),dot(Camera[21].xyz,Sun[1].xyz),dot(Camera[22].xyz,Sun[1].xyz));
    if(light.z>0) light=-light;
    float horizontal=length(light.xy);
    float major=max(abs(light.x),abs(light.y));
    LightPxDir=major>1e-6 ? light.xy/major : float2(1,0);
    float angle=atan2(-light.z,max(horizontal,1e-6));
    float2 angles=clamp(angle+float2(-0.017453293,0.017453293),0,1.560796327);
    LightDeltaZ=-length(LightPxDir)*Spacing*tan(angles);
    if(!all(isfinite(light)) || horizontal<1e-6) LightDeltaZ=-1e20;
	bool isVertical = abs(LightPxDir.y) > abs(LightPxDir.x);
	int majorStep = (isVertical ? LightPxDir.y : LightPxDir.x) > 0.0 ? 1 : -1;
	uint size=isVertical?dims.y:dims.x;
    StartPxCoord=majorStep>0?Slice*128:size-1-Slice*128;
    int majorPxCoord = int(StartPxCoord) + int(gtid) * majorStep;
	uint majorDimension = isVertical ? dims.y : dims.x;
	uint minorDimension = isVertical ? dims.x : dims.y;
	bool isValid = gid<minorDimension && majorPxCoord >= 0 && majorPxCoord < int(majorDimension);
	float minorDirection = isVertical ? LightPxDir.x : LightPxDir.y;
	float minorOffset = 0.5 + gtid * minorDirection;
	int rawMinorPxCoord = int(gid) + int(floor(minorOffset));
	uint minorPxCoord = GetWrappedCoord(rawMinorPxCoord, minorDimension);
	uint2 outputPxCoord = isVertical ? uint2(minorPxCoord, majorPxCoord) : uint2(majorPxCoord, minorPxCoord);
	float rayWrap = floor(float(rawMinorPxCoord) / float(minorDimension));

	if (isValid) {


		float terrainHeight = max(-1e20,TexHeight[outputPxCoord]);
		float2 heights = terrainHeight.xx;

		// fetch last dispatch
		int previousMajorCoord = majorPxCoord - majorStep;
		float previousMinorCoord = gid + 0.5 - minorDirection;
		if (gtid == 0 && previousMajorCoord >= 0 && previousMajorCoord < int(majorDimension) && previousMinorCoord >= 0.0 && previousMinorCoord < float(minorDimension)) {
			float2 previousPxCoord = isVertical ? float2(previousMinorCoord, previousMajorCoord + 0.5) : float2(previousMajorCoord + 0.5, previousMinorCoord);
			float2 sampleHeights = GetInterpolatedHeightRW(previousPxCoord, isVertical) + LightDeltaZ;
			heights = max(heights, sampleHeights);
		}

		g_shadowHeight[gtid] = heights;
	}

	GroupMemoryBarrierWithGroupSync();

	// simple parallel scan
	[unroll] for (uint offset = 1; offset < NTHREADS; offset <<= 1)
	{
		bool combineHeights = false;
		float2 currentHeights = 0.0;
		float2 sampleHeights = 0.0;
		if (isValid && gtid >= offset) {
			int previousMinorPxCoord = int(gid) + int(floor(0.5 + (gtid - offset) * minorDirection));
			if (floor(float(previousMinorPxCoord) / float(minorDimension)) == rayWrap) {
				combineHeights = true;
				currentHeights = g_shadowHeight[gtid];
				sampleHeights = g_shadowHeight[gtid - offset] + LightDeltaZ * offset;
			}
		}
		GroupMemoryBarrierWithGroupSync();
		if (combineHeights) {
			g_shadowHeight[gtid] = max(currentHeights, sampleHeights);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// save
	if (isValid) {
		RWTexShadowHeights[outputPxCoord] = g_shadowHeight[gtid];
	}
}
