// FO4VR ImageSpace[129] / BSImagespaceShaderSSLRRaytracing replacement.
// March one reflected ray in the selected eye's linear view space. Valid
// current hits own by geometric confidence at every distance. Misses and
// weak hits use Community Shaders' shared position-aware environment.

cbuffer SslrParameters : register(b0)
{
    float4 SslrParams;
};

cbuffer SslrEnvironmentParameters : register(b11)
{
    float4 PublishedProbeOriginAndValid;
    float4 PreviousProbeOriginAndValid;
    // x=environment transition, y=previous available,
    // z=current-hit minimum confidence, w=current environment available.
    float4 EnvironmentControl;
};

cbuffer CameraParameters : register(b12)
{
    float4 CameraData[77];
};

Texture2D<float> DepthTexture : register(t0);
Texture2D<float2> DirectionTexture : register(t1);
Texture2D<float> ViewDepthTexture : register(t2);
Texture2D<float4> SceneColorTexture : register(t3);
TextureCube<float3> PublishedEnvironment : register(t4);
TextureCube<float> PublishedValidity : register(t5);
TextureCube<float4> PublishedPosition : register(t6);
TextureCube<float3> PreviousEnvironment : register(t7);
TextureCube<float> PreviousValidity : register(t8);
TextureCube<float4> PreviousPosition : register(t9);
SamplerState SceneColorSampler : register(s3);
SamplerState EnvironmentSampler : register(s4);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 multiplyRows(uint firstRow, float4 value)
{
    return float4(
        dot(CameraData[firstRow + 0u], value),
        dot(CameraData[firstRow + 1u], value),
        dot(CameraData[firstRow + 2u], value),
        dot(CameraData[firstRow + 3u], value));
}

float3 transformRows(uint firstRow, float3 value)
{
    return float3(
        dot(CameraData[firstRow + 0u].xyz, value),
        dot(CameraData[firstRow + 1u].xyz, value),
        dot(CameraData[firstRow + 2u].xyz, value));
}

float3 decodeDirection(float2 encoded)
{
    float3 direction = float3(
        encoded,
        1.0f - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0f)
    {
        const float2 signs = float2(
            direction.x >= 0.0f ? 1.0f : -1.0f,
            direction.y >= 0.0f ? 1.0f : -1.0f);
        direction.xy = (1.0f - abs(direction.yx)) * signs;
    }
    return normalize(direction);
}

bool loadPrepassData(
    float2 rasterPosition,
    out float2 encodedDirection,
    out float sourceViewDepth)
{
    uint directionWidth;
    uint directionHeight;
    uint depthWidth;
    uint depthHeight;
    DirectionTexture.GetDimensions(directionWidth, directionHeight);
    ViewDepthTexture.GetDimensions(depthWidth, depthHeight);
    if (directionWidth == 0u || directionHeight == 0u ||
        directionWidth != depthWidth || directionHeight != depthHeight)
    {
        encodedDirection = 0.0f;
        sourceViewDepth = 0.0f;
        return false;
    }
    const uint2 maximumPixel = uint2(
        directionWidth - 1u,
        directionHeight - 1u);
    const uint2 pixel = min(
        (uint2)max(rasterPosition, 0.0f),
        maximumPixel);
    encodedDirection = DirectionTexture.Load(int3(pixel, 0));
    sourceViewDepth = ViewDepthTexture.Load(int3(pixel, 0));
    return true;
}

float loadViewDepth(float2 uv)
{
    uint width;
    uint height;
    DepthTexture.GetDimensions(width, height);
    const uint2 maximumPixel = uint2(width - 1u, height - 1u);
    const uint2 pixel = min(
        (uint2)(saturate(uv) * float2(width, height)),
        maximumPixel);
    return DepthTexture.Load(int3(pixel, 0));
}

float2 eyeLocalUv(float2 packed, bool rightEye)
{
    return float2(
        (packed.x - (rightEye ? 0.5f : 0.0f)) * 2.0f,
        packed.y);
}

float2 packedUv(float2 local, bool rightEye)
{
    return float2(
        local.x * 0.5f + (rightEye ? 0.5f : 0.0f),
        local.y);
}

