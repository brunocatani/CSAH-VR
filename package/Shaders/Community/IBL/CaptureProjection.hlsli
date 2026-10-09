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
