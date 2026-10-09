Texture2D<float> SceneDepth : register(t0);
Texture2D<float2> RawContactShadow : register(t1);
TextureCube<float> CloudOcclusion : register(t2);
Texture2D<float4> SceneNormal : register(t3);
RWTexture2D<unorm float2> ResolvedShadowMask : register(u0);
SamplerState CloudSampler : register(s0);

cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer NativeStereo : register(b8)
{
    float4 Stereo[1];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

cbuffer ContactShadowSettings : register(b13)
{
    float4 ContactParams0;
    float4 ContactParams1;
    float4 ContactParams2;
    float4 CloudParams;
};

float4 NativeClip(float2 packedUv, float depth, uint eye)
{
    const bool compressedNearDepth = depth <= 0.01f;
    const float nativeDepth = compressedNearDepth ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float baseClipX = packedUv.x / DFLight[45].x * 2.0f - 1.0f;
    const float eyeClipOffset = eye == 0u ? 0.5f : -0.5f;
    return float4(
        (baseClipX + eyeClipOffset * Stereo[0].x) *
            (Stereo[0].x + 1.0f),
        1.0f - packedUv.y / DFLight[45].y * 2.0f,
        nativeDepth,
        1.0f);
}

float3 ReconstructViewPosition(float2 packedUv, float depth, uint eye)
{
    const uint row = eye * 4u + (depth <= 0.01f ? 40u : 32u);
    const float4 clip = NativeClip(packedUv, depth, eye);
    const float4 homogeneous = float4(
        dot(Camera[row + 0u], clip),
        dot(Camera[row + 1u], clip),
        dot(Camera[row + 2u], clip),
        dot(Camera[row + 3u], clip));
    return homogeneous.xyz / max(abs(homogeneous.w), 1.0e-7f);
}

float CloudVisibility(float3 relativeWorldPosition, float3 towardLight)
{
    if (CloudParams.x <= 0.5f) {
        return 1.0f;
    }
    const float cloudHeight = max(CloudParams.z, 1.0f);
    const float planetRadius = max(CloudParams.w, cloudHeight);
    const float shellRadius = planetRadius + cloudHeight;
    const float3 p =
        (relativeWorldPosition + float3(0.0f, 0.0f, planetRadius)) /
        shellRadius;
    const float projected = dot(p, towardLight);
    const float discriminant = max(
        projected * projected - dot(p, p) + 1.0f,
        0.0f);
    const float travel = -projected + sqrt(discriminant);
    const float3 sampleDirection =
        (p + towardLight * travel) * shellRadius -
        float3(0.0f, 0.0f, planetRadius);
    const float cloud = CloudOcclusion.SampleLevel(
        CloudSampler, sampleDirection, 0.0f);
    return saturate(1.0f - cloud * saturate(CloudParams.y));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    uint width;
    uint height;
    SceneDepth.GetDimensions(width, height);
    const uint2 pixel = dispatchThread.xy;
    if (pixel.x >= width || pixel.y >= height || width < 2u) {
        return;
    }

    const float centerDepth = SceneDepth.Load(int3(pixel, 0));
    if (centerDepth <= 1.0e-6f) {
        ResolvedShadowMask[pixel] = 1.0f;
        return;
    }

    const uint eye = min(pixel.x / max(width / 2u, 1u), 1u);
    const float2 dimensions = float2(width, height);
    const float2 packedUv = (float2(pixel) + 0.5f) / dimensions;
    const float3 surface = ReconstructViewPosition(
        packedUv,
        centerDepth,
        eye);
    const float3 towardLight = normalize(DFLight[eye + 1u].xyz);
    const float cloudVisibility = CloudVisibility(surface, towardLight);

    float2 contactVisibility = 1.0f.xx;
    const bool contactEnabled =
        ContactParams2.z > 0.5f && ContactParams0.x > 0.0f;
    if (contactEnabled) {
        const float2 rawVisibility = saturate(
            RawContactShadow.Load(int3(pixel, 0)));
        const float viewDepth = abs(surface.z);
        const float fadeDistance = max(ContactParams2.x, 1.0f);
        const float distanceScale =
            1.0f - smoothstep(0.0f, fadeDistance, viewDepth);
        contactVisibility = lerp(
            1.0f,
            rawVisibility,
            saturate(ContactParams0.x) * distanceScale);
    }

    const float2 encoded = SceneNormal.Load(int3(pixel, 0)).xy * 4.0f - 2.0f;
    const float lengthSquared = dot(encoded, encoded);
    const float3 normal = float3(encoded * sqrt(max(1.0f - lengthSquared * 0.25f, 0.0f)),
        -(1.0f - lengthSquared * 0.5f));
    const bool facing = dot(normal, towardLight) >= 0.0f;
    ResolvedShadowMask[pixel] = cloudVisibility * float2(
        facing ? contactVisibility.x : 1.0f,
        facing ? contactVisibility.x : contactVisibility.y);
}