bool reconstructLinearPosition(
    float2 uv,
    float viewDepth,
    bool rightEye,
    out float3 position)
{
    if (!isfinite(viewDepth) || viewDepth <= 1.0e-5f)
    {
        position = 0.0f;
        return false;
    }
    const float2 localUv = eyeLocalUv(uv, rightEye);
    const float4 clipPosition = float4(
        localUv.x * 2.0f - 1.0f,
        (1.0f - localUv.y) * 2.0f - 1.0f,
        0.5f,
        1.0f);
    const uint eyeOffset = rightEye ? 4u : 0u;
    const float4 homogeneous = multiplyRows(
        eyeOffset + 32u,
        clipPosition);
    if (!all(isfinite(homogeneous)) || abs(homogeneous.w) <= 1.0e-6f)
    {
        position = 0.0f;
        return false;
    }
    const float3 eyeRay = homogeneous.xyz / homogeneous.w;
    if (!all(isfinite(eyeRay)) || abs(eyeRay.z) <= 1.0e-6f)
    {
        position = 0.0f;
        return false;
    }
    position = eyeRay * (viewDepth / eyeRay.z);
    return all(isfinite(position));
}

bool projectPosition(
    float3 position,
    bool rightEye,
    out float2 uv)
{
    const uint eyeOffset = rightEye ? 4u : 0u;
    const float4 clipPosition = multiplyRows(
        eyeOffset + 4u,
        float4(position, 1.0f));
    if (!all(isfinite(clipPosition)) || clipPosition.w <= 1.0e-6f)
    {
        uv = 0.0f;
        return false;
    }
    const float2 localUv = float2(
        clipPosition.x / clipPosition.w * 0.5f + 0.5f,
        clipPosition.y / clipPosition.w * -0.5f + 0.5f);
    if (!all(isfinite(localUv)) ||
        any(localUv <= 0.0f) || any(localUv >= 1.0f))
    {
        uv = 0.0f;
        return false;
    }
    uv = packedUv(localUv, rightEye);
    return rightEye ? uv.x > 0.5f : uv.x < 0.5f;
}

float viewDepthGap(
    float3 rayPosition,
    float2 uv,
    bool rightEye,
    out float3 scenePosition)
{
    const float sceneViewDepth = loadViewDepth(uv);
    if (!isfinite(sceneViewDepth) ||
        !reconstructLinearPosition(
            uv,
            sceneViewDepth,
            rightEye,
            scenePosition))
    {
        scenePosition = 0.0f;
        return -1.0e20f;
    }
    return rayPosition.z - scenePosition.z;
}

bool receiverWorldPosition(
    float3 receiverView,
    bool rightEye,
    out float3 receiverWorld)
{
    const float3 leftOrigin = CameraData[59u].xyz;
    const float3 rightOrigin = CameraData[60u].xyz;
    const float3 midpointOrigin = (leftOrigin + rightOrigin) * 0.5f;
    const float3 eyeOrigin = rightEye ? rightOrigin : leftOrigin;
    if (!all(isfinite(midpointOrigin)) || !all(isfinite(eyeOrigin)))
    {
        receiverWorld = 0.0f;
        return false;
    }

    const float3 eyeToMidpointWorld = eyeOrigin - midpointOrigin;
    const float3 eyeToMidpointView = transformRows(
        0u,
        eyeToMidpointWorld);
    const float3 midpointRelativeWorld = transformRows(
        20u,
        receiverView + eyeToMidpointView);
    receiverWorld = midpointOrigin + midpointRelativeWorld;
    return all(isfinite(receiverWorld)) &&
        all(abs(receiverWorld) < 8000000.0f);
}

