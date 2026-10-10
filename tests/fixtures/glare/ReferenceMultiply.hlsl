#include "BloomGlareCommon.hlsli"

Texture2D<float2> SceneFft : register(t0);
Texture2D<float2> PsfFft : register(t1);
RWTexture2D<float2> ResultFft : register(u0);

[numthreads(8, 8, 1)]
void CS_Multiply(uint2 tid : SV_DispatchThreadID)
{
    const uint resolution = FftResolution();
    if (tid.x >= resolution || tid.y >= resolution) {
        return;
    }
    const float2 scene = SceneFft[tid];
    const float2 psf = PsfFft[tid];
    const float normalization = max(PsfFft[uint2(0u, 0u)].x, 1.0e-6);
    ResultFft[tid] = float2(
        scene.x * psf.x - scene.y * psf.y,
        scene.x * psf.y + scene.y * psf.x) / normalization;
}
