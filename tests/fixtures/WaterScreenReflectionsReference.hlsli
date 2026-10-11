// Stereo water tracing adapted from the local effects_10.5 implementation.
// The shared midpoint ray queries both eye depth buffers; colour comes from
// the receiving eye, with bounded tracing and confidence fades.
Texture2D<float4> WaterSceneColor : register(t79);
Texture2D<float4> WaterSceneDepth : register(t80);
struct WaterRaytraceInput
{
    float3 waterNormal : TEXCOORD0;
    float2 packedUv : TEXCOORD1;
    nointerpolation uint eyeIndex : EYEINDEX;
    float3 eyeRelativeReceiver : TEXCOORD3;
};

float4 multiplyRows(uint firstRow, float4 value)
{
    return float4(
        dot(Camera[firstRow + 0u], value),
        dot(Camera[firstRow + 1u], value),
        dot(Camera[firstRow + 2u], value),
        dot(Camera[firstRow + 3u], value));
}

float3 transformRows(uint firstRow, float3 value)
{
    return float3(
        dot(Camera[firstRow + 0u].xyz, value),
        dot(Camera[firstRow + 1u].xyz, value),
        dot(Camera[firstRow + 2u].xyz, value));
}

float2 eyeLocalUv(float2 packed, bool rightEye)
{
    return float2(
        (packed.x - (rightEye ? 0.5f : 0.0f)) * 2.0f,
        packed.y);
}

float3 incidentViewFromScreen(float2 packed, bool rightEye)
{
    // Direction and receiver are separate contracts. A fixed clip plane
    // supplies only the angular camera ray, so water depth and eye-relative
    // vertex translation cannot rotate the reflection as the HMD moves.
    const float2 local = eyeLocalUv(packed, rightEye);
    const float4 clip = float4(
        local.x * 2.0f - 1.0f,
        (1.0f - local.y) * 2.0f - 1.0f,
        0.5f,
        1.0f);
    const uint eyeOffset = rightEye ? 4u : 0u;
    const float4 homogeneous = multiplyRows(eyeOffset + 32u, clip);
    return normalize(homogeneous.xyz / homogeneous.w);
}

float3 receiverViewFromScreen(
    WaterRaytraceInput input,
    bool rightEye)
{
    // The stock vertex vector points from the surface toward the eye. The
    // inverse-projected fixed-clip ray already points from the eye toward the
    // surface, which is the receiver direction required by the marcher.
    return incidentViewFromScreen(input.packedUv, rightEye) *
        length(input.eyeRelativeReceiver);
}

float3 stableWaterNormalView(
    WaterRaytraceInput input,
    bool rightEye)
{
    const float3 receiver = receiverViewFromScreen(input, rightEye);
    float3 geometricNormal = cross(ddx(receiver), ddy(receiver));
    const float geometricLengthSquared = dot(
        geometricNormal,
        geometricNormal);
    const float3 currentNormal = normalize(transformRows(
        0u,
        input.waterNormal));
    if (!all(isfinite(geometricNormal)) ||
        !isfinite(geometricLengthSquared) ||
        geometricLengthSquared <= 1.0e-8f) {
        return currentNormal;
    }
    geometricNormal *= rsqrt(geometricLengthSquared);
    if (dot(geometricNormal, currentNormal) < 0.0f) {
        geometricNormal = -geometricNormal;
    }

    const float3 worldXView = normalize(transformRows(
        0u,
        float3(1.0f, 0.0f, 0.0f)));
    const float3 worldYView = normalize(transformRows(
        0u,
        float3(0.0f, 1.0f, 0.0f)));
    const float3 tangentReference =
        abs(dot(worldXView, geometricNormal)) < 0.9f ?
            worldXView : worldYView;
    const float3 tangent = normalize(
        tangentReference - geometricNormal *
            dot(tangentReference, geometricNormal));
    float3 bitangent = normalize(cross(geometricNormal, tangent));
    if (dot(bitangent, worldYView) < 0.0f) {
        bitangent = -bitangent;
    }

    // The stock animated normal is expressed around the water plane's world
    // X/Y/up basis. Preserve its ripple components while replacing only the
    // unstable base orientation with the geometric surface basis.
    const float3 detail = normalize(input.waterNormal);
    return normalize(
        tangent * detail.x +
        bitangent * detail.y +
        geometricNormal * detail.z);
}

