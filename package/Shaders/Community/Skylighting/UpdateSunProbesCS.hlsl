// The directional draw owns these native b2/b12 values and t5/s5 shadow
// bindings. Evaluate while that draw is current; do not retain engine data.
cbuffer NativeSun : register(b0) { float4 Sun[46]; };
cbuffer NativeWorldCamera : register(b12) { float4 WorldCamera[61]; };
#include "SkylightingSampling.hlsli"
Texture2DArray<float> SunDepth : register(t0);
SamplerComparisonState SunComparison : register(s0);
// xyz retain the world light direction; w is visibility, -1 means unobserved.
RWTexture3D<float4> SunHistory : register(u0);

bool SampleSunCascade(float3 viewPosition, uint cascade, uint slice, bool bias, out float visibility)
{
    const uint row = 13 + cascade * 3;
    const float4 position = float4(viewPosition, 1);
    const float3 projected = float3(dot(Sun[row], position), dot(Sun[row + 1], position), dot(Sun[row + 2], position));
    visibility = 1;
    uint width, height, slices; SunDepth.GetDimensions(width, height, slices);
    if (slice >= slices || !all(isfinite(projected)) || any(projected < 0) || any(projected > 1)) return false;
    float depth = projected.z;
    if (bias) {
        const float range = Sun[38 + cascade].w - Sun[38 + cascade].z;
        if (!isfinite(range) || range <= 0) return false;
        depth -= 0.275f / range;
    }
    visibility = SunDepth.SampleCmpLevelZero(SunComparison, float3(projected.xy, slice), depth);
    return true;
}

bool SampleSun(float3 viewPosition, out float visibility)
{
    visibility = 1;
    if (UpdateControl.w == 1) {
        if (!(Sun[11].y >= 0 && Sun[11].y < 256)) return false;
        if (!SampleSunCascade(viewPosition, 0, (uint)Sun[11].y, false, visibility)) return false;
    } else {
        const float4 position = float4(viewPosition, 1);
        const float clipW = dot(WorldCamera[7], position);
        const float depth = dot(WorldCamera[6], position) / clipW;
        if (!isfinite(depth) || !(clipW > 0)) return false;
        const bool first = depth < Sun[12].x;
        const bool second = depth > Sun[11].x && depth < Sun[12].y;
        float a = 1, b = 1;
        const bool validA = first && SampleSunCascade(viewPosition, 0, 0, true, a);
        const bool validB = second && SampleSunCascade(viewPosition, 1, 1, true, b);
        if (!validA && !validB) return false;
        if (first) {
            if (!validA) return false;
            const float blend = smoothstep(Sun[11].x, Sun[12].x, depth);
            visibility = lerp(a, validB ? b : 1, blend);
        } else {
            if (!validB) return false;
            visibility = lerp(b, 1, smoothstep(Sun[11].y, Sun[12].y, depth));
        }
    }
    const float distanceRange = Sun[42].x;
    if (!isfinite(distanceRange) || distanceRange <= 0) return false;
    const float distanceWeight = saturate(dot(viewPosition, viewPosition) / distanceRange);
    // Native DFLight squares the distance fraction three times (u^8).
    const float squared = distanceWeight * distanceWeight;
    const float fourth = squared * squared;
    visibility = lerp(visibility, 1, fourth * fourth);
    return true;
}

[numthreads(8,8,4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    ProbeLevelSettings level;
    if (UpdateControl.x == 0) level = NearSunLevel; else level = FarSunLevel;
    if (any(id >= level.ArrayDimensions.xyz)) return;
    const int3 logical = int3((id + level.ArrayDimensions.xyz - level.ArrayOrigin.xyz) % level.ArrayDimensions.xyz);
    const bool retained = all(logical >= max(level.ValidMargin.xyz, 0)) &&
        all(logical <= int3(level.ArrayDimensions.xyz) - 1 + min(level.ValidMargin.xyz, 0));
    const float3 localPosition = (float3(logical) + 0.5f - float3(level.ArrayDimensions.xyz) * 0.5f) *
        level.CellSize.xyz + level.PositionOffset.xyz;
    const float3 worldPosition = ProbeWorldOrigin.xyz + localPosition;
    const float3 relativeEye = worldPosition - WorldCamera[59].xyz;
    const float3 viewPosition = float3(dot(WorldCamera[0].xyz, relativeEye),
        dot(WorldCamera[1].xyz, relativeEye), dot(WorldCamera[2].xyz, relativeEye));
    float visibility;
    if (Response.z <= 0.5f || !SampleSun(viewPosition, visibility)) {
        SunHistory[id] = float4(0,0,0,-1);
        return;
    }
    const float3 light = normalize(float3(dot(WorldCamera[20].xyz, Sun[1].xyz),
        dot(WorldCamera[21].xyz, Sun[1].xyz), dot(WorldCamera[22].xyz, Sun[1].xyz)));
    if (!all(isfinite(light))) { SunHistory[id] = float4(0,0,0,-1); return; }
    const float4 previous = SunHistory[id];
    // A rolling-cell replacement or a changed light direction starts with a
    // current observation. Stable cells smooth the low-frequency soft lobe.
    const bool history = retained && previous.w >= 0 && previous.w <= 1 && dot(previous.xyz, light) > 0.98f;
    SunHistory[id] = float4(light, history ? lerp(previous.w, visibility, 1.0f / 32.0f) : visibility);
}