float3 correctProbeDirection(
    TextureCube<float4> positionTexture,
    float3 direction,
    float3 receiverWorld,
    float3 probeOrigin,
    float originValid)
{
    if (!(originValid > 0.5f))
    {
        return direction;
    }
    const float4 hit = positionTexture.SampleLevel(
        EnvironmentSampler,
        direction,
        0.0f);
    const float3 probeToHit = hit.xyz - probeOrigin;
    const float radius = length(probeToHit);
    const float3 probeToReceiver = receiverWorld - probeOrigin;
    const float receiverDistanceSquared = dot(
        probeToReceiver,
        probeToReceiver);
    const float projection = dot(probeToReceiver, direction);
    const float discriminant = projection * projection -
        (receiverDistanceSquared - radius * radius);
    if (!(hit.w > 1.0e-4f && radius > 16.5f &&
          receiverDistanceSquared < radius * radius &&
          discriminant > 1.0e-4f))
    {
        return direction;
    }
    const float travel = -projection + sqrt(discriminant);
    const float3 corrected = normalize(
        probeToReceiver + direction * travel);
    return normalize(lerp(direction, corrected, saturate(hit.w)));
}

float3 loadWorldRadiance(
    float3 reflectedWorld,
    float3 receiverWorld,
    bool receiverValid,
    out float validity)
{
    validity = 0.0f;
    if (!(EnvironmentControl.w > 0.5f) ||
        !all(isfinite(reflectedWorld)))
    {
        return 0.0f;
    }

    const float3 direction = normalize(reflectedWorld);
    const float3 publishedDirection = receiverValid ?
        correctProbeDirection(
            PublishedPosition,
            direction,
            receiverWorld,
            PublishedProbeOriginAndValid.xyz,
            PublishedProbeOriginAndValid.w) : direction;
    float3 radiance = max(0.0f, PublishedEnvironment.SampleLevel(
        EnvironmentSampler,
        publishedDirection,
        0.0f));
    validity = saturate(PublishedValidity.SampleLevel(
        EnvironmentSampler,
        publishedDirection,
        0.0f));

    if (EnvironmentControl.y > 0.5f && EnvironmentControl.x < 1.0f)
    {
        const float3 previousDirection = receiverValid ?
            correctProbeDirection(
                PreviousPosition,
                direction,
                receiverWorld,
                PreviousProbeOriginAndValid.xyz,
                PreviousProbeOriginAndValid.w) : direction;
        const float3 previousRadiance = max(0.0f,
            PreviousEnvironment.SampleLevel(
                EnvironmentSampler,
                previousDirection,
                0.0f));
        const float previousValidity = saturate(
            PreviousValidity.SampleLevel(
                EnvironmentSampler,
                previousDirection,
                0.0f));
        const float transition = saturate(EnvironmentControl.x);
        radiance = lerp(previousRadiance, radiance, transition);
        validity = lerp(previousValidity, validity, transition);
    }
    return radiance;
}

float3 loadWorldRadianceForReceiver(
    float3 reflectedWorld,
    float3 receiverView,
    bool rightEye,
    out float validity)
{
    float3 receiverWorld;
    const bool receiverWorldValid = receiverWorldPosition(
        receiverView,
        rightEye,
        receiverWorld);
    return loadWorldRadiance(
        reflectedWorld,
        receiverWorld,
        receiverWorldValid,
        validity);
}

