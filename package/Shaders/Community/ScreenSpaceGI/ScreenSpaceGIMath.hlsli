#ifndef CSAH_SCREEN_SPACE_GI_MATH
#define CSAH_SCREEN_SPACE_GI_MATH
// Resource-free SSGI helpers shared by the compute passes and the pixel
// consumers, which declare their own native camera and stereo buffers.
static const float Pi=3.14159265358979323846f;
float3 SafeNormal(float3 v) {return v*rsqrt(max(dot(v,v),1e-12));}
// Upstream Color::RGBToYCoCg / YCoCgToRGB.
float3 RGBToYCoCg(float3 c) {return float3(dot(c,float3(.25,.5,.25)),dot(c,float3(.5,0,-.5)),dot(c,float3(-.25,.5,-.25)));}
float3 YCoCgToRGB(float3 c) {return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
// Upstream SphericalHarmonics (sh2 = float4 of L0, L1 m=-1, 0, 1).
float4 SHEvaluate(float3 d) {return float4(.28209479177387814,-.48860251190291992*d.y,.48860251190291992*d.z,-.48860251190291992*d.x);}
float4 SHCosineLobe(float3 d) {return float4(.88622692545275801,-1.0233267079464885*d.y,1.0233267079464885*d.z,-1.0233267079464885*d.x);}
// Zonal-harmonic hallucination for diffuse irradiance (torust.me/ZH3.pdf).
float SHHallucinateZH3Irradiance(float4 sh,float3 direction)
{
    float3 axis=SafeNormal(float3(sh.w,sh.y,sh.z));
    float ratio=abs(dot(float3(-sh.w,-sh.y,sh.z),axis))/max(sh.x,1e-6);
    float zonal=sh.x*(.08*ratio+.6*ratio*ratio);
    float fz=dot(axis,direction);
    float zh=sqrt(5.0/(16.0*Pi))*(3*fz*fz-1);
    return max(0,dot(sh,SHCosineLobe(direction))+.25*zonal*zh);
}
float SpecularLobeHalfAngle(float roughness)
{
    float r2=roughness*roughness;
    return clamp(4.1679*r2*r2-9.0127*r2*roughness+4.6161*r2+1.7048*roughness+.1,0,Pi*.5);
}
float3 SpecularDominantDirection(float3 n,float3 v,float roughness)
{
    float f=(1-roughness)*(sqrt(1-roughness)+roughness);
    return SafeNormal(lerp(n,reflect(-v,n),f));
}
float4 SHFauxSpecularLobe(float3 n,float3 v,float roughness)
{
    float3 dominant=SpecularDominantDirection(n,v,roughness);
    float blend=SpecularLobeHalfAngle(roughness)/(Pi*.5);
    return lerp(SHCosineLobe(dominant)/Pi,SHEvaluate(dominant),blend);
}
#endif
