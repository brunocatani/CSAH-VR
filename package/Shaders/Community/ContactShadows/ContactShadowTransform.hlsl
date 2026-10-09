Texture2D<float2> ContactShadowMask : register(t46);

cbuffer ContactShadowSettings : register(b13)
{
    float4 ContactParams0;
    float4 ContactParams1;
    float4 ContactParams2;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
};

float4 PSMain(PixelInput input) : SV_Target0
{
    const float enabled = saturate(ContactParams2.y);
    // Resolved direct/transmission visibility already includes cloud shading
    // and the receiver-facing selection. Neither lobe is multiplied twice.
    const float2 rawVisibility = enabled > 0.0f ? saturate(
        ContactShadowMask.Load(int3(int2(input.Position.xy), 0))) : 1.0f.xx;
    const float2 visibility = lerp(1.0f.xx, rawVisibility, enabled);
    return float4(visibility.x, visibility.y, visibility.x, visibility.x);
}
