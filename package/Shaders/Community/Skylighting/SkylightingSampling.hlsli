#ifndef CSAH_SKYLIGHTING_SAMPLING
#define CSAH_SKYLIGHTING_SAMPLING
Texture3D<float4> NearSkylightingProbeArray : register(t50);
Texture3D<float4> FarSkylightingProbeArray : register(t51);
Texture3D<float4> NearSkylightingSunHistory : register(t53);
Texture3D<float4> FarSkylightingSunHistory : register(t54);
static const float kPi = 3.14159265358979323846f;
struct ProbeLevelSettings
{
    float4 ArraySize;
    float4 CellSize;
    float4 PositionOffset;
    uint4 ArrayDimensions;
    uint4 ArrayOrigin;
    int4 ValidMargin;
};

#ifdef SKYLIGHTING_PARAMETERS_SRV
Buffer<float4> SkylightingParameters : register(t52);
ProbeLevelSettings ReadSkylightingLevel(uint base)
{
    ProbeLevelSettings level;
    level.ArraySize = SkylightingParameters[base];
    level.CellSize = SkylightingParameters[base + 1];
    level.PositionOffset = SkylightingParameters[base + 2];
    level.ArrayDimensions = asuint(SkylightingParameters[base + 3]);
    level.ArrayOrigin = asuint(SkylightingParameters[base + 4]);
    level.ValidMargin = asint(SkylightingParameters[base + 5]);
    return level;
}
#define NearLevel ReadSkylightingLevel(5)
#define FarLevel ReadSkylightingLevel(11)
#define NearSunLevel ReadSkylightingLevel(20)
#define FarSunLevel ReadSkylightingLevel(26)
#define Response SkylightingParameters[18]
#define ProbeWorldOrigin SkylightingParameters[19]
#else
cbuffer SkylightingSettings : register(b13)
{
    column_major float4x4 OcclusionViewProjection;
    float4 OcclusionDirection;
    ProbeLevelSettings NearLevel;
    ProbeLevelSettings FarLevel;
    uint4 UpdateControl;
    // x=minimum diffuse visibility, y=minimum specular visibility,
    // z=feature active, w=reserved.
    float4 Response;
    float4 ProbeWorldOrigin;
    ProbeLevelSettings NearSunLevel;
    ProbeLevelSettings FarSunLevel;
};

#endif
float4 CosineLobe(float3 direction)
{
    return float4(
        0.88622692545275801370f,
        -1.02332670794648848850f * direction.y,
        1.02332670794648848850f * direction.z,
        -1.02332670794648848850f * direction.x);
}

float4 DirectionalLobe(float3 direction)
{
    return float4(
        0.28209479177387814347f,
        -0.48860251190291992159f * direction.y,
        0.48860251190291992159f * direction.z,
        -0.48860251190291992159f * direction.x);
}

float4 FauxSpecularLobe(
    float3 normal,
    float3 viewDirection,
    float roughness)
{
    const float factor =
        (1.0f - roughness) *
        (sqrt(max(1.0f - roughness, 0.0f)) + roughness);
    const float3 reflection = reflect(-viewDirection, normal);
    const float3 dominant = normalize(lerp(normal, reflection, factor));
    const float roughnessSquared = roughness * roughness;
    const float halfAngle = clamp(
        4.1679f * roughnessSquared * roughnessSquared -
            9.0127f * roughnessSquared * roughness +
            4.6161f * roughnessSquared +
            1.7048f * roughness +
            0.1f,
        0.0f,
        0.5f * kPi);
    const float broadness = halfAngle / (0.5f * kPi);
    return lerp(
        CosineLobe(dominant) / kPi,
        DirectionalLobe(dominant),
        broadness);
}

float LevelExtent(
    float3 relativePosition,
    ProbeLevelSettings level)
{
    const float3 normalized = abs(
        (relativePosition - level.PositionOffset.xyz) /
        max(level.ArraySize.xyz * 0.5f, 1.0f));
    return max(length(normalized.xy), normalized.z);
}

