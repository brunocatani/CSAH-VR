// Shared stereo capture with deterministic nearest-surface reprojection.
// Depth/owner/resolve passes adapt the DynamicCubemapF4 1.3.1 approach (GPL-3.0).
Texture2D<float3> ReflectionFreeRadiance : register(t0);
Texture2D<float> SceneDepth : register(t1);
TextureCube<float3> PreviousEnvironment : register(t2);
TextureCube<float> PreviousValidity : register(t3);
TextureCube<float4> PreviousPosition : register(t4);
TextureCube<float4> PreviousSky : register(t5);
RWTexture2DArray<float3> EnvironmentMip : register(u0);
RWTexture2DArray<float> EnvironmentValidity : register(u1);
RWTexture2DArray<float4> EnvironmentPosition : register(u2);
RWTexture2DArray<float4> SkyHistory : register(u3);
RWTexture2DArray<uint> NearestDepth : register(u4);
RWTexture2DArray<uint> NearestOwner : register(u5);
SamplerState LinearClampSampler : register(s0);

cbuffer EnvironmentUpdateConstants : register(b11)
{
    uint2 SourceExtent;
    uint TargetExtent;
    uint HistoryAvailable;
    uint UpdatePass;
    float HistoryBlend;
    uint2 Reserved;
    float3 PreviousProbeOrigin;
    uint PreviousProbeOriginValid;
};

// The exact FO4VR DFComposite draw binds an 85-float4 buffer at b12. Local
// material DXBC independently proves c0..c2 rotate world vectors into the
// current view, c4..c11 are the ordinary per-eye projections, c32..c39 are
// the ordinary per-eye inverse projections, and c59/c60 are the live per-eye
// origins. The verified 85-row VR layout carries persistent camera-position
// adjustments at c80/c81. Do not use the compressed c63..c70 rows here: the
// headset-qualified GPFIX world cache proved that they do not preserve a
// stable world-direction key under HMD rotation.
cbuffer Fo4VrSceneConstants : register(b12)
{
    float4 Scene[85];
};

static const float PositionEpsilon = 1.0e-5f;
static const float MinimumCaptureDistance = 16.5f;
static const float MaximumCaptureDistance = 2000000.0f;
groupshared float4 SharedCameraOrigins[2];

bool CameraPositionAdjust(uint eye, out float3 adjustment)
{
    adjustment = Scene[80u + eye].xyz;
    const bool finite = all(adjustment == adjustment) &&
        all(abs(adjustment) < MaximumCaptureDistance * 4.0f);
    if (!finite)
    {
        adjustment = 0.0f;
    }
    return finite;
}

bool CameraOrigin(uint eye, out float3 origin)
{
    // Exact FO4VR reflection-composite DXBC exposes the live camera-relative
    // eye origins directly at c59/c60. Matrix inversion of the projection
    // rows yields the camera-relative coordinate origin and discards headset
    // translation, which makes position history follow the player.
    origin = Scene[59u + eye].xyz;
    const bool finite = all(origin == origin) &&
        all(abs(origin) < MaximumCaptureDistance * 4.0f);
    if (!finite)
    {
        origin = 0.0f;
    }
    return finite;
}

bool ReconstructPersistentWorldPosition(
    float2 localUv,
    float depth,
    uint eye,
    out float3 worldPosition,
    out float distance)
{
    worldPosition = 0.0f;
    distance = 0.0f;
    if (!(depth > PositionEpsilon && depth < 1.0f - PositionEpsilon))
    {
        return false;
    }

    const float mappedDepth = depth <= 0.01f ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float4 clipPosition = float4(
        localUv.x * 2.0f - 1.0f,
        1.0f - localUv.y * 2.0f,
        mappedDepth,
        1.0f);
    const uint inverseProjectionBase = 32u + eye * 4u;
    const float4 homogeneousView = float4(
        dot(Scene[inverseProjectionBase + 0u], clipPosition),
        dot(Scene[inverseProjectionBase + 1u], clipPosition),
        dot(Scene[inverseProjectionBase + 2u], clipPosition),
        dot(Scene[inverseProjectionBase + 3u], clipPosition));
    if (!all(homogeneousView == homogeneousView) ||
        !(abs(homogeneousView.w) > PositionEpsilon))
    {
        return false;
    }

    const float3 eyeRelativeView =
        homogeneousView.xyz / homogeneousView.w;
    distance = length(eyeRelativeView);
    const float3 leftOrigin = Scene[59u].xyz;
    const float3 rightOrigin = Scene[60u].xyz;
    const float3 midpointOrigin = (leftOrigin + rightOrigin) * 0.5f;
    const float3 eyeOrigin = eye == 0u ? leftOrigin : rightOrigin;
    const float3 eyeToMidpointWorld = eyeOrigin - midpointOrigin;
    const float3 eyeToMidpointView = float3(
        dot(Scene[0u].xyz, eyeToMidpointWorld),
        dot(Scene[1u].xyz, eyeToMidpointWorld),
        dot(Scene[2u].xyz, eyeToMidpointWorld));
    const float3 midpointRelativeView =
        eyeRelativeView + eyeToMidpointView;
    const float3 midpointRelativeWorld = float3(
        dot(Scene[20u].xyz, midpointRelativeView),
        dot(Scene[21u].xyz, midpointRelativeView),
        dot(Scene[22u].xyz, midpointRelativeView));
    worldPosition = midpointOrigin + midpointRelativeWorld +
        Scene[80u + eye].xyz;
    return distance > 0.0f && distance < MaximumCaptureDistance &&
        all(worldPosition == worldPosition) &&
        all(abs(worldPosition) < MaximumCaptureDistance * 4.0f);
}

