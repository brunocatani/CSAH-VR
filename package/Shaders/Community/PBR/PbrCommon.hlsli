#ifndef FO4VR_COMMUNITY_SHADERS_PBR_COMMON_HLSLI
#define FO4VR_COMMUNITY_SHADERS_PBR_COMMON_HLSLI

static const float PbrPi = 3.14159265358979323846f;
static const float PbrEpsilon = 1.0e-5f;
static const float PbrMinimumRoughness = 0.04f;

float3 PbrSafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > PbrEpsilon ?
        value * rsqrt(lengthSquared) : fallback;
}

float3 PbrFresnelSchlick(float3 f0, float viewDotHalf)
{
    const float oneMinus = 1.0f - saturate(viewDotHalf);
    const float oneMinus2 = oneMinus * oneMinus;
    return f0 + (1.0f - f0) * oneMinus2 * oneMinus2 * oneMinus;
}

float PbrDistributionGgx(float roughness, float normalDotHalf)
{
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float normalDotHalf2 = normalDotHalf * normalDotHalf;
    const float denominator = normalDotHalf2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(PbrPi * denominator * denominator, PbrEpsilon);
}

float PbrVisibilitySmithJointApprox(
    float roughness,
    float normalDotView,
    float normalDotLight)
{
    const float alpha = roughness * roughness;
    const float visibilityView = normalDotLight *
        (normalDotView * (1.0f - alpha) + alpha);
    const float visibilityLight = normalDotView *
        (normalDotLight * (1.0f - alpha) + alpha);
    return 0.5f / max(
        visibilityView + visibilityLight,
        PbrEpsilon);
}

float2 PbrEnvironmentBrdf(float roughness, float normalDotView)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(
        r.x * r.x,
        exp2(-9.28f * saturate(normalDotView))) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

float PbrSpecularOcclusion(
    float normalDotView,
    float roughness,
    float ambientOcclusion)
{
    const float alpha = roughness * roughness;
    return saturate(
        pow(abs(normalDotView + ambientOcclusion), alpha) -
        1.0f + ambientOcclusion);
}

float PbrDecodeMetalness(float encodedMaterialTag)
{
    return step(0.5f, encodedMaterialTag) * saturate(
        (1.0f - encodedMaterialTag) * 2.0f);
}

float PbrRoughnessFromPhong(float encodedShininess)
{
    const float exponent = exp2(
        saturate(encodedShininess) * 10.0f + 1.0f);
    return clamp(
        sqrt(sqrt(2.0f / (exponent + 2.0f))),
        PbrMinimumRoughness,
        1.0f);
}

float PbrMaterialRoughness(
    float environmentLod,
    float encodedShininess,
    float encodedGlossiness,
    float roughnessMultiplier,
    float specularRoughnessBlend)
{
    const float lodRoughness = saturate(environmentLod / 7.0f);
    const float converted = PbrRoughnessFromPhong(encodedShininess);
    const float conversionWeight = saturate(
        specularRoughnessBlend *
        (1.0f - saturate(encodedGlossiness)));
    return clamp(
        lerp(converted, lodRoughness, conversionWeight) *
            max(roughnessMultiplier, 0.0f),
        PbrMinimumRoughness,
        1.0f);
}

float PbrDielectricF0(
    float encodedSpecular,
    float baseF0Multiplier,
    float minimumF0,
    float cubemapToF0Multiplier)
{
    return saturate(max(
        minimumF0,
        saturate(encodedSpecular) * baseF0Multiplier *
            cubemapToF0Multiplier));
}

#endif