bool SampleProbeField(
    uint field,
    uint levelIndex,
    float3 relativeWorldPosition,
    float3 normal,
    out float4 visibilitySh,
    out bool insideVolume,
    out bool weightedSample)
{
    visibilitySh = float4(
        3.54490770181103205460f,
        0.0f,
        0.0f,
        0.0f);
    insideVolume = false;
    weightedSample = false;
    ProbeLevelSettings level;
    if (levelIndex == 0u) {
        if (field == 0) level = NearLevel; else level = NearSunLevel;
    } else {
        if (field == 0) level = FarLevel; else level = FarSunLevel;
    }
    if (Response.z <= 0.5f ||
        any(level.ArrayDimensions.xyz == 0u)) {
        return false;
    }

    relativeWorldPosition += normal * level.CellSize.xyz * 0.5f;
    const float3 adjusted =
        relativeWorldPosition - level.PositionOffset.xyz;
    const float3 uvw = adjusted / level.ArraySize.xyz + 0.5f;
    if (any(uvw < 0.0f) || any(uvw > 1.0f)) {
        return false;
    }
    insideVolume = true;

    const int3 dimensions = int3(level.ArrayDimensions.xyz);
    const float3 cellCoordinates = uvw * float3(dimensions);
    const int3 cell000 = int3(floor(cellCoordinates - 0.5f));
    const float3 trilinearPosition =
        cellCoordinates - 0.5f - float3(cell000);
    float4 sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int x = 0; x < 2; ++x) {
        [unroll]
        for (int y = 0; y < 2; ++y) {
            [unroll]
            for (int z = 0; z < 2; ++z) {
                const int3 offset = int3(x, y, z);
                const int3 logicalCell = cell000 + offset;
                if (any(logicalCell < 0) || any(logicalCell >= dimensions)) {
                    continue;
                }
                const float3 axisWeights =
                    1.0f - abs(float3(offset) - trilinearPosition);
                const float trilinearWeight =
                    axisWeights.x * axisWeights.y * axisWeights.z;
                const float3 cellCentre =
                    (float3(logicalCell) + 0.5f -
                        float3(dimensions) * 0.5f) *
                        level.CellSize.xyz +
                    level.PositionOffset.xyz;
                const float3 towardCell = cellCentre - relativeWorldPosition;
                const float distanceSquared = dot(towardCell, towardCell);
                const float tangentWeight = distanceSquared > 1.0e-8f ?
                    dot(towardCell * rsqrt(distanceSquared), normal) *
                            0.5f +
                        0.5f :
                    1.0f;
                const float weight = trilinearWeight * tangentWeight;
                const int3 physicalCell = int3(
                    (uint3(logicalCell) + level.ArrayOrigin.xyz) %
                    level.ArrayDimensions.xyz);
                float4 sample;
                if (field == 0) {
                    if (levelIndex == 0) sample = NearSkylightingProbeArray.Load(int4(physicalCell, 0));
                    else sample = FarSkylightingProbeArray.Load(int4(physicalCell, 0));
                } else {
                    if (levelIndex == 0) sample = NearSkylightingSunHistory.Load(int4(physicalCell, 0));
                    else sample = FarSkylightingSunHistory.Load(int4(physicalCell, 0));
                    if (!isfinite(sample.w) || sample.w < 0) continue;
                }
                sum += sample * weight;
                weightSum += weight;
            }
        }
    }
    if (weightSum <= 1.0e-6f) {
        return false;
    }
    weightedSample = true;
    visibilitySh = sum / weightSum;
    return true;
}

bool SampleSkylighting(
    float3 relativeWorldPosition,
    float3 normal,
    out float4 visibilitySh,
    out float fade,
    out bool insideVolume,
    out bool weightedSample)
{
    const float4 unitVisibility = float4(
        3.54490770181103205460f,
        0.0f,
        0.0f,
        0.0f);
    visibilitySh = unitVisibility;
    fade = 0.0f;
    insideVolume = false;
    weightedSample = false;

    const float nearExtent = LevelExtent(
        relativeWorldPosition,
        NearLevel);
    float nearWeight = 1.0f - smoothstep(
        0.55f,
        0.90f,
        nearExtent);
    const float farExtent = LevelExtent(
        relativeWorldPosition,
        FarLevel);
    const float farFade = 1.0f - smoothstep(
        0.75f,
        0.98f,
        farExtent);

    float4 nearVisibility = unitVisibility;
    bool nearInside = false;
    bool nearWeighted = false;
    const bool nearSampled = nearWeight > 0.0f && SampleProbeField(
        0u, 0u,
        relativeWorldPosition,
        normal,
        nearVisibility,
        nearInside,
        nearWeighted);
    if (!nearSampled) {
        nearWeight = 0.0f;
    }

    float4 farVisibility = unitVisibility;
    bool farInside = false;
    bool farWeighted = false;
    const bool farSampled = nearWeight < 0.999f && farFade > 0.0f &&
        SampleProbeField(
            0u, 1u,
            relativeWorldPosition,
            normal,
            farVisibility,
            farInside,
            farWeighted);

    insideVolume = nearInside || farInside;
    weightedSample = nearWeighted || farWeighted;
    if (!nearSampled && !farSampled) {
        return false;
    }
    if (nearWeight >= 0.999f && nearSampled) {
        visibilitySh = nearVisibility;
        fade = 1.0f;
        return true;
    }

    visibilitySh = lerp(farVisibility, nearVisibility, nearWeight);
    fade = farFade;
    return true;
}


float SkylightingSunVisibility(float3 worldPosition, float3 normalWorld, float nativeVisibility)
{
    if (Response.z <= 0.5f) return nativeVisibility;
    const float3 position = worldPosition - ProbeWorldOrigin.xyz;
    float nearWeight = 1.0f - smoothstep(0.55f, 0.90f, LevelExtent(position, NearSunLevel));
    float farWeight = (1.0f - nearWeight) * (1.0f - smoothstep(0.75f, 0.98f, LevelExtent(position, FarSunLevel)));
    float4 nearValue = 0, farValue = 0;
    bool inside, weighted;
    if (nearWeight > 0 && !SampleProbeField(1u, 0u, position, normalWorld, nearValue, inside, weighted)) nearWeight = 0;
    if (farWeight > 0 && !SampleProbeField(1u, 1u, position, normalWorld, farValue, inside, weighted)) farWeight = 0;
    const float weight = nearWeight + farWeight;
    if (weight <= 1.0e-6f) return nativeVisibility;
    const float visibility = (nearValue.w * nearWeight + farValue.w * farWeight) / weight;
    return lerp(nativeVisibility, saturate(visibility), saturate(weight));
}

float SkylightingReflectionVisibility(float3 worldPosition, float3 normalWorld, float3 viewWorld, float roughness)
{
    if (Response.z <= 0.5f) return 1.0f;
    float4 visibility; float fade; bool inside, weighted;
    if (!SampleSkylighting(worldPosition - ProbeWorldOrigin.xyz, normalWorld, visibility, fade, inside, weighted)) return 1.0f;
    return lerp(Response.y, 1.0f, lerp(1.0f,
        saturate(dot(visibility, FauxSpecularLobe(normalWorld, viewWorld, saturate(roughness)))), fade));
}
#endif
