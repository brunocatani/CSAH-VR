#include "BloomGlareCommon.hlsli"

Texture2D<float2> FftInput : register(t0);
RWTexture2D<float2> FftOutput : register(u0);

#define MAX_FFT_SIZE 512
groupshared float2 Shared0[MAX_FFT_SIZE];
groupshared float2 Shared1[MAX_FFT_SIZE];

float2 ComplexMultiply(float2 left, float2 right)
{
    return float2(
        left.x * right.x - left.y * right.y,
        left.x * right.y + left.y * right.x);
}

float2 Twiddle(uint index, uint count)
{
#ifdef INVERSE
    const float angle = 2.0 * Pi * index / count;
#else
    const float angle = -2.0 * Pi * index / count;
#endif
    float sine;
    float cosine;
    sincos(angle, sine, cosine);
    return float2(cosine, sine);
}

[numthreads(512, 1, 1)]
void CS_Fft(uint3 groupId : SV_GroupID, uint threadIndex : SV_GroupThreadID)
{
    const uint count = FftResolution();
    const bool active = threadIndex < count;
    if (active) {
#ifdef ROW_PASS
        Shared0[threadIndex] = FftInput[uint2(threadIndex, groupId.x)];
#else
        Shared0[threadIndex] = FftInput[uint2(groupId.x, threadIndex)];
#endif
    }
    GroupMemoryBarrierWithGroupSync();

    if (active) {
        const uint bits = firstbithigh(count);
        uint reversed = 0u;
        uint source = threadIndex;
        for (uint bit = 0u; bit < bits; ++bit) {
            reversed = (reversed << 1u) | (source & 1u);
            source >>= 1u;
        }
        Shared1[reversed] = Shared0[threadIndex];
    }
    GroupMemoryBarrierWithGroupSync();

    if (active) {
        Shared0[threadIndex] = Shared1[threadIndex];
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stage = 1u; stage < count; stage <<= 1u) {
        if (active) {
            const uint fullStage = stage << 1u;
            const uint butterflyGroup = threadIndex / fullStage;
            const uint butterflyIndex = threadIndex % fullStage;
            if (butterflyIndex < stage) {
                const uint topIndex =
                    butterflyGroup * fullStage + butterflyIndex;
                const uint bottomIndex = topIndex + stage;
                const float2 top = Shared0[topIndex];
                const float2 bottom = ComplexMultiply(
                    Twiddle(butterflyIndex, fullStage),
                    Shared0[bottomIndex]);
                Shared1[topIndex] = top + bottom;
                Shared1[bottomIndex] = top - bottom;
            }
        }
        GroupMemoryBarrierWithGroupSync();
        if (active) {
            Shared0[threadIndex] = Shared1[threadIndex];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (active) {
        float2 result = Shared0[threadIndex];
#ifdef INVERSE
        result /= count;
#endif
#ifdef ROW_PASS
        FftOutput[uint2(threadIndex, groupId.x)] = result;
#else
        FftOutput[uint2(groupId.x, threadIndex)] = result;
#endif
    }
}
