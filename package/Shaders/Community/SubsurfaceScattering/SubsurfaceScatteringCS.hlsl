Texture2D<float4> SourceLighting : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float> SurfaceClass : register(t2);
Texture2D<float4> GBufferMaterial : register(t3);
RWTexture2D<float4> OutputLighting : register(u0);

cbuffer SubsurfaceScatteringConstants : register(b0)
{
    // xy=stereo target dimensions, zw=unit blur direction.
    float4 TargetAndDirection;
    // x=radius in pixels, y=strength, z=depth rejection, w=skin class code.
    float4 ScatteringParameters;
    // x=class-code tolerance. Remaining values are reserved.
    float4 ClassificationParameters;
    float4 RenderViewport;
};

bool IsSkin(float surfaceClass)
{
    return abs(surfaceClass - ScatteringParameters.w) <=
        ClassificationParameters.x;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    const uint2 dimensions = uint2(TargetAndDirection.xy);
    if (any(dispatchThread.xy >= dimensions))
    {
        return;
    }

    const int2 pixel = int2(dispatchThread.xy);
    const float4 centre = SourceLighting.Load(int3(pixel, 0));
    const float centreClass = SurfaceClass.Load(int3(pixel, 0));
    if (!IsSkin(centreClass) || any(pixel < int2(RenderViewport.xy)) ||
        any(pixel >= int2(RenderViewport.xy + RenderViewport.zw)))
    {
        OutputLighting[pixel] = centre;
        return;
    }

    // FO4VR renders both eyes side by side. Horizontal samples may never
    // cross the eye seam; vertical samples share the same clamp harmlessly.
    const int eyeWidth = int(uint(RenderViewport.z) >> 1u);
    const int eyeIndex = (pixel.x - int(RenderViewport.x)) >= eyeWidth ? 1 : 0;
    const int eyeMinimum = int(RenderViewport.x) + eyeIndex * eyeWidth;
    const int eyeMaximum = eyeMinimum + eyeWidth - 1;
    const float centreDepth = SceneDepth.Load(int3(pixel, 0));
    const float depthLimit = max(
        ScatteringParameters.z,
        abs(centreDepth) * ScatteringParameters.z * 2.0);
    // Settings are authored in reference-eye pixels. Scale them with the
    // actual per-eye width so the physical appearance does not collapse at
    // FO4VR's high headset render resolutions.
    const float radius = max(
        ScatteringParameters.x * (float(eyeWidth) / 720.0f), 0.5f);

    const float3 centreAlbedo = max(
        saturate(GBufferMaterial.Load(int3(pixel, 0)).rgb), 0.04f);
    const float3 centreIrradiance = centre.xyz / centreAlbedo;

    // A compact skin diffusion profile: red travels furthest, green less,
    // and blue least. Blur irradiance rather than albedo-bearing lighting so
    // freckles, complexion, and authored facial texture detail stay intact.
    const float3 profileSigma = float3(0.72f, 0.46f, 0.28f);
    float3 accumulated = centreIrradiance;
    float3 accumulatedWeight = 1.0f;
    [unroll]
    for (int offset = -5; offset <= 5; ++offset)
    {
        if (offset == 0)
        {
            continue;
        }
        const float normalizedOffset = float(offset) / 5.0f;
        const int sampleOffset = int(round(normalizedOffset * radius));
        int2 samplePixel = pixel +
            int2(TargetAndDirection.zw * sampleOffset);
        samplePixel.x = clamp(samplePixel.x, eyeMinimum, eyeMaximum);
        samplePixel.y = clamp(samplePixel.y, int(RenderViewport.y), int(RenderViewport.y + RenderViewport.w) - 1);
        const float sampleClass = SurfaceClass.Load(int3(samplePixel, 0));
        const float sampleDepth = SceneDepth.Load(int3(samplePixel, 0));
        if (!IsSkin(sampleClass) ||
            abs(sampleDepth - centreDepth) > depthLimit)
        {
            continue;
        }
        const float3 profilePosition = normalizedOffset / profileSigma;
        const float3 weight = exp2(
            -0.72134752f * profilePosition * profilePosition);
        const float3 sampleAlbedo = max(
            saturate(GBufferMaterial.Load(int3(samplePixel, 0)).rgb),
            0.04f);
        const float3 sampleIrradiance =
            SourceLighting.Load(int3(samplePixel, 0)).xyz / sampleAlbedo;
        accumulated += sampleIrradiance * weight;
        accumulatedWeight += weight;
    }

    const float3 scatteredIrradiance =
        accumulated / max(accumulatedWeight, 1.0e-4f);
    const float3 scattered = scatteredIrradiance * centreAlbedo;
    OutputLighting[pixel] = float4(
        lerp(centre.xyz, scattered, saturate(ScatteringParameters.y)),
        centre.w);
}
