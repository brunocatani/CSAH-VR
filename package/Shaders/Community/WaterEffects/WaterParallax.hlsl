// Adapted from community-shaders-shared WaterEffects/WaterParallax.hlsli.
// The native VR vertex shader supplies world-scaled coordinates through its
// existing b1/b2 buffers. No extra vertex output or stereo semantic is needed.
Texture2D<float4> Normal0 : register(t4);
Texture2D<float4> Normal1 : register(t5);
Texture2D<float4> Normal2 : register(t6);
SamplerState Sampler0 : register(s4);
SamplerState Sampler1 : register(s5);
SamplerState Sampler2 : register(s6);
Buffer<float4> WaterEffects : register(t66);
Buffer<float4> WaterVertexMaterial : register(t67);
Buffer<float4> WaterVertexGeometry : register(t68);
cbuffer NativeWaterMaterial : register(b1) {float4 NativeMaterial[10];};
struct Input {float4 eyeVector:TEXCOORD0;float4 uv01:TEXCOORD1;float4 uv2:TEXCOORD2;};
struct Output {float4 normal0:SV_Target0;float4 normal1:SV_Target1;float4 normal2:SV_Target2;float4 active:SV_Target3;};
float Mip(Texture2D<float4> tex,float2 dx,float2 dy)
{
    uint w,h;tex.GetDimensions(w,h);float2 d=float2(w,h);
    return max(0,.5*log2(max(max(dot(dx*d,dx*d),dot(dy*d,dy*d)),1e-12)));
}
float Mean(Texture2D<float4> tex,SamplerState sampleState)
{
    uint w,h,mips;tex.GetDimensions(0,w,h,mips);
    return 1-tex.SampleLevel(sampleState,0,mips-1).a;
}
float Height(Input input,float2 offset,float3 inverseScale,float3 mip,float3 amplitudes)
{
    return dot(1-float3(Normal0.SampleLevel(Sampler0,input.uv01.xy+offset*inverseScale.x,mip.x).a,
        Normal1.SampleLevel(Sampler1,input.uv01.zw+offset*inverseScale.y,mip.y).a,
        Normal2.SampleLevel(Sampler2,input.uv2.xy+offset*inverseScale.z,mip.z).a),amplitudes);
}
Output PSMain(Input input)
{
    Output output=(Output)0;
    float2 dx0=ddx(input.uv01.xy),dy0=ddy(input.uv01.xy);
    float2 dx1=ddx(input.uv01.zw),dy1=ddy(input.uv01.zw);
    float2 dx2=ddx(input.uv2.xy),dy2=ddy(input.uv2.xy);
    uint settingsCount,materialCount,geometryCount;
    WaterEffects.GetDimensions(settingsCount);WaterVertexMaterial.GetDimensions(materialCount);WaterVertexGeometry.GetDimensions(geometryCount);
    [branch] if(settingsCount>0 && materialCount>=3 && geometryCount>=15)
    {
        float3 scale=WaterVertexMaterial[2].xyz;
        float3 direction=input.eyeVector.xyz*rsqrt(max(dot(input.eyeVector.xyz,input.eyeVector.xyz),1e-12));
        [branch] if(WaterEffects[0].x>.5 && WaterVertexGeometry[14].x==0 && all(abs(scale)>1e-4) && all(isfinite(scale)) && direction.z<-.05)
        {
            float3 inverseScale=rcp(scale),amplitudes=max(0,NativeMaterial[9].xyz);
            float maximumHeight=dot(amplitudes,1);
            float3 mip=float3(Mip(Normal0,dx0,dy0),Mip(Normal1,dx1,dy1),Mip(Normal2,dx2,dy2));
            float mean=dot(float3(Mean(Normal0,Sampler0),Mean(Normal1,Sampler1),Mean(Normal2,Sampler2)),amplitudes);
            float2 ray=direction.xy/-direction.z*(20*WaterEffects[0].y);
            float current=0,previous=0;
            float height=Height(input,-mean*ray,inverseScale,mip,amplitudes),previousHeight=height;
            float stepSize=maximumHeight/16;
            // Unlike the upstream while loop, all authored amplitudes have a
            // finite 16-step budget, with guarded interpolation at flat heights.
            [loop] for(uint step=0;step<16 && current<height;step++)
            {
                previous=current;previousHeight=height;current+=stepSize;
                height=Height(input,(current-mean)*ray,inverseScale,mip,amplitudes);
            }
            float a=previous-previousHeight,b=current-height,denominator=a-b;
            float intersection=abs(denominator)>1e-6?(current*a-previous*b)/denominator:current;
            float2 offset=ray*(clamp(intersection,0,maximumHeight)-mean);
            output.normal0=Normal0.SampleGrad(Sampler0,input.uv01.xy+offset*inverseScale.x,dx0,dy0);
            output.normal1=Normal1.SampleGrad(Sampler1,input.uv01.zw+offset*inverseScale.y,dx1,dy1);
            output.normal2=Normal2.SampleGrad(Sampler2,input.uv2.xy+offset*inverseScale.z,dx2,dy2);
            output.active=1;
        }
    }
    return output;
}
