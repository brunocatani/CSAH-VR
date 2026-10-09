Buffer<float4> InverseSquareParameters : register(t78);
struct Output {float attenuation:SV_Target0;float cutoff:SV_Target1;};
Output main(float3 input:TEXCOORD0)
{
    uint rows;InverseSquareParameters.GetDimensions(rows);
    Output result;result.attenuation=input.z;result.cutoff=.001;
    if(rows>0) {
        float4 parameters=InverseSquareParameters[0];
        if(parameters.x>.5 && input.y>0 && input.x>=0 && all(isfinite(input.xy))) {
            // Community shaders' finite area-source model, in FO4 game units.
            // Native light radius, culling, shadows and gameplay are unchanged.
            float attenuation=3920/(input.x*input.x+1960*parameters.y*parameters.y);
            float fade=saturate((input.y-input.x)/min(252,input.y));
            fade=fade*fade*(3-2*fade);
            result.attenuation=attenuation*fade*parameters.z;
            // Keep the area-source tail until the lower donor cutoff; the
            // vanilla 0.001 early-out would truncate it well inside the radius.
            result.cutoff=.00001;
        }
    }
    return result;
}
