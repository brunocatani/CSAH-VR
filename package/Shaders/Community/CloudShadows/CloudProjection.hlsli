// GPL-3.0-only. Stable shell intersection adapted from FO4CloudShadows.
// CloudField contains a world-fixed origin, the previous origin, and its blend weight.
float3 CloudWorldVector(float3 v)
{
    return float3(dot(Camera[20].xyz,v), dot(Camera[21].xyz,v), dot(Camera[22].xyz,v));
}
float CloudAtOrigin(float3 receiver, float3 sun, float3 origin, float height, float radius)
{
    float3 p = receiver-origin;
    float shell = radius+height;
    float deficit = 2*radius*(height-p.z)+height*height-dot(p,p);
    if (deficit <= 0 || !isfinite(deficit)) return 0;
    float radial = deficit/(shell*shell);
    float projected = (radius*sun.z+dot(p,sun))/shell;
    float denominator = projected+sqrt(max(projected*projected+radial,0));
    if (denominator <= 1e-7 || !isfinite(denominator)) return 0;
    float3 direction = p+sun*(shell*radial/denominator);
    return saturate(CloudOcclusion.SampleLevel(CloudSampler,direction,0));
}
float CapturedCloudVisibility(float3 receiver, float3 viewLight, float strength, float height, float radius)
{
    float3 sun = normalize(CloudWorldVector(viewLight));
    if (CloudField[0].w < 0.5 || sun.z <= 0 || !all(isfinite(sun)) || !all(isfinite(receiver))) return 1;
    float coverage = CloudAtOrigin(receiver,sun,CloudField[0].xyz,height,radius);
    if (CloudField[2].x > 0)
        coverage = lerp(coverage,CloudAtOrigin(receiver,sun,CloudField[1].xyz,height,radius),CloudField[2].x);
    return saturate(1-coverage*saturate(strength));
}
