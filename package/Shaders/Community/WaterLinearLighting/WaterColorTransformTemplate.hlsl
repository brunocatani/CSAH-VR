// This shader is an offline DXBC instruction template. It is never loaded by
// the game. The contract generator compiles it and transplants its verified
// verified colour-domain transforms into the exact FO4VR Water pixel shaders.

cbuffer WaterPerFrame : register(b0)
{
    float4 WaterFramePad0 : packoffset(c0);
    float4 WaterFramePad1 : packoffset(c1);
    float4 SunColor : packoffset(c2);
};

cbuffer WaterPerMaterial : register(b1)
{
    float4 ShallowColor : packoffset(c0);
    float4 DeepColor : packoffset(c1);
    float4 WaterMaterialPad0 : packoffset(c2);
    float4 WaterMaterialPad1 : packoffset(c3);
    float4 WaterMaterialPad2 : packoffset(c4);
    float4 WaterMaterialPad3 : packoffset(c5);
    float4 FogNearColor : packoffset(c6);
    float4 FogFarColor : packoffset(c7);
};

cbuffer WaterPerLights : register(b2)
{
    float4 PointLightColor : packoffset(c20);
};

// Offline placeholder for the scalar atmospheric-fog coverage recovered from
// each qualified FO4VR Water shader. The contract generator replaces every
// read with that shader's original temporary operand, so b4 is never declared
// by a packaged replacement.
cbuffer WaterFogAlphaSource : register(b4)
{
    float FogAlphaSource : packoffset(c0.x);
};

cbuffer LinearLightingFrame : register(b5)
{
    uint EnableLinearLighting : packoffset(c0.x);
    float2 LinearLightingFramePad0 : packoffset(c0.y);
    float LightGamma : packoffset(c0.w);
    float4 LinearLightingFramePad1 : packoffset(c1);
    float FogGamma : packoffset(c2.x);
    float FogAlphaGamma : packoffset(c2.y);
    float2 LinearLightingFramePad2 : packoffset(c2.z);
    float LinearLightingFramePad3 : packoffset(c3.x);
    float WaterGamma : packoffset(c3.y);
    float2 LinearLightingFramePad4 : packoffset(c3.z);
    float DirectionalLightMultiplier : packoffset(c4.x);
    float PointLightMultiplier : packoffset(c4.y);
    float2 LinearLightingFramePad5 : packoffset(c4.z);
};

static const float NativeProducerGamma = 2.2f;

struct TransformOutput
{
    float4 shallow : SV_Target0;
    float4 deep : SV_Target1;
};

TransformOutput PSMain()
{
    TransformOutput output;
    output.shallow = ShallowColor;
    output.deep = DeepColor;
    if (EnableLinearLighting != 0u) {
        output.shallow.xyz = pow(abs(output.shallow.xyz), WaterGamma / NativeProducerGamma);
        output.deep.xyz = pow(abs(output.deep.xyz), WaterGamma / NativeProducerGamma);
    }
    return output;
}

float4 PSSunMain() : SV_Target0
{
    float4 output = SunColor;
    if (EnableLinearLighting != 0u) {
        output.xyz = pow(
            abs(output.xyz),
            LightGamma / NativeProducerGamma) *
            DirectionalLightMultiplier;
    }
    return output;
}

TransformOutput PSFogMain()
{
    TransformOutput output;
    output.shallow = FogNearColor;
    output.deep = FogFarColor;
    if (EnableLinearLighting != 0u) {
        output.shallow.xyz = pow(abs(output.shallow.xyz), FogGamma / NativeProducerGamma);
        output.deep.xyz = pow(abs(output.deep.xyz), FogGamma / NativeProducerGamma);
    }
    return output;
}

float4 PSPointMain() : SV_Target0
{
    float4 output = PointLightColor;
    if (EnableLinearLighting != 0u) {
        output.xyz = pow(
            abs(output.xyz),
            LightGamma / NativeProducerGamma) *
            PointLightMultiplier;
    }
    return output;
}

float4 PSFogAlphaMain() : SV_Target0
{
    float output = FogAlphaSource;
    if (EnableLinearLighting != 0u) {
        output = pow(abs(output), FogAlphaGamma / NativeProducerGamma);
    }
    return output.xxxx;
}
