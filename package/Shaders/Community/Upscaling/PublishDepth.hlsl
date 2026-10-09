// Late native draws keep the game's projection and depth encoding. Publish
// the same samples over the display-sized eye regions after DLSS publishes
// color. In particular, do not convert reserved first-person depth to sky.
Texture2D<float> LeftNativeDepth : register(t0);
Texture2D<float> RightNativeDepth : register(t1);
cbuffer PublicationExtent : register(b0)
{
    uint2 OutputEyeExtent;
    uint2 InputEyeExtent;
};

float4 mainVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float mainPS(float4 position : SV_Position) : SV_Depth
{
    const uint eye = uint(position.x) / OutputEyeExtent.x;
    const uint2 local = uint2(position.xy) - uint2(eye * OutputEyeExtent.x, 0);
    const uint2 source = min(uint2((float2(local) + 0.5f) *
        float2(InputEyeExtent) / float2(OutputEyeExtent)), InputEyeExtent - 1);
    // Point sampling preserves foreground/background boundaries and the
    // native depth-range split; interpolating those ranges invents surfaces.
    return eye == 0 ? LeftNativeDepth.Load(int3(source, 0)) : RightNativeDepth.Load(int3(source, 0));
}
