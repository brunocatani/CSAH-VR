Texture2D<float2> ContactShadowMask : register(t46);

cbuffer NativeDFLight : register(b2) { float4 DFLight[46]; };
cbuffer ContactShadowSettings : register(b13)
{
    float4 ContactParams0;
    float4 ContactParams1;
    float4 ContactParams2;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    nointerpolation uint Eye : EYEINDEX;
    float4 NormalCarrier : TEXCOORD0;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    const float enabled = saturate(ContactParams2.y);
    // The resolved pair retains contact and cloud visibility independently.
    // Clouds affect both sides; contact darkens direct light only on its
    // facing side, while wrap/transmission receives contact visibility once.
    const float2 rawVisibility = enabled > 0.0f ? saturate(
        ContactShadowMask.Load(int3(int2(input.Position.xy), 0))) : 1.0f.xx;
    const float2 visibility = lerp(1.0f.xx, rawVisibility, enabled);
    const float facing = step(0.0f, dot(input.NormalCarrier.xyz, DFLight[input.Eye + 1u].xyz));
    const float direct = visibility.y * lerp(1.0f, visibility.x, facing);
    const float transmission = visibility.y * visibility.x;
    return float4(direct, transmission, direct, direct);
}
