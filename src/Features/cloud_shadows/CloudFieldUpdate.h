#pragma once
namespace csah::cloud_shadows {
inline constexpr char kCloudFieldUpdate[] = R"(
cbuffer Parameters : register(b0) { float4 Params; };
cbuffer NativeCamera : register(b12) { float4 Camera[61]; };
RWBuffer<float4> Field : register(u0);
[numthreads(1,1,1)] void main() {
    float3 center = (Camera[59].xyz + Camera[60].xyz) * 0.5;
    if (!all(isfinite(center))) { Field[0] = Field[1] = Field[2] = 0; return; }
    if (Params.x > 0.5 || Field[0].w < 0.5) {
        Field[0] = float4(center,1); Field[1] = float4(center,Params.y-Params.w);
    } else {
        float3 delta = center-Field[0].xyz;
        // FO4CloudShadows reanchors after 1.5 cloud heights horizontally or
        // half a height vertically, crossfading for four seconds.
        if (Params.y-Field[1].w >= Params.w &&
            (dot(delta.xy,delta.xy) > 2.25*Params.z*Params.z || abs(delta.z)>0.5*Params.z)) {
            Field[1] = float4(Field[0].xyz,Params.y); Field[0] = float4(center,1);
        }
    }
    Field[2] = float4(1-smoothstep(0,Params.w,Params.y-Field[1].w),0,0,0);
}
)";
}
