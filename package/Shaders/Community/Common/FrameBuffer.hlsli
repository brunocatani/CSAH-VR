#ifndef FRAMEBUFFER_HLSLI
#define FRAMEBUFFER_HLSLI

// CB12 PerFrame Camera — game-provided, read-only
// Both VR eye matrices packed in one buffer
cbuffer PerFrameCamera : register(b12) {
    float4x4 CB12_ViewMatrix;                    // 0x000 — shared both eyes
    float4x4 CB12_ProjMatrixEye0;                // 0x040
    float4x4 CB12_ProjMatrixEye1;                // 0x080
    float4x4 CB12_ViewProjMatrixEye0;            // 0x0C0
    float4x4 CB12_ViewProjMatrixEye1;            // 0x100
    float4x4 CB12_InvViewMatrix;                 // 0x140
    float4x4 CB12_InvViewProjMatrixEye0;         // 0x180
    float4x4 CB12_InvViewProjMatrixEye1;         // 0x1C0
    float4x4 CB12_InvProjMatrixEye0;             // 0x200
    float4x4 CB12_InvProjMatrixEye1;             // 0x240
    float4x4 CB12_InvFirstPersonProjEye0;                     // 0x280
    float4x4 CB12_InvFirstPersonProjEye1;                     // 0x2C0
    float4 CB12_ShaderParameters48; // 0x300: global shader state, not clip planes
    float4 CB12_ShaderParameters49; // 0x310: global shader state
    float4 CB12_ShaderParameters50; // 0x320: derive screen dimensions from resources
    float4x4 CB12_PrevViewProjUnjitteredEye0;    // 0x330
    float4x4 CB12_PrevViewProjUnjitteredEye1;    // 0x370
    float4   CB12_CurrentEyePosition0;            // 0x3B0
    float4   CB12_CurrentEyePosition1;            // 0x3C0
    float4   CB12_PreviousEyePosition0;           // 0x3D0
    float4   CB12_PreviousEyePosition1;           // 0x3E0
    float4x4 CB12_ViewProjUnjitteredEye0;        // 0x3F0
    float4x4 CB12_ViewProjUnjitteredEye1;        // 0x430
    float4   CB12_FogColor1;                      // 0x470
    float4   CB12_FogColor2;                      // 0x480
    float4   CB12_FogParams1;                     // 0x490
    float4   CB12_FogParams2;                     // 0x4A0
    float4   CB12_FogParams3;                     // 0x4B0
    float4   CB12_FogParams4;                     // 0x4C0
};

#endif