float3 CubeDirection(uint face, float2 coordinate)
{
    float3 direction = 0.0f;
    switch (face)
    {
    case 0:
        direction = float3(1.0f, -coordinate.y, -coordinate.x);
        break;
    case 1:
        direction = float3(-1.0f, -coordinate.y, coordinate.x);
        break;
    case 2:
        direction = float3(coordinate.x, 1.0f, coordinate.y);
        break;
    case 3:
        direction = float3(coordinate.x, -1.0f, -coordinate.y);
        break;
    case 4:
        direction = float3(coordinate.x, -coordinate.y, 1.0f);
        break;
    default:
        direction = float3(-coordinate.x, -coordinate.y, -1.0f);
        break;
    }
    return normalize(direction);
}

bool ProjectEye(float3 worldDirection, uint eye, out float2 localUv,
    out float edgeWeight)
{
    const float3 viewDirection = float3(
        dot(Scene[0u].xyz, worldDirection),
        dot(Scene[1u].xyz, worldDirection),
        dot(Scene[2u].xyz, worldDirection));
    const float4 viewVector = float4(viewDirection, 0.0f);
    const uint projectionBase = 4u + eye * 4u;
    const float4 clipDirection = float4(
        dot(Scene[projectionBase + 0u], viewVector),
        dot(Scene[projectionBase + 1u], viewVector),
        dot(Scene[projectionBase + 2u], viewVector),
        dot(Scene[projectionBase + 3u], viewVector));
    if (!all(clipDirection == clipDirection) ||
        !(clipDirection.w > PositionEpsilon))
    {
        localUv = 0.0f;
        edgeWeight = 0.0f;
        return false;
    }

    const float2 ndc = clipDirection.xy / clipDirection.w;
    const float2 edge = 1.0f - abs(ndc);
    if (min(edge.x, edge.y) <= 0.0f)
    {
        localUv = 0.0f;
        edgeWeight = 0.0f;
        return false;
    }

    // FO4VR motion-vector DXBC uses the algebraically equivalent
    // previousUV-currentUV transform (-0.5,+0.5), proving this mapping.
    localUv = ndc * float2(0.5f, -0.5f) + 0.5f;
    edgeWeight = saturate(min(edge.x, edge.y) * 8.0f);
    edgeWeight *= edgeWeight;
    return true;
}

bool SampleEye(
    float3 worldDirection,
    uint eye,
    out float3 radiance,
    out float weight,
    out float3 worldPosition,
    out float positionWeight)
{
    worldPosition = 0.0f;
    positionWeight = 0.0f;
    float2 localUv;
    if (!ProjectEye(worldDirection, eye, localUv, weight))
    {
        radiance = 0.0f;
        return false;
    }

    const float2 halfTexel = 0.5f / float2(SourceExtent);
    float2 packedUv = float2((localUv.x + (float)eye) * 0.5f, localUv.y);
    const float eyeMinimum = (float)eye * 0.5f + halfTexel.x;
    const float eyeMaximum = ((float)eye + 1.0f) * 0.5f - halfTexel.x;
    packedUv.x = clamp(packedUv.x, eyeMinimum, eyeMaximum);
    packedUv.y = clamp(packedUv.y, halfTexel.y, 1.0f - halfTexel.y);

    // Both zero-depth sky and finite surfaces are valid radiance. Finite
    // positions are converted from camera-relative to persistent coordinates;
    // malformed samples fail closed.
    const float depth = SceneDepth.SampleLevel(
        LinearClampSampler, packedUv, 0.0f);
    const bool validDepth = depth == depth && depth >= 0.0f && depth <= 1.0f;
    radiance = ReflectionFreeRadiance.SampleLevel(
        LinearClampSampler, packedUv, 0.0f);
    const bool validRadiance = all(radiance == radiance) &&
        all(radiance >= 0.0f) && all(radiance < 65504.0f);
    if (!validDepth || !validRadiance)
    {
        radiance = 0.0f;
        weight = 0.0f;
        return false;
    }

    float distance;
    if (ReconstructPersistentWorldPosition(
            localUv,
            depth,
            eye,
            worldPosition,
            distance))
    {
        // Hands, weapons, and headset-adjacent geometry must not become the
        // environment reflected by the world around the player.
        if (distance < MinimumCaptureDistance)
        {
            radiance = 0.0f;
            weight = 0.0f;
            worldPosition = 0.0f;
            return false;
        }
        positionWeight = weight;
    }
    return true;
}

