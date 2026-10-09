#ifndef CSAH_FO4VR_DEPTH_HLSLI
#define CSAH_FO4VR_DEPTH_HLSLI

// Native DFComposite selects b12[40..47] for the compressed first-person
// range and b12[32..39] for world depth. Decode before applying a projection.
bool Fo4VrFirstPersonDepth(float depth)
{
    return depth > 0.0f && depth <= 0.01f;
}

float Fo4VrProjectionDepth(float depth)
{
    return depth <= 0.01f ? depth * 100.0f : depth * 1.01f - 0.01f;
}

uint Fo4VrInverseProjectionBase(float depth, uint eye)
{
    return (depth <= 0.01f ? 40u : 32u) + eye * 4u;
}

#endif
