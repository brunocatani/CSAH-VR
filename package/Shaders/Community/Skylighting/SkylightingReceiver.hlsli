float4 NativeClip(float2 screenUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = screenUv.x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    return float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) *
            (Stereo[0].x + 1.0f),
        1.0f - screenUv.y * 2.0f,
        nativeDepth,
        1.0f);
}

float3 ReconstructRelativeWorldPosition(
    float2 screenUv,
    float depth,
    uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(screenUv, depth, eye);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    const float3 viewPosition = homogeneous.xyz / homogeneous.w;
    const float3 worldRelativeToEye = float3(dot(Camera[20].xyz, viewPosition),
        dot(Camera[21].xyz, viewPosition), dot(Camera[22].xyz, viewPosition));
    return Camera[59u + eye].xyz + worldRelativeToEye - ProbeWorldOrigin.xyz;
}

float3 DecodeNativeNormal(float2 encoded)
{
    const float2 expanded = encoded * 4.0f - 2.0f;
    const float lengthSquared = dot(expanded, expanded);
    const float3 viewNormal = float3(expanded * sqrt(max(1.0f - lengthSquared * 0.25f, 0.0f)),
        -(1.0f - lengthSquared * 0.5f));
    return normalize(float3(dot(Camera[20].xyz, viewNormal),
        dot(Camera[21].xyz, viewNormal), dot(Camera[22].xyz, viewNormal)));
}
