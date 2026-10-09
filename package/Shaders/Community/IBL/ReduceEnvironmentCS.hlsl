// Validate every mip-zero texel and accumulate the same first-order weighted
// least-squares system as the CPU reference. Two bounded tree reductions leave
// just ten float4 rows per face for the nonblocking publication readback.
Texture2DArray<float3> Radiance : register(t0);
Texture2DArray<float> Validity : register(t1);
StructuredBuffer<float4> Partials : register(t2);
RWStructuredBuffer<float4> Output : register(u0);

cbuffer ReductionConstants : register(b11)
{
    uint Extent;
    uint GroupsPerAxis;
    uint ReducePartials;
    uint FitDiffuse;
};

groupshared float4 Values[10][64];

float3 CubeDirection(uint face, float2 uv)
{
    switch (face) {
    case 0: return normalize(float3(1, -uv.y, -uv.x));
    case 1: return normalize(float3(-1, -uv.y, uv.x));
    case 2: return normalize(float3(uv.x, 1, uv.y));
    case 3: return normalize(float3(uv.x, -1, -uv.y));
    case 4: return normalize(float3(uv.x, -uv.y, 1));
    default: return normalize(float3(-uv.x, -uv.y, -1));
    }
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID,
    uint lane : SV_GroupIndex)
{
    [unroll] for (uint row = 0; row < 10; ++row) Values[row][lane] = 0;
    if (ReducePartials != 0) {
        const uint groupCount = GroupsPerAxis * GroupsPerAxis;
        for (uint tile = lane; tile < groupCount; tile += 64) {
            const uint start = (group.z * groupCount + tile) * 10;
            [unroll] for (uint r = 0; r < 10; ++r) {
                const float4 value = Partials[start + r];
                if (r == 1) {
                    Values[r][lane].xyz += value.xyz;
                    Values[r][lane].w = max(Values[r][lane].w, value.w);
                } else Values[r][lane] += value;
            }
        }
    } else if (id.x < Extent && id.y < Extent) {
        const float3 color = Radiance.Load(int4(id, 0));
        const float validity = Validity.Load(int4(id, 0));
        const bool valid = all(isfinite(color)) && all(color >= 0) &&
            isfinite(validity) && validity >= 0 && validity <= 1.0001f;
        Values[2][lane].w = 1;
        if (!valid) Values[2][lane].z = 1;
        else {
            const float2 uv = ((float2(id.xy) + 0.5f) / Extent) * 2 - 1;
            const float denominator = 1 + dot(uv, uv);
            const float solidAngle = rcp(denominator * sqrt(denominator));
            const float peak = max(color.r, max(color.g, color.b));
            Values[0][lane] = float4(color, validity);
            Values[1][lane] = float4(dot(color, float3(0.2126f, 0.7152f, 0.0722f)),
                validity * solidAngle, solidAngle, peak);
            Values[2][lane].xy = float2(peak > 1e-5f, validity > 1e-4f);
            if (FitDiffuse != 0) {
                const float3 direction = CubeDirection(id.z, uv);
                const float4 basis = float4(0.28209479177387814347f,
                    0.48860251190291992159f * float3(-direction.y, direction.z, -direction.x));
                const float4 weightedBasis = saturate(validity) * solidAngle * basis;
                [unroll] for (uint k = 0; k < 4; ++k)
                    Values[3 + k][lane] = weightedBasis * basis[k];
                Values[7][lane] = weightedBasis * color.r;
                Values[8][lane] = weightedBasis * color.g;
                Values[9][lane] = weightedBasis * color.b;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 32; stride > 0; stride >>= 1) {
        if (lane < stride) {
            [unroll] for (uint r = 0; r < 10; ++r) {
                if (r == 1) {
                    Values[r][lane].xyz += Values[r][lane + stride].xyz;
                    Values[r][lane].w = max(Values[r][lane].w, Values[r][lane + stride].w);
                } else Values[r][lane] += Values[r][lane + stride];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) {
        const uint index = ReducePartials != 0 ? group.z :
            (group.z * GroupsPerAxis + group.y) * GroupsPerAxis + group.x;
        [unroll] for (uint r = 0; r < 10; ++r) Output[index * 10 + r] = Values[r][0];
    }
}
