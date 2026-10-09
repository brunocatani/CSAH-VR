// Native geometry, clipping and read-only depth/stencil still own coverage.
// Lighting is evaluated only at the cubemap taps by CaptureRadianceCS.
float4 main() : SV_Target0
{
    return 1.0f;
}
