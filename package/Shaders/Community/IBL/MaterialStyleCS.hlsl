// Authored cubemap style analysis adapted from DynamicCubemapF4 1.3.1 (GPL-3.0).
// Runs on the GPU once per array refresh, never once per shaded pixel.
TextureCubeArray<float4> Original : register(t0);
TextureCube<float4> ExteriorReference : register(t1);
RWTexture2D<float4> Styles : register(u0);
SamplerState PointSampler : register(s0);
cbuffer ReferenceEncoding : register(b0) { uint ReferenceConversion; uint3 Padding; };
float3 MatchReferenceEncoding(float3 color)
{
    if (ReferenceConversion == 1u)
        return float3(color.r <= 0.04045f ? color.r / 12.92f : pow(abs((color.r + 0.055f) / 1.055f), 2.4f),
            color.g <= 0.04045f ? color.g / 12.92f : pow(abs((color.g + 0.055f) / 1.055f), 2.4f),
            color.b <= 0.04045f ? color.b / 12.92f : pow(abs((color.b + 0.055f) / 1.055f), 2.4f));
    if (ReferenceConversion == 2u)
        return float3(color.r <= 0.0031308f ? color.r * 12.92f : 1.055f * pow(abs(color.r), 1.0f / 2.4f) - 0.055f,
            color.g <= 0.0031308f ? color.g * 12.92f : 1.055f * pow(abs(color.g), 1.0f / 2.4f) - 0.055f,
            color.b <= 0.0031308f ? color.b * 12.92f : 1.055f * pow(abs(color.b), 1.0f / 2.4f) - 0.055f);
    return color;
}

float3 Direction(uint face, float2 uv)
{
    if (face == 0u) return float3(1, -uv.y, -uv.x);
    if (face == 1u) return float3(-1, -uv.y, uv.x);
    if (face == 2u) return float3(uv.x, 1, uv.y);
    if (face == 3u) return float3(uv.x, -1, -uv.y);
    if (face == 4u) return float3(uv.x, -uv.y, 1);
    return float3(-uv.x, -uv.y, -1);
}
float Saturation(float3 c)
{
    const float hi = max(c.r, max(c.g, c.b));
    return hi > 1e-5f ? (hi - min(c.r, min(c.g, c.b))) / hi : 0;
}
float3 Chromaticity(float3 c)
{
    return c / max(c.r + c.g + c.b, 1e-5f);
}
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h, cubes, mips;
    Original.GetDimensions(0, w, h, cubes, mips);
    if (id.x >= cubes) return;
    uint rw, rh, rmips;
    ExteriorReference.GetDimensions(0, rw, rh, rmips);
    float3 faces[6];
    float3 average = 0;
    float saturation = 0;
    bool finite = true;
    bool exterior = rw > 0 && rh > 0 && rmips > 0;
    bool referenceNonzero = false;
    [unroll] for (uint face = 0; face < 6; ++face)
    {
        const float3 center = Direction(face, 0);
        float3 c = Original.SampleLevel(PointSampler, float4(center, id.x), mips - 1).rgb;
        const uint coarse = mips > 1 ? mips - 2 : 0;
        [unroll] for (uint y = 0; y < 2; ++y)
            [unroll] for (uint x = 0; x < 2; ++x)
                c += Original.SampleLevel(PointSampler,
                    float4(Direction(face, float2(x, y) - 0.5f), id.x), coarse).rgb;
        c *= 0.2f;
        finite = finite && all(isfinite(c));
        faces[face] = max(c, 0);
        average += faces[face] / 6.0f;
        saturation += Saturation(faces[face]) / 6.0f;
        // Compare actual sampled content, including direction; no identity hashes.
        [loop] for (uint sy = 0; sy < 8; ++sy)
            [loop] for (uint sx = 0; sx < 8; ++sx)
            {
                const float3 d = Direction(face, (float2(sx, sy) + 0.5f) / 4.0f - 1.0f);
                const float3 a = Original.SampleLevel(PointSampler, float4(d, id.x),
                    min((float)mips - 1, max(log2((float)w) - 3, 0))).rgb;
                const float3 b = MatchReferenceEncoding(ExteriorReference.SampleLevel(PointSampler, d,
                    min((float)rmips - 1, max(log2(max((float)rw, 1)) - 3, 0))).rgb);
                exterior = exterior && all(isfinite(a)) && all(isfinite(b)) &&
                    all(abs(a - b) <= 1e-5f + 1e-5f * max(abs(a), abs(b)));
                referenceNonzero = referenceNonzero || any(b > 1e-5f);
            }
    }
    float deviation = 0;
    [unroll] for (uint f = 0; f < 6; ++f)
        deviation += length(Chromaticity(faces[f]) - Chromaticity(average));
    const float consistency = saturate(1.0f - deviation / (6.0f * 0.816497f));
    const float luminance = dot(average, float3(0.2126f, 0.7152f, 0.0722f));
    const bool usable = finite && luminance > 1e-5f;
    const bool consistent = saturation < 0.12f || consistency >= 0.72f;
    // Without the named reference, neutral cubes cannot be distinguished from
    // the exterior fallback. Retain native data until reference loading succeeds.
    float4 tint = float4(1, 1, 1, -2);
    float intensity = 1;
    if (exterior && referenceNonzero) tint.w = -1;
    else if (usable && consistent && (referenceNonzero || saturation >= 0.12f))
    {
        tint = float4(average / luminance, saturate(saturation));
        intensity = clamp(sqrt(saturate(luminance)), 0.15f, 1.0f);
    }
    Styles[uint2(0, id.x)] = tint;
    Styles[uint2(1, id.x)] = float4(intensity, 0, 0, 0);
}