float3 reflectedWaterView(
    WaterRaytraceInput input,
    bool rightEye)
{
    const float3 animatedNormal = stableWaterNormalView(input, rightEye);
    const float3 flatNormal = normalize(transformRows(
        0u,
        float3(0.0f, 0.0f, 1.0f)));
    const float3 normal = normalize(lerp(
        animatedNormal,
        flatNormal,
        saturate(WaterReflectionParameters[4].y)));
    return normalize(reflect(
        incidentViewFromScreen(input.packedUv, rightEye),
        normal));
}

float2 packEyeUv(float2 local, bool rightEye)
{
    return float2(
        local.x * 0.5f + (rightEye ? 0.5f : 0.0f),
        local.y);
}

bool reconstructScenePosition(
    float2 packed,
    bool rightEye,
    out float3 position)
{
    uint width;
    uint height;
    WaterSceneDepth.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        position = 0.0f;
        return false;
    }
    const uint2 pixel = min(
        (uint2)(WaterReflectionParameters[5].xy + saturate(packed) * WaterReflectionParameters[5].zw),
        uint2(width - 1u, height - 1u));
    const float rawDepth = WaterSceneDepth.Load(int3(pixel, 0)).x;
    if (!isfinite(rawDepth) || rawDepth<=0.01f || rawDepth>=1.0f) {
        position = 0.0f;
        return false;
    }

    const bool compressedNear = rawDepth <= 0.01f;
    const float adjustedDepth = compressedNear ?
        rawDepth * 100.0f : rawDepth * 1.01f - 0.01f;
    const float2 local = eyeLocalUv(packed, rightEye);
    const float4 clip = float4(
        local.x * 2.0f - 1.0f,
        (1.0f - local.y) * 2.0f - 1.0f,
        adjustedDepth,
        1.0f);
    const uint eyeOffset = rightEye ? 4u : 0u;
    const uint firstRow = eyeOffset + (compressedNear ? 40u : 32u);
    const float4 homogeneous = multiplyRows(firstRow, clip);
    if (!all(isfinite(homogeneous)) || abs(homogeneous.w) <= 1.0e-6f) {
        position = 0.0f;
        return false;
    }
    position = homogeneous.xyz / homogeneous.w;
    return all(isfinite(position));
}

bool projectPosition(
    float3 position,
    bool rightEye,
    out float2 packed)
{
    const uint eyeOffset = rightEye ? 4u : 0u;
    const float4 clip = multiplyRows(
        eyeOffset + 4u,
        float4(position, 1.0f));
    if (!all(isfinite(clip)) || clip.w <= 1.0e-6f) {
        packed = 0.0f;
        return false;
    }
    const float2 local = float2(
        clip.x / clip.w * 0.5f + 0.5f,
        clip.y / clip.w * -0.5f + 0.5f);
    if (!all(isfinite(local)) ||
        any(local <= 0.0f) || any(local >= 1.0f)) {
        packed = 0.0f;
        return false;
    }
    packed = packEyeUv(local, rightEye);
    return rightEye ? packed.x > 0.5f : packed.x < 0.5f;
}

float3 eyeOriginFromMidpoint(bool rightEye)
{
    const float3 midpoint =
        (Camera[59u].xyz + Camera[60u].xyz) * 0.5f;
    return Camera[rightEye ? 60u : 59u].xyz - midpoint;
}