uint3 CubeCoordinate(float3 direction)
{
    const float3 a = abs(direction);
    uint face;
    float2 uv;
    if (a.x >= a.y && a.x >= a.z)
    {
        face = direction.x >= 0.0f ? 0u : 1u;
        uv = float2(direction.x >= 0.0f ? -direction.z : direction.z, -direction.y) / a.x;
    }
    else if (a.y >= a.z)
    {
        face = direction.y >= 0.0f ? 2u : 3u;
        uv = float2(direction.x, direction.y >= 0.0f ? direction.z : -direction.z) / a.y;
    }
    else
    {
        face = direction.z >= 0.0f ? 4u : 5u;
        uv = float2(direction.z >= 0.0f ? direction.x : -direction.x, -direction.y) / a.z;
    }
    return uint3(min(uint2(saturate(uv * 0.5f + 0.5f) * TargetExtent), TargetExtent - 1u), face);
}

uint LinearIndex(uint3 coordinate)
{
    return (coordinate.z * TargetExtent + coordinate.y) * TargetExtent + coordinate.x;
}

float3 DirectionFromIndex(uint index)
{
    const uint3 coordinate = uint3(index % TargetExtent,
        (index / TargetExtent) % TargetExtent, index / (TargetExtent * TargetExtent));
    return CubeDirection(coordinate.z,
        (float2(coordinate.xy) + 0.5f) * (2.0f / TargetExtent) - 1.0f);
}

