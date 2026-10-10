#include "BloomGlareCommon.hlsli"

Texture2D<float2> FftInput : register(t0);
#ifdef MULTIPLY_PSF
Texture2D<float2> PsfFft : register(t1);
#endif
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

// Scatter straight into bit-reversed shared storage, then alternate buffers.
// Each stage reads only the completed preceding stage, so one barrier suffices.
[numthreads(512, 1, 1)]
void CS_Fft(uint3 groupId : SV_GroupID, uint threadIndex : SV_GroupThreadID)
{
    const uint count = FftResolution();
    const bool active = threadIndex < count;
    if (active) {
#ifdef ROW_PASS
        const uint2 coordinate = uint2(threadIndex, groupId.x);
#else
        const uint2 coordinate = uint2(groupId.x, threadIndex);
#endif
        float2 value = FftInput[coordinate];
#ifdef MULTIPLY_PSF
        // Same complex product and normalization as the former standalone pass.
        const float2 psf = PsfFft[coordinate];
        value = ComplexMultiply(value, psf) / max(PsfFft[uint2(0, 0)].x, 1.0e-6);
#endif
        const uint reversed = reversebits(threadIndex) >> (32u - firstbithigh(count));
        Shared0[reversed] = value;
    }
    GroupMemoryBarrierWithGroupSync();

    bool readZero = true;
    for (uint stage = 1u; stage < count; stage <<= 1u) {
        if (active) {
            const uint fullStage = stage << 1u;
            const uint butterflyGroup = threadIndex / fullStage;
            const uint butterflyIndex = threadIndex % fullStage;
            if (butterflyIndex < stage) {
                const uint topIndex = butterflyGroup * fullStage + butterflyIndex;
                const uint bottomIndex = topIndex + stage;
                float2 top;
                float2 bottomValue;
                if (readZero) {
                    top = Shared0[topIndex]; bottomValue = Shared0[bottomIndex];
                } else {
                    top = Shared1[topIndex]; bottomValue = Shared1[bottomIndex];
                }
                const float2 bottom = ComplexMultiply(Twiddle(butterflyIndex, fullStage), bottomValue);
                if (readZero) {
                    Shared1[topIndex] = top + bottom; Shared1[bottomIndex] = top - bottom;
                } else {
                    Shared0[topIndex] = top + bottom; Shared0[bottomIndex] = top - bottom;
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();
        readZero = !readZero;
    }
    if (active) {
        float2 result;
        if (readZero) result = Shared0[threadIndex]; else result = Shared1[threadIndex];
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