float3 viewToMidpointWorld(float3 position, bool rightEye)
{
    return transformRows(20u, position) +
        eyeOriginFromMidpoint(rightEye);
}

float3 midpointWorldToView(float3 position, bool rightEye)
{
    return transformRows(
        0u,
        position - eyeOriginFromMidpoint(rightEye));
}

bool projectMidpointWorld(
    float3 position,
    bool rightEye,
    out float2 packed)
{
    return projectPosition(
        midpointWorldToView(position, rightEye),
        rightEye,
        packed);
}

bool reconstructSceneMidpointWorld(
    float2 packed,
    bool rightEye,
    out float3 position)
{
    float3 viewPosition;
    if (!reconstructScenePosition(packed, rightEye, viewPosition)) {
        position = 0.0f;
        return false;
    }
    position = viewToMidpointWorld(viewPosition, rightEye);
    return all(isfinite(position));
}

bool sharedDepthGap(
    float3 rayPosition,
    float3 rayDirection,
    out float gap,
    out float3 scenePosition)
{
    float2 leftUv;
    float2 rightUv;
    float3 leftScene;
    float3 rightScene;
    const bool leftValid =
        projectMidpointWorld(rayPosition, false, leftUv) &&
        reconstructSceneMidpointWorld(leftUv, false, leftScene);
    const bool rightValid =
        projectMidpointWorld(rayPosition, true, rightUv) &&
        reconstructSceneMidpointWorld(rightUv, true, rightScene);
    if (!leftValid && !rightValid) {
        gap = -1.0e20f;
        scenePosition = 0.0f;
        return false;
    }

    const float leftGap = leftValid ?
        dot(rayPosition - leftScene, rayDirection) : -1.0e20f;
    const float rightGap = rightValid ?
        dot(rayPosition - rightScene, rayDirection) : -1.0e20f;
    if (rightGap > leftGap) {
        gap = rightGap;
        scenePosition = rightScene;
    } else {
        gap = leftGap;
        scenePosition = leftScene;
    }
    return isfinite(gap) && gap > -1.0e19f;
}

// Keep the visibility fix shared with the optimized tracer so this fixture
// independently measures traversal changes rather than the acceptance fix.
float waterOwnEyeConfidence(float disagreement, float thickness)
{
    return 1.0f - smoothstep(thickness * 0.75f, thickness, disagreement);
}

