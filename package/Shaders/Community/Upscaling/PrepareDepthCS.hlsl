#include "../Common/Fo4VrDepth.hlsli"

Texture2D<float> NativeDepth : register(t0);
RWTexture2D<float> WorldProjectionDepth : register(u0);

cbuffer DepthPreparationConstants : register(b0)
{
    uint2 OutputExtent;
    uint2 SourceOrigin;
    float4 LocalUvScaleBias;
    float4 FirstPersonToWorldZ;
    float4 FirstPersonToWorldW;
    float FarDepth;
    uint RawProbe;
    uint FirstPersonProjectionAvailable;
    uint Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutputExtent)) return;
    if (RawProbe != 0)
    {
        uint width, height;
        NativeDepth.GetDimensions(width, height);
        const uint eyeWidth = width / 2;
        const uint eye = id.x / 8;
        const uint local = id.x % 8;
        const uint x = min(eye * eyeWidth + ((2 * (local % 4) + 1) * eyeWidth) / 8, width - 1);
        const uint y = min(((2 * (local / 4) + 1) * height) / 4, height - 1);
        WorldProjectionDepth[id.xy] = NativeDepth.Load(int3(x, y, 0));
        return;
    }
    const float nativeDepth = NativeDepth.Load(int3(SourceOrigin + id.xy, 0));
    float result = FarDepth;
    if (isfinite(nativeDepth) && nativeDepth > 0.0f && nativeDepth <= 1.0f)
    {
        result = Fo4VrProjectionDepth(nativeDepth);
        if (Fo4VrFirstPersonDepth(nativeDepth))
        {
            result = FarDepth;
            if (FirstPersonProjectionAvailable != 0)
            {
                const float2 uv = (float2(id.xy) + 0.5f) * LocalUvScaleBias.xy + LocalUvScaleBias.zw;
                const float4 clip = float4(uv * float2(2, -2) + float2(-1, 1), Fo4VrProjectionDepth(nativeDepth), 1);
                const float denominator = dot(FirstPersonToWorldW, clip);
                result = abs(denominator) > 1e-8f ? dot(FirstPersonToWorldZ, clip) / denominator : FarDepth;
            }
        }
    }
    WorldProjectionDepth[id.xy] = isfinite(result) ? saturate(result) : FarDepth;
}