[numthreads(8, 8, 1)]
void main(
    uint3 dispatchId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupThreadId.x == 0u && groupThreadId.y == 0u)
    {
        float3 leftOrigin;
        float3 rightOrigin;
        const bool leftValid = CameraOrigin(0u, leftOrigin);
        const bool rightValid = CameraOrigin(1u, rightOrigin);
        SharedCameraOrigins[0] = float4(leftOrigin, leftValid ? 1.0f : 0.0f);
        SharedCameraOrigins[1] = float4(rightOrigin, rightValid ? 1.0f : 0.0f);
    }
    GroupMemoryBarrierWithGroupSync();

    if (dispatchId.x >= TargetExtent || dispatchId.y >= TargetExtent ||
        dispatchId.z >= 6u)
    {
        return;
    }

    const float2 faceCoordinate =
        ((float2(dispatchId.xy) + 0.5f) / (float)TargetExtent) * 2.0f - 1.0f;
    const float3 worldDirection = CubeDirection(
        dispatchId.z, faceCoordinate);

    float3 cameraOrigins[2];
    bool cameraOriginValid[2];
    float3 cameraPositionAdjusts[2];
    bool cameraPositionAdjustValid[2];
    cameraOrigins[0] = SharedCameraOrigins[0].xyz;
    cameraOrigins[1] = SharedCameraOrigins[1].xyz;
    cameraOriginValid[0] = SharedCameraOrigins[0].w > 0.0f;
    cameraOriginValid[1] = SharedCameraOrigins[1].w > 0.0f;
    cameraPositionAdjustValid[0] = CameraPositionAdjust(
        0u, cameraPositionAdjusts[0]);
    cameraPositionAdjustValid[1] = CameraPositionAdjust(
        1u, cameraPositionAdjusts[1]);
    float3 cameraCenter = 0.0f;
    float cameraCount = 0.0f;
    [unroll]
    for (uint cameraIndex = 0u; cameraIndex < 2u; ++cameraIndex)
    {
        if (cameraOriginValid[cameraIndex] &&
            cameraPositionAdjustValid[cameraIndex])
        {
            cameraCenter += cameraOrigins[cameraIndex] +
                cameraPositionAdjusts[cameraIndex];
            cameraCount += 1.0f;
        }
    }
    cameraCenter = cameraCount > 0.0f ?
        cameraCenter / cameraCount : 0.0f;

    // A teleport must not import either geometry or sky from the old location.
    const bool retainHistory = HistoryAvailable != 0u &&
        PreviousProbeOriginValid != 0u && cameraCount > 0.0f &&
        distance(cameraCenter, PreviousProbeOrigin) <= 2048.0f;

    if (UpdatePass < 2u)
    {
        if (!retainHistory) return;
        const float4 hit = PreviousPosition.SampleLevel(LinearClampSampler, worldDirection, 0.0f);
        const float validity = PreviousValidity.SampleLevel(LinearClampSampler, worldDirection, 0.0f);
        const float3 translated = hit.xyz - cameraCenter;
        const float hitDistance = length(translated);
        if (!(hit.w > PositionEpsilon && validity > PositionEpsilon &&
            all(isfinite(hit)) && isfinite(hitDistance) &&
            hitDistance >= MinimumCaptureDistance && hitDistance < MaximumCaptureDistance)) return;
        const uint3 target = CubeCoordinate(translated);
        const uint depthKey = asuint(hitDistance);
        if (UpdatePass == 0u) InterlockedMin(NearestDepth[target], depthKey);
        else if (NearestDepth[target] == depthKey)
            InterlockedMin(NearestOwner[target], LinearIndex(dispatchId));
        return;
    }

    float4 sky = retainHistory ? PreviousSky.SampleLevel(
        LinearClampSampler, worldDirection, 0.0f) : 0.0f;
    float3 previousRadiance = 0.0f;
    float previousValidity = 0.0f;
    float4 previousPosition = 0.0f;
    const uint owner = NearestOwner[dispatchId];
    if (retainHistory && owner < TargetExtent * TargetExtent * 6u)
    {
        const float3 sourceDirection = DirectionFromIndex(owner);
        previousRadiance = PreviousEnvironment.SampleLevel(LinearClampSampler, sourceDirection, 0.0f);
        previousValidity = saturate(PreviousValidity.SampleLevel(LinearClampSampler, sourceDirection, 0.0f));
        previousPosition = PreviousPosition.SampleLevel(LinearClampSampler, sourceDirection, 0.0f);
    }

    float3 accumulated = 0.0f;
    float totalWeight = 0.0f;
    float3 accumulatedPosition = 0.0f;
    float totalPositionWeight = 0.0f;
    float3 observedSky = 0.0f;
    float skyWeight = 0.0f;
    [unroll]
    for (uint eye = 0u; eye < 2u; ++eye)
    {
        float3 radiance, position;
        float weight, positionWeight;
        if (SampleEye(worldDirection, eye, radiance, weight, position, positionWeight))
        {
            if (positionWeight > 0.0f)
            {
                accumulated += radiance * weight;
                totalWeight += weight;
                accumulatedPosition += position * positionWeight;
                totalPositionWeight += positionWeight;
            }
            else if (max(radiance.r, max(radiance.g, radiance.b)) > 0.0001f)
            {
                // A clear but unresolved background is not an observation of black sky.
                observedSky += radiance * weight;
                skyWeight += weight;
            }
        }
    }
    if (skyWeight > 0.0f)
        sky = float4(observedSky / skyWeight, saturate(skyWeight));
    SkyHistory[dispatchId] = sky;

    float3 radiance = previousRadiance;
    float validity = previousValidity;
    float4 position = previousPosition;
    if (totalWeight > 0.0f)
    {
        const float3 current = accumulated / totalWeight;
        const float3 currentPosition = accumulatedPosition / totalPositionWeight;
        // A newly visible surface must replace an occluded surface immediately.
        const float hitDistance = distance(currentPosition, cameraCenter);
        const bool sameSurface = previousPosition.w > 0.0f &&
            distance(currentPosition, previousPosition.xyz) <= max(2.0f, hitDistance * 0.02f);
        const float historyWeight = sameSurface ? saturate(HistoryBlend) * previousValidity : 0.0f;
        radiance = lerp(current, previousRadiance, historyWeight);
        validity = max(saturate(totalWeight), previousValidity * (sameSurface ? 1.0f : 0.0f));
        position = float4(currentPosition, saturate(totalPositionWeight));
    }
    else if (skyWeight > 0.0f || previousValidity <= 0.0f)
    {
        // Observed sky replaces old geometry. Unseen holes use retained sky only;
        // the consumer supplies the native cubemap when neither has been observed.
        radiance = sky.rgb;
        validity = sky.a;
        position = 0.0f;
    }
    EnvironmentMip[dispatchId] = max(0.0f, radiance * validity);
    EnvironmentValidity[dispatchId] = saturate(validity);
    EnvironmentPosition[dispatchId] = position;
}