float4 TraceScreenWaterReflection(WaterRaytraceInput input)
{
    const bool rightEye = input.eyeIndex != 0u;
    const float3 receiver = viewToMidpointWorld(
        receiverViewFromScreen(input, rightEye),
        rightEye);
    const float3 reflection = normalize(transformRows(
        20u,
        reflectedWaterView(input, rightEye)));
    if (!all(isfinite(receiver)) || !all(isfinite(reflection))) {
        return 0.0f;
    }
    const float4 worldReflection = 0;

    static const uint maximumSteps = 32u;
    static const uint refinementSteps = 5u;
    static const float minimumTravel = 2.0f;
    static const float maximumTravel = 1000.0f;
    float previousTravel = minimumTravel;
    float previousGap = 0.0f;
    bool previousValid = false;

    [loop]
    for (uint step = 0u; step < maximumSteps; ++step) {
        const float normalizedStep =
            (float)(step + 1u) / (float)maximumSteps;
        const float travel = lerp(
            minimumTravel,
            maximumTravel,
            normalizedStep * normalizedStep);
        const float3 rayPosition = receiver + reflection * travel;
        float3 scenePosition;
        float gap;
        if (!sharedDepthGap(
            rayPosition,
            reflection,
            gap,
            scenePosition)) {
            previousValid = false;
            continue;
        }

        if (previousValid && previousGap < 0.0f && gap >= 0.0f) {
            float lower = previousTravel;
            float upper = travel;
            float3 hitScene = scenePosition;
            [loop]
            for (uint refine = 0u; refine < refinementSteps; ++refine) {
                const float candidateTravel = (lower + upper) * 0.5f;
                const float3 candidateRay =
                    receiver + reflection * candidateTravel;
                float3 candidateScene;
                float candidateGap;
                if (!sharedDepthGap(
                    candidateRay,
                    reflection,
                    candidateGap,
                    candidateScene)) {
                    lower = candidateTravel;
                    continue;
                }
                if (candidateGap >= 0.0f) {
                    upper = candidateTravel;
                    hitScene = candidateScene;
                } else {
                    lower = candidateTravel;
                }
            }

            const float hitTravel = dot(
                hitScene - receiver,
                reflection);
            const float3 closest = receiver +
                reflection * max(hitTravel, 0.0f);
            const float separation = length(hitScene - closest);
            const float thickness =
                1.0f + max(hitTravel, 0.0f) * 0.01f;
            if (hitTravel > minimumTravel && separation <= thickness) {
                float2 hitUv;
                if (!projectMidpointWorld(
                        hitScene,
                        rightEye,
                        hitUv)) {
                    previousTravel = travel;
                    previousGap = gap;
                    previousValid = true;
                    continue;
                }
                float3 ownEyeScene;
                if (!reconstructSceneMidpointWorld(hitUv, rightEye, ownEyeScene) ||
                    length(ownEyeScene - hitScene) > thickness) {
                    return worldReflection;
                }
                const float2 localHit = eyeLocalUv(hitUv, rightEye);
                const float visibilityFade = waterOwnEyeConfidence(
                    length(ownEyeScene - hitScene), thickness);
                const float edge = min(
                    min(localHit.x, 1.0f - localHit.x),
                    min(localHit.y, 1.0f - localHit.y));
                const float edgeFade = saturate(edge / 0.05f);
                const float travelFade =
                    1.0f - saturate(upper / maximumTravel);
                uint width;
                uint height;
                WaterSceneDepth.GetDimensions(width, height);
                const float selfFade = saturate(
                    length(
                        (hitUv - input.packedUv) *
                        WaterReflectionParameters[5].zw) /
                    4.0f);
                const float separationFade =
                    1.0f - saturate(separation / thickness);
                const float confidence =
                    edgeFade * travelFade * selfFade * separationFade * visibilityFade;
                // A clamp sampler clamps the full packed image, not its eye
                // halves. Keep the linear filter footprint inside this eye.
                uint colorWidth, colorHeight;
                WaterSceneColor.GetDimensions(colorWidth, colorHeight);
                if (colorWidth < 2u || colorHeight == 0u) return worldReflection;
                // The native colour can be downsampled, but occupies the same
                // fraction of its allocation as the full-size depth viewport.
                const float4 rect = WaterReflectionParameters[5] / float4(width,height,width,height);
                const float2 colorSize = float2(colorWidth,colorHeight);
                // A downsampled eye can end between pixel centres. Restrict
                // both bilinear taps to centres owned by this eye.
                const float2 first = (ceil((rect.xy + float2(rightEye ? 0.5f : 0.0f,0) * rect.zw) * colorSize - 0.5f) + 0.5f) / colorSize;
                const float2 last = (ceil((rect.xy + float2(rightEye ? 1.0f : 0.5f,1) * rect.zw) * colorSize - 0.5f) - 0.5f) / colorSize;
                const float2 sampleUv = clamp(rect.xy + hitUv * rect.zw,
                    first, last);
                const float3 color = WaterSceneColor.SampleLevel(
                    WaterEnvironmentSampler,
                    sampleUv,
                    0.0f).rgb;
                const float ownership = saturate(
                    confidence);
                return ownership >= worldReflection.a ?
                    float4(max(color, 0.0f), ownership) :
                    worldReflection;
            }
        }

        previousTravel = travel;
        previousGap = gap;
        previousValid = true;
    }
    return worldReflection;
}