float4 main(PixelInput input) : SV_TARGET
{
    float2 encodedDirection;
    float sourceViewDepth;
    if (!loadPrepassData(
            input.position.xy,
            encodedDirection,
            sourceViewDepth) ||
        !isfinite(sourceViewDepth) || abs(sourceViewDepth) <= 1.0e-5f)
    {
        return 0.0f;
    }

    const bool rightEye = input.uv.x >= 0.5f;
    const float3 reflectedWorld = decodeDirection(encodedDirection);
    const float3 reflectedView = normalize(transformRows(
        0u,
        reflectedWorld));
    float3 receiver;
    if (!all(isfinite(reflectedWorld)) ||
        !all(isfinite(reflectedView)) ||
        !reconstructLinearPosition(
            input.uv,
            sourceViewDepth,
            rightEye,
            receiver))
    {
        return 0.0f;
    }

    static const uint maximumSteps = 32u;
    static const uint refinementSteps = 5u;
    static const float minimumTravel = 2.0f;
    static const float maximumTravel = 1000.0f;
    float previousTravel = minimumTravel;
    float previousGap = 0.0f;
    bool previousValid = false;

    [loop]
    for (uint step = 0u; step < maximumSteps; ++step)
    {
        const float normalizedStep =
            (float)(step + 1u) / (float)maximumSteps;
        const float travel = lerp(
            minimumTravel,
            maximumTravel,
            normalizedStep * normalizedStep);
        const float3 rayPosition = receiver + reflectedView * travel;
        float2 rayUv;
        if (!projectPosition(rayPosition, rightEye, rayUv))
        {
            break;
        }

        float3 scenePosition;
        const float gap = viewDepthGap(
            rayPosition,
            rayUv,
            rightEye,
            scenePosition);
        if (gap <= -1.0e19f)
        {
            previousValid = false;
            continue;
        }

        if (previousValid && previousGap < 0.0f && gap >= 0.0f)
        {
            float lower = previousTravel;
            float upper = travel;
            float2 hitUv = rayUv;
            float3 hitScene = scenePosition;
            [loop]
            for (uint refine = 0u; refine < refinementSteps; ++refine)
            {
                const float candidateTravel = (lower + upper) * 0.5f;
                const float3 candidateRay =
                    receiver + reflectedView * candidateTravel;
                float2 candidateUv;
                if (!projectPosition(candidateRay, rightEye, candidateUv))
                {
                    lower = candidateTravel;
                    continue;
                }
                float3 candidateScene;
                const float candidateGap = viewDepthGap(
                    candidateRay,
                    candidateUv,
                    rightEye,
                    candidateScene);
                if (candidateGap >= 0.0f)
                {
                    upper = candidateTravel;
                    hitUv = candidateUv;
                    hitScene = candidateScene;
                }
                else
                {
                    lower = candidateTravel;
                }
            }

            const float hitTravel = dot(
                hitScene - receiver,
                reflectedView);
            const float3 closestPoint = receiver + reflectedView *
                max(hitTravel, 0.0f);
            const float separation = length(hitScene - closestPoint);
            const float thickness =
                1.0f + max(hitTravel, 0.0f) * 0.01f;
            if (hitTravel > minimumTravel && separation <= thickness)
            {
                const float2 localHit = eyeLocalUv(hitUv, rightEye);
                const float edge = min(
                    min(localHit.x, 1.0f - localHit.x),
                    min(localHit.y, 1.0f - localHit.y));
                const float edgeFade = saturate(edge / 0.05f);
                const float travelFade =
                    1.0f - saturate(upper / maximumTravel);
                uint width;
                uint height;
                DepthTexture.GetDimensions(width, height);
                const float selfFade = saturate(
                    length((hitUv - input.uv) * float2(width, height)) /
                    4.0f);
                const float separationFade =
                    1.0f - saturate(separation / thickness);
                const float confidence =
                    edgeFade * travelFade * selfFade * separationFade;
                const float3 color = max(0.0f, SceneColorTexture.SampleLevel(
                    SceneColorSampler,
                    hitUv,
                    0.0f).rgb);
                // A fully owned current-frame hit is independent of the world
                // fallback. Avoid its position reconstruction and six cubemap
                // samples without changing the resulting color or validity.
                if (confidence >= 1.0f - 1.0e-5f)
                {
                    return float4(color, confidence);
                }
                float worldValidity;
                const float3 worldRadiance = loadWorldRadianceForReceiver(
                    reflectedWorld,
                    receiver,
                    rightEye,
                    worldValidity);
                if (worldValidity > 0.05f)
                {
                    // Current visible geometry owns at any hit distance.
                    const float currentOwnership = saturate(
                        (confidence - EnvironmentControl.z) /
                        max(1.0f - EnvironmentControl.z, 1.0e-5f));
                    return float4(
                        lerp(worldRadiance, color, currentOwnership),
                        max(worldValidity, confidence * currentOwnership));
                }
                return float4(color, confidence);
            }
        }

        previousTravel = travel;
        previousGap = gap;
        previousValid = true;
    }
    float worldValidity;
    const float3 worldRadiance = loadWorldRadianceForReceiver(
        reflectedWorld,
        receiver,
        rightEye,
        worldValidity);
    return float4(worldRadiance, worldValidity);
}
