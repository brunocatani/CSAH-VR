cbuffer MotionRepairConstants : register(b0)
{
    uint2 RenderDimensions;
    float NearPlane;
    float FarPlane;
    uint ResetHistory;
    float HistoryWeight;
    float FarDepthStart;
    uint DepthInverted;
};

Texture2D<float2> MotionInput : register(t0);
Texture2D<float> DepthInput : register(t1);
Texture2D<float2> MotionHistory : register(t2);
RWTexture2D<float2> MotionOutput : register(u0);

float LinearDepth(float depth)
{
    if (DepthInverted != 0) depth = 1.0f - depth;
    const float denominator = max(
        FarPlane - saturate(depth) * (FarPlane - NearPlane),
        1.0e-4);
    return NearPlane * FarPlane / denominator;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= RenderDimensions))
        return;

    const int2 pixel = int2(dispatchThreadId.xy);
    const float centerDepth = DepthInput.Load(int3(pixel, 0));
    const float centerLinearDepth = LinearDepth(centerDepth);
    float2 motion = MotionInput.Load(int3(pixel, 0));

    // FO4/FO4VR skies and very distant surfaces can carry weak or empty
    // vectors. Reconstruct only those pixels from nearer 5x5 neighbors; all
    // ordinary geometry passes through bit-for-bit from the qualified input.
    if (centerLinearDepth > FarDepthStart)
    {
        float2 weightedMotion = 0.0;
        float weightSum = 0.0;

        [unroll]
        for (int y = -2; y <= 2; ++y)
        {
            [unroll]
            for (int x = -2; x <= 2; ++x)
            {
                const int2 neighbor = pixel + int2(x, y);
                if (any(neighbor < 0) || any(neighbor >= int2(RenderDimensions)))
                    continue;

                const float neighborDepth = DepthInput.Load(int3(neighbor, 0));
                const float neighborLinearDepth = LinearDepth(neighborDepth);
                if (neighborLinearDepth >= centerLinearDepth)
                    continue;

                const float depthDifference = abs(
                    centerLinearDepth - neighborLinearDepth);
                const float weight = rcp(1.0 + depthDifference);
                weightedMotion += MotionInput.Load(int3(neighbor, 0)) * weight;
                weightSum += weight;
            }
        }

        if (weightSum > 0.0)
            motion = weightedMotion / weightSum;

        if (ResetHistory == 0)
        {
            motion = lerp(
                motion,
                MotionHistory.Load(int3(pixel, 0)),
                saturate(HistoryWeight));
        }
    }

    MotionOutput[dispatchThreadId.xy] = motion;
}
