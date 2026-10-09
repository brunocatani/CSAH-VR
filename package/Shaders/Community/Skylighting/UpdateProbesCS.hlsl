Texture2D<float> OcclusionDepth : register(t0);
RWTexture3D<float4> ProbeArray : register(u0);
RWTexture3D<uint> AccumulationFrames : register(u1);
RWByteAddressBuffer DiagnosticStats : register(u2);
SamplerComparisonState OcclusionSampler : register(s0);

struct ProbeLevelSettings
{
    float4 ArraySize;
    float4 CellSize;
    float4 PositionOffset;
    uint4 ArrayDimensions;
    uint4 ArrayOrigin;
    int4 ValidMargin;
};

cbuffer SkylightingSettings : register(b13)
{
    column_major float4x4 OcclusionViewProjection;
    float4 OcclusionDirection;
    ProbeLevelSettings NearLevel;
    ProbeLevelSettings FarLevel;
    // x=level (0 near, 1 far), y=first Z slice, z=slice count.
    uint4 UpdateControl;
    // x=minimum diffuse visibility, y=minimum specular visibility,
    // z=feature active, w=one-shot diagnostic collection.
    float4 Response;
    float4 ProbeWorldOrigin;
};

static const float kPi = 3.14159265358979323846f;
static const float4 kUnitVisibilitySh =
    float4(3.54490770181103205460f, 0.0f, 0.0f, 0.0f);

float4 EvaluateSh(float3 direction)
{
    return float4(
        0.28209479177387814347f,
        -0.48860251190291992159f * direction.y,
        0.48860251190291992159f * direction.z,
        -0.48860251190291992159f * direction.x);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    ProbeLevelSettings level;
    if (UpdateControl.x == 0u) {
        level = NearLevel;
    } else {
        level = FarLevel;
    }
    const uint3 dimensions = level.ArrayDimensions.xyz;
    const uint probeSlice = UpdateControl.y + dispatchThread.z;
    if (dispatchThread.x >= dimensions.x ||
        dispatchThread.y >= dimensions.y ||
        dispatchThread.z >= UpdateControl.z ||
        probeSlice >= dimensions.z) {
        return;
    }
    const uint3 probeTexel = uint3(
        dispatchThread.xy,
        probeSlice);

    const int3 logicalCell = int3(
        (probeTexel + dimensions - level.ArrayOrigin.xyz) % dimensions);
    const int3 validMinimum = max(level.ValidMargin.xyz, 0);
    const int3 validMaximum =
        int3(dimensions) - 1 + min(level.ValidMargin.xyz, 0);
    const bool previouslyValid =
        all(logicalCell >= validMinimum) &&
        all(logicalCell <= validMaximum);

    const float3 cellCentre =
        (float3(logicalCell) + 0.5f - float3(dimensions) * 0.5f) *
            level.CellSize.xyz +
        level.PositionOffset.xyz;
    const float4 projected = mul(
        OcclusionViewProjection,
        float4(cellCentre, 1.0f));
    const float reciprocalW = rcp(max(abs(projected.w), 1.0e-7f));
    const float3 occlusionPosition = projected.xyz * reciprocalW;
    const float2 occlusionUv =
        occlusionPosition.xy * float2(0.5f, -0.5f) + 0.5f;
    const bool diagnosticSample = Response.w > 0.5f &&
        UpdateControl.x == 0u &&
        all((probeTexel % uint3(16u, 16u, 8u)) == 0u);
    uint ignored;
    if (diagnosticSample) {
        DiagnosticStats.InterlockedAdd(0u, 1u, ignored);
        if (all(abs(occlusionPosition) < 1.0e20f)) {
            DiagnosticStats.InterlockedAdd(4u, 1u, ignored);
        }
    }

    if (Response.z > 0.5f &&
        all(occlusionUv > 0.0f) && all(occlusionUv < 1.0f)) {
        if (diagnosticSample) {
            DiagnosticStats.InterlockedAdd(8u, 1u, ignored);
            if (occlusionPosition.z >= 0.0f &&
                occlusionPosition.z <= 1.0f) {
                DiagnosticStats.InterlockedAdd(12u, 1u, ignored);
            }
            uint depthWidth;
            uint depthHeight;
            OcclusionDepth.GetDimensions(depthWidth, depthHeight);
            const uint2 depthPixel = min(
                uint2(occlusionUv * float2(depthWidth, depthHeight)),
                uint2(depthWidth - 1u, depthHeight - 1u));
            const float sampledDepth = OcclusionDepth.Load(
                int3(depthPixel, 0));
            if (sampledDepth < 1.0f - 1.0e-5f) {
                DiagnosticStats.InterlockedAdd(16u, 1u, ignored);
            }
            DiagnosticStats.InterlockedMin(
                32u,
                asuint(sampledDepth),
                ignored);
            DiagnosticStats.InterlockedMax(
                36u,
                asuint(sampledDepth),
                ignored);
        }
        const uint previousFrames = previouslyValid ?
            AccumulationFrames[probeTexel] : 0u;
        const uint accumulation = min(previousFrames + 1u, 255u);
        const float visibility = OcclusionDepth.SampleCmpLevelZero(
            OcclusionSampler,
            occlusionUv,
            occlusionPosition.z);
        if (diagnosticSample && visibility < 0.9999f) {
            DiagnosticStats.InterlockedAdd(20u, 1u, ignored);
        }
        float4 sampledSh = EvaluateSh(
            normalize(OcclusionDirection.xyz)) *
            (visibility * 4.0f * kPi);

        static const float kConfidenceFrames = 15.0f;
        if (previousFrames != 0u) {
            const float previousConfidence =
                min(kConfidenceFrames, (float)previousFrames) /
                kConfidenceFrames;
            const float4 previous = ProbeArray[probeTexel];
            const float4 unbiasedPrevious = kUnitVisibilitySh +
                (previous - kUnitVisibilitySh) /
                    max(previousConfidence, 1.0f / kConfidenceFrames);
            sampledSh = lerp(
                unbiasedPrevious,
                sampledSh,
                rcp((float)accumulation));
        }
        const float confidence =
            min(kConfidenceFrames, (float)accumulation) /
            kConfidenceFrames;
        const float4 updatedProbe = lerp(
            kUnitVisibilitySh,
            sampledSh,
            confidence);
        ProbeArray[probeTexel] = updatedProbe;
        AccumulationFrames[probeTexel] = accumulation;
        if (diagnosticSample) {
            DiagnosticStats.InterlockedAdd(24u, 1u, ignored);
            const float4 difference = abs(updatedProbe - kUnitVisibilitySh);
            if (max(max(difference.x, difference.y),
                    max(difference.z, difference.w)) > 1.0e-3f) {
                DiagnosticStats.InterlockedAdd(28u, 1u, ignored);
            }
        }
    } else if (!previouslyValid) {
        ProbeArray[probeTexel] = kUnitVisibilitySh;
        AccumulationFrames[probeTexel] = 0u;
    }
}
