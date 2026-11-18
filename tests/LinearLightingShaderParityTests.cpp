#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr UINT kRenderTargetCount = 6;
    constexpr UINT kFloat4Size = sizeof(float) * 4;
    constexpr float kAbsoluteTolerance = 2.0e-5F;
    constexpr float kRelativeTolerance = 2.0e-5F;

    struct ShaderContract
    {
        std::string_view name;
        UINT mrtCount;
        bool hasVertexColor;
        bool isInstanced;
        bool usesTessellatedInputs{};
        bool hasAdditionalAlphaMask{};
        bool hasLandscapeLod{};
        bool hasGradientRemap{};
        bool hasGradientHair{};
        bool hasBoneTint{};
        bool hasMenuScreen{};
        bool hasPipboyScreen{};
        bool hasFaceDetail{};
        bool faceUsesModelSpaceNormals{};
        bool hasSkinTint{};
        bool hasStandaloneHair{};
        bool hasDismemberment{};
        bool hasMeatCuff{};
        bool hasCombinedMaterial{};
        bool hasLandscapeLayers{};
        bool hasInstancedLandscapeLayers{};
    };

    constexpr std::array kShaderContracts{
        ShaderContract{ "DefaultProjectedFiveMrt_L4_00008002", 5, false, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L3_00008003", 5, true, false },
        ShaderContract{ "DefaultSixMrt_L4_00000002", 6, false, false },
        ShaderContract{ "DefaultSixMrt_L3_00000003", 6, true, false },
        ShaderContract{ "DefaultModelSpaceSixMrt_L4_00000006", 6, false, false },
        ShaderContract{ "DefaultModelSpaceSixMrt_L3_00000007", 6, true, false },
        ShaderContract{ "DefaultDefShadowSixMrt_L4_00004002", 6, false, false },
        ShaderContract{ "DefaultDefShadowSixMrt_L3_00004003", 6, true, false },
        ShaderContract{ "EnvmapSixMrt_L4_00000102", 6, false, false },
        ShaderContract{ "EnvmapSixMrt_L3_00000103", 6, true, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_L4_00000106", 6, false, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_L3_00000107", 6, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L4_00008102", 5, false, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L3_00008103", 5, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_L4_00008106", 5, false, false },
        ShaderContract{ "TexturedEmissionAlphaTestSixMrt_L4_00004102", 6, false, false },
        ShaderContract{ "TexturedEmissionAlphaTestSixMrt_L3_00004103", 6, true, false },
        ShaderContract{ "EnvmapModelSpaceSixMrt_RgbOnlyAlphaTest_54F53016", 6, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_RgbOnlyAlphaTest_5A5E1AD5", 5, true, false },
        ShaderContract{ "EnvmapProjectedFiveMrt_VertexColorNoEarlyDepth_B4D2FE98", 5, true, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L4NoEarlyDepth_15E29A6C", 5, false, false },
        ShaderContract{ "DefaultProjectedFiveMrt_L3NoEarlyDepth_B80CA12A", 5, true, false },
        ShaderContract{ "GlowmapSixMrt_L4NoEarlyDepth_00004006", 6, false, false },
        ShaderContract{ "GlowmapSixMrt_L3NoEarlyDepth_00004007", 6, true, false },
        ShaderContract{ "GlowmapAlphaTestSixMrt_L4NoEarlyDepth_00004106", 6, false, false },
        ShaderContract{ "GlowmapAlphaTestSixMrt_L3NoEarlyDepth_00004107", 6, true, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L4_0000C002", 5, false, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L3_0000C003", 5, true, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L4NoEarlyDepth_0000C006", 5, false, false },
        ShaderContract{ "GlowmapBlendFiveMrt_L3NoEarlyDepth_0000C007", 5, true, false },
        ShaderContract{ "GlowmapAlphaTestBlendFiveMrt_L4_0000C102", 5, false, false },
        ShaderContract{ "GlowmapAlphaTestBlendFiveMrt_L3_0000C103", 5, true, false },
        ShaderContract{ "InstancedSixMrt_L4_08000002", 6, false, true },
        ShaderContract{ "InstancedSixMrt_L3_08000003", 6, true, true },
        ShaderContract{ "ModelSpaceNormalsSixMrt_L4_00002002", 6, false, false },
        ShaderContract{ "ModelSpaceNormalsSixMrt_L3_00002003", 6, true, false },
        ShaderContract{ "ModelSpaceNormalsAlphaTestSixMrt_L4_00002102", 6, false, false },
        ShaderContract{ "TessellatedSixMrt_L4_00080002", 6, false, false, true },
        ShaderContract{ "TessellatedSixMrt_L3_00080003", 6, true, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_L4_00080102", 6, false, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_L3_00080103", 6, true, false, true },
        ShaderContract{ "TessellatedAlphaTestSixMrt_RgbOnlyVertexColor_00080503", 6, true, false, true },
        ShaderContract{ "InstancedLandLodBlendSixMrt_L4_0A000002", 6, false, true },
        ShaderContract{ "InstancedLandLodBlendSixMrt_L3_0A000003", 6, true, true },
        ShaderContract{ "ModelSpaceNormalsSkinnedSixMrt_L4_00002006", 6, false, false },
        ShaderContract{ "AdditionalAlphaMaskSixMrt_L4_01000002", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskSixMrt_L3_01000003", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskAlphaTestSixMrt_L4_01000102", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskAlphaTestSixMrt_L3_01000103", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskAlphaTestSixMrt_RgbOnlyVertexColor_01000503", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskModelSpaceNormalsSixMrt_L4_01002002", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskModelSpaceNormalsSixMrt_L3_01002003", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskModelSpaceNormalsAlphaTestSixMrt_L4_01002102", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskTessellatedSixMrt_L4_01080002", 6, false, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskTessellatedSixMrt_L3_01080003", 6, true, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskTessellatedAlphaTestSixMrt_L4_01080102", 6, false, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskTessellatedAlphaTestSixMrt_L3_01080103", 6, true, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskTessellatedAlphaTestSixMrt_RgbOnlyVertexColor_01080503", 6, true, false, true, true },
        ShaderContract{ "LandscapeLodSixMrt_L4_00000202", 6, false, false, false, false, true },
        ShaderContract{ "LandscapeLodSixMrt_L3_00000203", 6, true, false, false, false, true },
        ShaderContract{ "LandscapeLodAlphaTestSixMrt_L4_00000302", 6, false, false, false, false, true },
        ShaderContract{ "LandscapeLodAlphaTestSixMrt_L3_00000303", 6, true, false, false, false, true },
        ShaderContract{ "LandscapeLodModelSpaceNormalsSixMrt_L4_00002202", 6, false, false, false, false, true },
        ShaderContract{ "LandscapeLodInstancedSixMrt_L4_08000202", 6, false, true, false, false, true },
        ShaderContract{ "LandscapeLodInstancedSixMrt_L3_08000203", 6, true, true, false, false, true },
        ShaderContract{ "GradientRemapSixMrt_L4_04000006", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapSixMrt_L3_04000007", 6, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestSixMrt_L4_04000106", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestSixMrt_L3_04000107", 6, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapTessellatedSixMrt_L4_04080002", 6, false, false, true, false, false, true },
        ShaderContract{ "GradientRemapTessellatedSixMrt_L3_04080003", 6, true, false, true, false, false, true },
        ShaderContract{ "GradientRemapProjectedFiveMrt_L4_04008002", 5, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapProjectedFiveMrt_L3_04008003", 5, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestProjectedFiveMrt_L3_04008103", 5, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapHairProjectedFiveMrt_L4_04028002", 5, false, false, false, false, false, true, true },
        ShaderContract{ "GradientRemapHairProjectedFiveMrt_L3_04028003", 5, true, false, false, false, false, true, true },
        ShaderContract{ "GradientRemapHairAlphaTestProjectedFiveMrt_L3_04028103", 5, true, false, false, false, false, true, true },
        ShaderContract{ "GradientRemapProjectedFiveMrt_L4NoEarlyDepth_04008006", 5, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapProjectedFiveMrt_L3NoEarlyDepth_04008007", 5, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestProjectedFiveMrt_L4NoEarlyDepth_04008106", 5, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestProjectedFiveMrt_L3NoEarlyDepth_04008107", 5, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapHairProjectedFiveMrt_L4NoEarlyDepth_04028006", 5, false, false, false, false, false, true, true },
        ShaderContract{ "GradientRemapHairProjectedFiveMrt_L3NoEarlyDepth_04028007", 5, true, false, false, false, false, true, true },
        ShaderContract{ "GradientRemapHairAlphaTestProjectedFiveMrt_L3NoEarlyDepth_04028107", 5, true, false, false, false, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskProjectedFiveMrt_L4_01008002", 5, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskProjectedFiveMrt_L3_01008003", 5, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskAlphaTestProjectedFiveMrt_L4_01008102", 5, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskAlphaTestProjectedFiveMrt_L3_01008103", 5, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskModelSpaceNormalsProjectedFiveMrt_L4_0100A002", 5, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskLodObjectProjectedFiveMrt_L4_01018802", 5, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapSixMrt_L4_05000002", 6, false, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapSixMrt_L3_05000003", 6, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapAlphaTestSixMrt_L4_05000102", 6, false, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapAlphaTestSixMrt_L3_05000103", 6, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapTessellatedSixMrt_L4_05080002", 6, false, false, true, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapTessellatedSixMrt_L3_05080003", 6, true, false, true, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapProjectedFiveMrt_L4_05008002", 5, false, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapProjectedFiveMrt_L3_05008003", 5, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapAlphaTestProjectedFiveMrt_L3_05008103", 5, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapHairProjectedFiveMrt_L4_05028002", 5, false, false, false, true, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapHairProjectedFiveMrt_L3_05028003", 5, true, false, false, true, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapHairAlphaTestProjectedFiveMrt_L3_05028103", 5, true, false, false, true, false, true, true },
        ShaderContract{ "BoneTintGradientRemapProjectedFiveMrt_L4_44008002", 5, false, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapProjectedFiveMrt_L3_44008003", 5, true, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestProjectedFiveMrt_L3_44008103", 5, true, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapHairProjectedFiveMrt_L4_44028002", 5, false, false, false, false, false, true, true, true },
        ShaderContract{ "BoneTintGradientRemapHairProjectedFiveMrt_L3_44028003", 5, true, false, false, false, false, true, true, true },
        ShaderContract{ "BoneTintGradientRemapHairAlphaTestProjectedFiveMrt_L3_44028103", 5, true, false, false, false, false, true, true, true },
        ShaderContract{ "BoneTintGradientRemapProjectedFiveMrt_L4NoEarlyDepth_44008006", 5, false, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapProjectedFiveMrt_L3NoEarlyDepth_44008007", 5, true, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestProjectedFiveMrt_L4NoEarlyDepth_44008106", 5, false, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapHairProjectedFiveMrt_L4NoEarlyDepth_44028006", 5, false, false, false, false, false, true, true, true },
        ShaderContract{ "BoneTintGradientRemapHairProjectedFiveMrt_L3NoEarlyDepth_44028007", 5, true, false, false, false, false, true, true, true },
        ShaderContract{ "BoneTintGradientRemapHairAlphaTestProjectedFiveMrt_L3NoEarlyDepth_44028107", 5, true, false, false, false, false, true, true, true },
        ShaderContract{ "GlowmapTessellatedSixMrt_L4_00084002", 6, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapSixMrt_L4_01004002", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapSixMrt_L3_01004003", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapAlphaTestSixMrt_L4_01004102", 6, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapAlphaTestSixMrt_L3_01004103", 6, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapTessellatedAlphaTestSixMrt_L3_01084103", 6, true, false, true, true },
        ShaderContract{ "MenuScreenSixMrt_L4_00010002", 6, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "MenuScreenSixMrt_L3_00010003", 6, true, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "MenuScreenInstancedSixMrt_L3_08010003", 6, true, true, false, false, false, false, false, false, true, false },
        ShaderContract{ "PipboyScreenSixMrt_L4_00800002", 6, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "PipboyScreenSixMrt_L3_00800003", 6, true, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailSixMrt_L4_80000042", 6, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailSixMrt_L3_80000043", 6, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailAlphaTestSixMrt_L4_80000142", 6, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailModelSpaceNormalsSixMrt_L4_80002042", 6, false, false, false, false, false, false, false, false, false, false, true, true },
        ShaderContract{ "FaceDetailModelSpaceNormalsAlphaTestSixMrt_L4_80002142", 6, false, false, false, false, false, false, false, false, false, false, true, true },
        ShaderContract{ "SkinTintCharacterLightMaskSixMrt_L4_00040002", 6, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "SkinTintVertexColorSixMrt_L3_00040003", 6, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "SkinTintAlphaTestSixMrt_L4_00040102", 6, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "SkinTintCharacterLightMaskModelSpaceNormalsSixMrt_L4_00042002", 6, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskSkinTintSixMrt_L4_01040002", 6, false, false, false, true, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskSkinTintSixMrt_L3_01040003", 6, true, false, false, true, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskSkinTintAlphaTestSixMrt_L4_01040102", 6, false, false, false, true, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskSkinTintModelSpaceNormalsSixMrt_L4_01042002", 6, false, false, false, true, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapSixMrt_L4_04004002", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapSixMrt_L3_04004003", 6, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapAlphaTestSixMrt_L4_04004102", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapAlphaTestSixMrt_L3_04004103", 6, true, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskFaceDetailSixMrt_L4_81000042", 6, false, false, false, true, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskFaceDetailAlphaTestSixMrt_L4_81000142", 6, false, false, false, true, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskFaceDetailModelSpaceNormalsSixMrt_L4_81002042", 6, false, false, false, true, false, false, false, false, false, false, true, true },
        ShaderContract{ "AdditionalAlphaMaskFaceDetailModelSpaceNormalsAlphaTestSixMrt_L4_81002142", 6, false, false, false, true, false, false, false, false, false, false, true, true },
        ShaderContract{ "BoneTintFaceDetailSixMrt_L4_C0000042", 6, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "BoneTintFaceDetailSixMrt_L3_C0000043", 6, true, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "BoneTintFaceDetailAlphaTestSixMrt_L4_C0000142", 6, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "BoneTintFaceDetailModelSpaceNormalsSixMrt_L4_C0002042", 6, false, false, false, false, false, false, false, true, false, false, true, true },
        ShaderContract{ "GradientRemapSixMrt_L4_04000002", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapSixMrt_L3_04000003", 6, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestSixMrt_L4_04000102", 6, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapAlphaTestSixMrt_L3_04000103", 6, true, false, false, false, false, true },
        ShaderContract{ "BoneTintSixMrt_L4_40000002", 6, false, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintSixMrt_L3_40000003", 6, true, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintSixMrt_L4NoEarlyDepth_40000006", 6, false, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintSixMrt_L3NoEarlyDepth_40000007", 6, true, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestSixMrt_L4_44000102", 6, false, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestSixMrt_L3_44000103", 6, true, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestSixMrt_L4NoEarlyDepth_44000106", 6, false, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintGradientRemapAlphaTestSixMrt_L3NoEarlyDepth_44000107", 6, true, false, false, false, false, true, false, true },
        ShaderContract{ "BoneTintSkinTintSixMrt_L4_40040002", 6, false, false, false, false, false, false, false, true, false, false, false, false, true },
        ShaderContract{ "BoneTintSkinTintModelSpaceNormalsSixMrt_L4_40042002", 6, false, false, false, false, false, false, false, true, false, false, false, false, true },
        ShaderContract{ "BoneTintSkinTintSixMrt_L4NoEarlyDepth_40040006", 6, false, false, false, false, false, false, false, true, false, false, false, false, true },
        ShaderContract{ "BoneTintProjectedFiveMrt_L4_40008002", 5, false, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintProjectedFiveMrt_L3_40008003", 5, true, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintProjectedFiveMrt_L4NoEarlyDepth_40008006", 5, false, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintProjectedFiveMrt_L3NoEarlyDepth_40008007", 5, true, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintLodObjectProjectedFiveMrt_L4_40018802", 5, false, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintLodObjectProjectedFiveMrt_L4NoEarlyDepth_40018806", 5, false, false, false, false, false, false, false, true },
        ShaderContract{ "LodObjectProjectedFiveMrt_L4_00018802", 5, false, false },
        ShaderContract{ "LodObjectProjectedFiveMrt_L3_00018803", 5, true, false },
        ShaderContract{ "LodObjectProjectedFiveMrt_L4NoEarlyDepth_00018806", 5, false, false },
        ShaderContract{ "LodObjectProjectedFiveMrt_L3NoEarlyDepth_00018807", 5, true, false },
        ShaderContract{ "LodObjectAlphaTestProjectedFiveMrt_L4_00018902", 5, false, false },
        ShaderContract{ "LodObjectAlphaTestProjectedFiveMrt_L4NoEarlyDepth_00018906", 5, false, false },
        ShaderContract{ "LandscapeLodBlendFiveMrt_L4_00008202", 5, false, false, false, false, true },
        ShaderContract{ "LandscapeLodBlendFiveMrt_L3_00008203", 5, true, false, false, false, true },
        ShaderContract{ "ModelSpaceNormalsProjectedFiveMrt_L4_0000A002", 5, false, false },
        ShaderContract{ "ModelSpaceNormalsProjectedFiveMrt_L3_0000A003", 5, true, false },
        ShaderContract{ "HairSixMrt_L3_00020003", 6, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairAlphaTestSixMrt_L3_00020103", 6, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairAlphaTestSixMrt_L4NoEarlyDepth_00020106", 6, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairAlphaTestSixMrt_L3NoEarlyDepth_00020107", 6, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairProjectedFiveMrt_L3_00028003", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairAlphaTestProjectedFiveMrt_L3_00028103", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "HairAlphaTestProjectedFiveMrt_L3NoEarlyDepth_00028107", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskHairAlphaTestSixMrt_L3NoEarlyDepth_01020103", 6, true, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskHairAlphaTestSixMrt_L4NoEarlyDepth_01020106", 6, false, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskHairProjectedFiveMrt_L3NoEarlyDepth_01028003", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskHairAlphaTestProjectedFiveMrt_L3NoEarlyDepth_01028103", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapHairSixMrt_L3_04020003", 6, true, false, false, false, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapHairAlphaTestSixMrt_L4_04024102", 6, false, false, false, false, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapHairAlphaTestSixMrt_L3_04024103", 6, true, false, false, false, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapHairAlphaTestSixMrt_L4NoEarlyDepth_04024106", 6, false, false, false, false, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapHairAlphaTestSixMrt_L3NoEarlyDepth_04024107", 6, true, false, false, false, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapGlowmapHairAlphaTestSixMrt_L4NoEarlyDepth_05024102", 6, false, false, false, true, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapGlowmapHairAlphaTestSixMrt_L3NoEarlyDepth_05024103", 6, true, false, false, true, false, true, true, false, false, false, false, false, false, true },
        ShaderContract{ "BoneTintGradientRemapGlowmapHairAlphaTestSixMrt_L4_44024102", 6, false, false, false, false, false, true, true, true, false, false, false, false, false, true },
        ShaderContract{ "BoneTintGradientRemapGlowmapHairAlphaTestSixMrt_L3_44024103", 6, true, false, false, false, false, true, true, true, false, false, false, false, false, true },
        ShaderContract{ "BoneTintGradientRemapGlowmapHairAlphaTestSixMrt_L4NoEarlyDepth_44024106", 6, false, false, false, false, false, true, true, true, false, false, false, false, false, true },
        ShaderContract{ "BoneTintGradientRemapGlowmapHairAlphaTestSixMrt_L3NoEarlyDepth_44024107", 6, true, false, false, false, false, true, true, true, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskBoneTintGradientRemapGlowmapHairAlphaTestSixMrt_L3NoEarlyDepth_45024103", 6, true, false, false, true, false, true, true, true, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapSixMrt_L3NoEarlyDepth_04004007", 6, true, false, false, false, false, true },
        ShaderContract{ "GradientRemapGlowmapAlphaTestSixMrt_L3NoEarlyDepth_04004107", 6, true, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapGlowmapSixMrt_L4NoEarlyDepth_05004002", 6, false, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapGlowmapSixMrt_L3NoEarlyDepth_05004003", 6, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapGlowmapAlphaTestSixMrt_L3NoEarlyDepth_05004103", 6, true, false, false, true, false, true },
        ShaderContract{ "AdditionalAlphaMaskPipboyScreenSixMrt_L4NoEarlyDepth_01800002", 6, false, false, false, true, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskPipboyScreenSixMrt_L3NoEarlyDepth_01800003", 6, true, false, false, true, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapBlendFiveMrt_L4_0100C002", 5, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapBlendFiveMrt_L3_0100C003", 5, true, false, false, true },
        ShaderContract{ "SkinTintSixMrt_L4NoEarlyDepth_00040006", 6, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "SkinTintAlphaTestSixMrt_L4NoEarlyDepth_00040106", 6, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "SkinTintTessellatedSixMrt_L3_000C0003", 6, true, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailSixMrt_L4NoEarlyDepth_80000046", 6, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailAlphaTestSixMrt_L4NoEarlyDepth_80000146", 6, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailModelSpaceNormalsSixMrt_L4NoEarlyDepth_80002046", 6, false, false, false, false, false, false, false, false, false, false, true, true },
        ShaderContract{ "DismembermentTessellatedSixMrt_L4_00280042", 6, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "DismembermentTessellatedSixMrt_L3_00280043", 6, true, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "DismembermentModelSpaceNormalsTessellatedSixMrt_L3_00282043", 6, true, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "MeatCuffProjectedFiveMrt_L4_00408042", 5, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "MeatCuffProjectedFiveMrt_L3_00408043", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "MeatCuffModelSpaceNormalsProjectedFiveMrt_L3_0040A043", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "DismembermentSkinTintTessellatedSixMrt_L4_002C0042", 6, false, false, true, false, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "DismembermentSkinTintTessellatedSixMrt_L3_002C0043", 6, true, false, true, false, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "DismembermentSkinTintModelSpaceNormalsTessellatedSixMrt_L3_002C2043", 6, true, false, true, false, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "MeatCuffSkinTintProjectedFiveMrt_L4_00448042", 5, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "MeatCuffSkinTintProjectedFiveMrt_L3_00448043", 5, true, false, false, false, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "MeatCuffSkinTintModelSpaceNormalsProjectedFiveMrt_L3_0044A043", 5, true, false, false, false, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskDismembermentTessellatedSixMrt_L4NoEarlyDepth_01280046", 6, false, false, true, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentTessellatedSixMrt_L3_01280043", 6, true, false, true, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentModelSpaceNormalsTessellatedSixMrt_L3_01282043", 6, true, false, true, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentSkinTintTessellatedSixMrt_L4NoEarlyDepth_012C0046", 6, false, false, true, true, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentSkinTintTessellatedSixMrt_L3_012C0043", 6, true, false, true, true, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentSkinTintModelSpaceNormalsTessellatedSixMrt_L3_012C2043", 6, true, false, true, true, false, false, false, false, false, false, false, false, true, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffProjectedFiveMrt_L4NoEarlyDepth_01408046", 5, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffProjectedFiveMrt_L3_01408043", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffModelSpaceNormalsProjectedFiveMrt_L3_0140A043", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffSkinTintProjectedFiveMrt_L4NoEarlyDepth_01448046", 5, false, false, false, true, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffSkinTintProjectedFiveMrt_L3_01448043", 5, true, false, false, true, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskMeatCuffSkinTintModelSpaceNormalsProjectedFiveMrt_L3_0144A043", 5, true, false, false, true, false, false, false, false, false, false, false, false, true, false, false, true },
        ShaderContract{ "GlowmapDismembermentTessellatedSixMrt_L4NoEarlyDepth_00284046", 6, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "GlowmapDismembermentTessellatedSixMrt_L3_00284043", 6, true, false, true, false, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskGlowmapDismembermentTessellatedSixMrt_L4NoEarlyDepth_01284046", 6, false, false, true, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskGlowmapDismembermentTessellatedSixMrt_L3_01284043", 6, true, false, true, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "GlowmapMeatCuffProjectedFiveMrt_L4NoEarlyDepth_0040C046", 5, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "GlowmapMeatCuffProjectedFiveMrt_L3_0040C043", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapMeatCuffProjectedFiveMrt_L4NoEarlyDepth_0140C046", 5, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGlowmapMeatCuffProjectedFiveMrt_L3_0140C043", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapDismembermentTessellatedSixMrt_L4NoEarlyDepth_04280046", 6, false, false, true, false, false, true, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "GradientRemapDismembermentTessellatedSixMrt_L3_04280043", 6, true, false, true, false, false, true, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapDismembermentTessellatedSixMrt_L4NoEarlyDepth_05280046", 6, false, false, true, true, false, true, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapDismembermentTessellatedSixMrt_L3_05280043", 6, true, false, true, true, false, true, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "GradientRemapMeatCuffProjectedFiveMrt_L4NoEarlyDepth_04408046", 5, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "GradientRemapMeatCuffProjectedFiveMrt_L3_04408043", 5, true, false, false, false, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapMeatCuffProjectedFiveMrt_L4NoEarlyDepth_05408046", 5, false, false, false, true, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskGradientRemapMeatCuffProjectedFiveMrt_L3_05408043", 5, true, false, false, true, false, true, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "DismembermentBlendFiveMrt_L3NoEarlyDepth_00288047", 5, true, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "AdditionalAlphaMaskDismembermentBlendFiveMrt_L3NoEarlyDepth_01288047", 5, true, false, false, true, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "CombinedAlphaTestSixMrt_L4_10000142", 6, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "CombinedAlphaTestSixMrt_L3_10000143", 6, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskCombinedAlphaTestSixMrt_L4_11000142", 6, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskCombinedAlphaTestSixMrt_L3_11000143", 6, true, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "CombinedAlphaTestBlendFiveMrt_L4_10008142", 5, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "CombinedGradientRemapAlphaTestSixMrt_L4_14000142", 6, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "AdditionalAlphaMaskCombinedGradientRemapAlphaTestSixMrt_L4_15000142", 6, false, false, false, true, false, true, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "LandscapeLodMenuScreenSixMrt_L4_00010202", 6, false, false, false, false, true, false, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "LandscapeLodMenuScreenSixMrt_L3_00010203", 6, true, false, false, false, true, false, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "LandscapeLodMenuScreenInstancedSixMrt_L3_08010203", 6, true, true, false, false, true, false, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "LandLodBlendMenuScreenSixMrt_L4_02010002", 6, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "LandLodBlendMenuScreenSixMrt_L3_02010003", 6, true, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "LandscapeFourLayerSixMrt_L3_00000023", 6, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "LandscapeFourLayerLodBlendSixMrt_L3_02000023", 6, true, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, true, false },
        ShaderContract{ "InstancedLandscapeFourLayerLodBlendSixMrt_L3_0A000023", 6, true, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, true, true },
        ShaderContract{ "BoneTintGlowmapAlphaTestSixMrt_L3NoEarlyDepth_40004107", 6, true, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false },
        ShaderContract{ "BoneTintFaceDetailSixMrt_L4NoEarlyDepth_C0000046", 6, false, false, false, false, false, false, false, true, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "BoneTintGradientRemapGlowmapAlphaTestSixMrt_L3_44004103", 6, true, false, false, false, false, true, false, true, false, false, false, false, false, false, false, false, false, false, false },
        ShaderContract{ "AdditionalAlphaMaskBoneTintFaceDetailSixMrt_L4NoEarlyDepth_C1000042", 6, false, false, false, true, false, false, false, true, false, false, true, false, false, false, false, false, false, false, false },
        ShaderContract{ "AdditionalAlphaMaskBoneTintBlendFiveMrt_L3NoEarlyDepth_41008003", 5, true, false, false, true, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false },
        ShaderContract{ "AdditionalAlphaMaskBoneTintSkinTintSixMrt_L4NoEarlyDepth_41040002", 6, false, false, false, true, false, false, false, true, false, false, false, false, true, false, false, false, false, false, false },
        ShaderContract{ "TessellatedAlphaTestBlendFiveMrt_L3NoEarlyDepth_00088103", 5, true, false, true },
        ShaderContract{ "SkinTintBlendFiveMrt_L3_00048003", 5, true, false, false, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "UntexturedSixMrt_L4_00000000", 6, false, false },
        ShaderContract{ "GlowmapPipboyScreenSixMrt_L4_00804002", 6, false, false, false, false, false, false, false, false, false, true },
        ShaderContract{ "MenuScreenBlendFiveMrt_L3_00018003", 5, true, false, false, false, false, false, false, false, true },
        ShaderContract{ "FaceDetailSkinTintSixMrt_L4_80040042", 6, false, false, false, false, false, false, false, false, false, false, true, false, true },
    };

    enum class AdditionalAlphaCase : std::uint8_t
    {
        disabled,
        texturePass,
        textureReject,
        noisePass,
        noiseReject,
    };

    enum class MeatCuffAlphaCase : std::uint8_t
    {
        pass,
        reject,
    };

    struct RenderTargets
    {
        std::array<ComPtr<ID3D11Texture2D>, kRenderTargetCount> gpu;
        std::array<ComPtr<ID3D11RenderTargetView>, kRenderTargetCount> views;
        std::array<ComPtr<ID3D11Texture2D>, kRenderTargetCount> staging;
    };

    using Pixel = std::array<float, 4>;
    using RenderResult = std::array<Pixel, kRenderTargetCount>;

    struct LinearLightingCase
    {
        std::string_view name;
        bool enabled;
        float colorGamma;
        float emitColorGamma;
        float glowmapGamma;
        float vanillaDiffuseColorMult;
        float emitColorMult;
        float glowmapMult;
        float emissiveMult;
    };

    constexpr LinearLightingCase kDisabledCase{
        "disabled",
        false,
        2.2F,
        1.8F,
        1.6F,
        1.3F,
        0.75F,
        1.4F,
        3.0F,
    };
    constexpr LinearLightingCase kIdentityCase{
        "enabled-identity",
        true,
        2.2F,
        2.2F,
        2.2F,
        1.0F,
        1.0F,
        1.0F,
        3.0F,
    };
    constexpr LinearLightingCase kTransformedCase{
        "enabled-transformed",
        true,
        2.2F,
        1.8F,
        1.6F,
        1.3F,
        0.75F,
        1.4F,
        3.0F,
    };

    constexpr Pixel kDiffuseTexture{ 0.1F, 0.5F, 0.75F, 0.8F };
    constexpr Pixel kNormalTexture{ 0.35F, 0.65F, 0.2F, 0.8F };
    constexpr Pixel kSpecularTexture{ 0.45F, 0.7F, 0.15F, 0.9F };
    constexpr Pixel kGlowTexture{ 0.6F, 0.4F, 0.2F, 1.0F };
    constexpr std::array<Pixel, 4> kDismembermentDiffuseTexture{
        Pixel{ 0.15F, 0.25F, 0.35F, 0.55F },
        Pixel{ 0.7F, 0.2F, 0.4F, 0.65F },
        Pixel{ 0.3F, 0.75F, 0.25F, 0.8F },
        Pixel{ 0.85F, 0.35F, 0.6F, 0.7F },
    };
    constexpr std::array<Pixel, 4> kDismembermentNormalTexture{
        Pixel{ 0.55F, 0.35F, 0.3F, 0.8F },
        Pixel{ 0.6F, 0.4F, 0.3F, 0.8F },
        Pixel{ 0.45F, 0.65F, 0.3F, 0.8F },
        Pixel{ 0.35F, 0.55F, 0.3F, 0.8F },
    };
    constexpr std::array<Pixel, 4> kDismembermentSpecularTexture{
        Pixel{ 0.15F, 0.35F, 0.2F, 0.9F },
        Pixel{ 0.25F, 0.8F, 0.35F, 0.9F },
        Pixel{ 0.65F, 0.3F, 0.45F, 0.9F },
        Pixel{ 0.4F, 0.7F, 0.55F, 0.9F },
    };
    constexpr std::array<Pixel, 4> kScreenTexture{
        Pixel{ 0.12F, 0.34F, 0.56F, 1.0F },
        Pixel{ 0.78F, 0.23F, 0.45F, 1.0F },
        Pixel{ 0.65F, 0.42F, 0.18F, 1.0F },
        Pixel{ 0.31F, 0.73F, 0.27F, 1.0F },
    };
    constexpr Pixel kFaceDetailTexture{ 0.2F, 0.7F, 0.4F, 0.85F };
    constexpr Pixel kSkinTintColor{ 0.2F, 0.65F, 0.9F, 0.7F };
    constexpr Pixel kVertexColor{ 0.8F, 0.7F, 0.6F, 0.9F };
    constexpr Pixel kEmitColor{ 0.3F, 0.45F, 0.6F, 0.2F };
    constexpr Pixel kInstanceEmitColor{ 0.55F, 0.25F, 0.7F, 0.2F };
    constexpr Pixel kAdditionalAlphaTexture{ 0.0F, 0.0F, 0.0F, 0.35F };
    constexpr Pixel kAdditionalAlphaNoise{ 0.75F, 0.0F, 0.0F, 0.0F };
    constexpr Pixel kLandscapeLodDiffuse{ 0.65F, 0.7F, 0.75F, 1.0F };
    constexpr Pixel kLandscapeLodNormal{ 0.6F, 0.4F, 0.0F, 1.0F };
    constexpr std::array<Pixel, 4> kLandscapeDiffuseLayers{
        Pixel{ 0.1F, 0.5F, 0.75F, 1.0F },
        Pixel{ 0.65F, 0.2F, 0.4F, 1.0F },
        Pixel{ 0.3F, 0.8F, 0.25F, 1.0F },
        Pixel{ 0.9F, 0.35F, 0.6F, 1.0F },
    };
    constexpr std::array<Pixel, 4> kLandscapeNormalLayers{
        Pixel{ 0.45F, 0.55F, 0.0F, 1.0F },
        Pixel{ 0.6F, 0.45F, 0.0F, 1.0F },
        Pixel{ 0.4F, 0.65F, 0.0F, 1.0F },
        Pixel{ 0.55F, 0.35F, 0.0F, 1.0F },
    };
    constexpr std::array<Pixel, 4> kLandscapeSpecularLayers{
        Pixel{ 0.2F, 0.35F, 0.0F, 1.0F },
        Pixel{ 0.45F, 0.7F, 0.0F, 1.0F },
        Pixel{ 0.65F, 0.25F, 0.0F, 1.0F },
        Pixel{ 0.3F, 0.8F, 0.0F, 1.0F },
    };
    constexpr std::array<float, 4> kLandscapeLayerWeights{
        0.4F,
        0.3F,
        0.2F,
        0.1F,
    };
    constexpr Pixel kLandscapeLodMultiplier{ 0.9F, 0.8F, 0.7F, 1.0F };
    constexpr float kLandscapeLodBlend = 0.35F;
    constexpr Pixel kBoneTintLookup{ 0.1F, 0.75F, 0.2F, 0.6F };
    constexpr Pixel kBoneTintVertexColor{ 0.2F, 0.3F, 0.4F, 0.625F };
    constexpr std::array<Pixel, 4> kBoneTintPalette{
        Pixel{ 0.15F, 0.25F, 0.35F, 0.45F },
        Pixel{ 0.75F, 0.2F, 0.4F, 0.5F },
        Pixel{ 0.6F, 0.8F, 0.3F, 0.65F },
        Pixel{ 0.35F, 0.65F, 0.85F, 0.55F },
    };
    constexpr std::array<Pixel, 4> kGradientRemapTexture{
        Pixel{ 0.1F, 0.15F, 0.2F, 0.25F },
        Pixel{ 0.25F, 0.4F, 0.65F, 0.45F },
        Pixel{ 0.7F, 0.6F, 0.5F, 0.4F },
        Pixel{ 0.8F, 0.55F, 0.3F, 0.75F },
    };

    constexpr std::array<float, 4> kClearColor{
        123.25F,
        -456.5F,
        789.75F,
        -101.125F,
    };

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(HRESULT result, const std::string& operation)
    {
        if (SUCCEEDED(result)) {
            return;
        }
        fail(operation + " failed with HRESULT 0x" +
            [] (HRESULT value) {
                std::array<char, 16> buffer{};
                (void)std::snprintf(
                    buffer.data(),
                    buffer.size(),
                    "%08X",
                    static_cast<unsigned int>(value));
                return std::string(buffer.data());
            }(result));
    }

    [[nodiscard]] std::vector<std::byte> readFile(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            fail("could not open shader " + path.string());
        }
        const auto end = stream.tellg();
        if (end <= 0) {
            fail("shader is empty: " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        stream.seekg(0, std::ios::beg);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            fail("could not read shader " + path.string());
        }
        return bytes;
    }

    [[nodiscard]] bool usesTextureSlot(
        std::span<const std::byte> bytecode,
        UINT slot)
    {
        ComPtr<ID3DBlob> assembly;
        require(
            D3DDisassemble(
                bytecode.data(),
                bytecode.size_bytes(),
                0,
                nullptr,
                &assembly),
            "D3DDisassemble(pixel shader)");
        const std::string_view text{
            static_cast<const char*>(assembly->GetBufferPointer()),
            assembly->GetBufferSize(),
        };
        const auto expectedRegister = "t" + std::to_string(slot);
        std::size_t lineStart = 0;
        while (lineStart < text.size()) {
            const auto lineEnd = text.find('\n', lineStart);
            auto line = text.substr(
                lineStart,
                lineEnd == std::string_view::npos ?
                    text.size() - lineStart : lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            const auto lastSpace = line.find_last_of(' ');
            if (line.find("dcl_resource_") != std::string_view::npos &&
                lastSpace != std::string_view::npos &&
                line.substr(lastSpace + 1) == expectedRegister) {
                return true;
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return false;
    }

    [[nodiscard]] ComPtr<ID3DBlob> compileVertexShader(
        bool hasVertexColor,
        bool isInstanced,
        bool usesTessellatedInputs,
        bool hasLandscapeLod,
        bool hasBoneTint,
        bool hasPipboyScreen,
        bool hasFaceDetail,
        bool hasDismemberment,
        bool hasCombinedMaterial,
        bool hasLandscapeLayers,
        bool hasInstancedLandscapeLayers,
        bool useDismemberment,
        bool hasMeatCuff,
        std::uint32_t meatCuffCase,
        std::uint32_t eyeIndex)
    {
        constexpr std::string_view source = R"(
struct VSOutput
{
    float4 position : SV_POSITION;
#if USES_TESSELLATED_INPUTS || HAS_DISMEMBERMENT
    float2 uv : TEXCOORD0;
#if HAS_DISMEMBERMENT
    float2 dismembermentUv : TEXCOORD4;
#endif
#if HAS_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
    float3 tangent : TEXCOORD1;
    float3 bitangent : TEXCOORD2;
    float3 normal : TEXCOORD3;
#if HAS_DISMEMBERMENT
    float2 dismembermentSelector : TEXCOORD5;
#endif
#if USES_TESSELLATED_INPUTS
    float4 currentPosition : POSITION1;
    float4 previousPosition : POSITION2;
#endif
#else
    float3 tangent : TEXCOORD0;
    float3 bitangent : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 currentPosition : TEXCOORD3;
    float4 previousPosition : TEXCOORD4;
#if HAS_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#endif
#if HAS_INSTANCED_LANDSCAPE_LAYERS
    nointerpolation uint landscapeRecordIndex : COLOR2;
#endif
#if HAS_LANDSCAPE_LAYERS
    float2 lodMultiplierUv : TEXCOORD6;
    float4 landscapeLayerWeights : TEXCOORD7;
    float3 landscapeLodCoordinatesAndBlend : TEXCOORD8;
#endif
#if HAS_MEAT_CUFF
    float2 cuffIndex : TEXCOORD6;
    float3 cuffOrientation : TEXCOORD7;
    float3 cuffBasisX : TEXCOORD8;
    float3 cuffBasisY : TEXCOORD9;
#endif
#if HAS_PIPBOY_SCREEN
    float3 screenDirection : TEXCOORD6;
#elif HAS_FACE_DETAIL
    float faceFactor : TEXCOORD6;
#endif
#if HAS_BONE_TINT
    float4 boneTintColor : COLOR1;
#endif
#if IS_INSTANCED || HAS_COMBINED_MATERIAL
    nointerpolation uint instanceDataIndex : COLOR2;
#endif
#if HAS_LANDSCAPE_LOD && !IS_INSTANCED && !HAS_LANDSCAPE_LAYERS
    float2 landscapeLodCoordinates : TEXCOORD9;
#endif
    nointerpolation uint eyeIndex : EYEINDEX;
#if HAS_LANDSCAPE_LOD && IS_INSTANCED && !HAS_LANDSCAPE_LAYERS
    float2 landscapeLodCoordinates : TEXCOORD9;
#endif
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    output.position = float4(positions[vertexId], 0.5, 1.0);
#if USES_TESSELLATED_INPUTS || HAS_DISMEMBERMENT
    output.uv = float2(0.25, 0.75);
#if HAS_DISMEMBERMENT
    output.dismembermentUv = float2(0.75, 0.25);
    output.dismembermentSelector = float2(
        0.0,
        USE_DISMEMBERMENT_LAYER ? 1.0 : -1.0);
#endif
    output.tangent = float3(1.0, 0.0, 0.0);
    output.bitangent = float3(0.0, 1.0, 0.0);
    output.normal = float3(0.0, 0.0, -1.0);
#if USES_TESSELLATED_INPUTS
    output.currentPosition = float4(0.2, -0.3, 0.4, 1.0);
    output.previousPosition = float4(0.15, -0.2, 0.35, 1.0);
#endif
#else
    output.tangent = float3(1.0, 0.0, 0.0);
    output.bitangent = float3(0.0, 1.0, 0.0);
    output.normal = float3(0.0, 0.0, -1.0);
    output.currentPosition = float4(0.2, -0.3, 0.4, 0.25);
    output.previousPosition = float4(0.15, -0.2, 0.35, 0.75);
#endif
#if HAS_VERTEX_COLOR
    output.vertexColor = float4(0.8, 0.7, 0.6, 0.9);
#endif
#if HAS_INSTANCED_LANDSCAPE_LAYERS
    output.landscapeRecordIndex = 0;
#endif
#if HAS_LANDSCAPE_LAYERS
    output.lodMultiplierUv = float2(0.25, 0.75);
    output.landscapeLayerWeights = float4(0.4, 0.3, 0.2, 0.1);
    output.landscapeLodCoordinatesAndBlend =
        float3(128.0, 256.0, 0.35);
#endif
#if HAS_MEAT_CUFF
    output.cuffIndex = float2(TEST_MEAT_CUFF_INDEX, 0.0);
    output.cuffOrientation = float3(
        0.6,
        TEST_MEAT_CUFF_ORIENTATION_Y,
        0.0);
    output.cuffBasisX = float3(1.0, 0.0, 0.0);
    output.cuffBasisY = float3(0.0, 1.0, 0.0);
#endif
#if HAS_PIPBOY_SCREEN
    output.screenDirection = float3(0.2, -0.4, -1.0);
#elif HAS_FACE_DETAIL
    output.faceFactor = 0.6;
#endif
#if HAS_BONE_TINT
    output.boneTintColor = float4(0.2, 0.3, 0.4, 0.625);
#endif
#if IS_INSTANCED || HAS_COMBINED_MATERIAL
    output.instanceDataIndex = IS_INSTANCED ? 2 : 0;
#endif
#if HAS_LANDSCAPE_LOD && !HAS_LANDSCAPE_LAYERS
    output.landscapeLodCoordinates = float2(128.0, 256.0);
#endif
    output.eyeIndex = TEST_EYE_INDEX;
    return output;
}
)";

        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto meatCuffIndex = meatCuffCase == 2 ? "-1.0" :
            (meatCuffCase == 3 ? "11.0" : "3.0");
        const auto meatCuffOrientationY = meatCuffCase == 1 ? "-0.8" : "0.8";
        const D3D_SHADER_MACRO macros[]{
            { "HAS_VERTEX_COLOR", hasVertexColor ? "1" : "0" },
            { "IS_INSTANCED", isInstanced ? "1" : "0" },
            { "USES_TESSELLATED_INPUTS",
                usesTessellatedInputs ? "1" : "0" },
            { "HAS_LANDSCAPE_LOD", hasLandscapeLod ? "1" : "0" },
            { "HAS_BONE_TINT", hasBoneTint ? "1" : "0" },
            { "HAS_PIPBOY_SCREEN", hasPipboyScreen ? "1" : "0" },
            { "HAS_FACE_DETAIL", hasFaceDetail ? "1" : "0" },
            { "HAS_DISMEMBERMENT", hasDismemberment ? "1" : "0" },
            { "HAS_COMBINED_MATERIAL", hasCombinedMaterial ? "1" : "0" },
            { "HAS_LANDSCAPE_LAYERS", hasLandscapeLayers ? "1" : "0" },
            { "HAS_INSTANCED_LANDSCAPE_LAYERS",
                hasInstancedLandscapeLayers ? "1" : "0" },
            { "USE_DISMEMBERMENT_LAYER", useDismemberment ? "1" : "0" },
            { "HAS_MEAT_CUFF", hasMeatCuff ? "1" : "0" },
            { "TEST_MEAT_CUFF_INDEX", meatCuffIndex },
            { "TEST_MEAT_CUFF_ORIENTATION_Y", meatCuffOrientationY },
            { "TEST_EYE_INDEX", eyeIndex == 0 ? "0" : "1" },
            { nullptr, nullptr },
        };
        const auto result = D3DCompile(
            source.data(),
            source.size(),
            "LinearLightingParityVS",
            macros,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0,
            &bytecode,
            &errors);
        if (FAILED(result)) {
            const auto details = errors ? std::string(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize()) : std::string{};
            fail("vertex shader compilation failed: " + details);
        }
        return bytecode;
    }

    [[nodiscard]] ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device& device,
        std::span<const std::array<float, 4>> values)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(values.size_bytes());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = values.data();
        ComPtr<ID3D11Buffer> buffer;
        require(
            device.CreateBuffer(&description, &initial, &buffer),
            "CreateBuffer");
        return buffer;
    }

    struct CombinedMaterialData
    {
        std::array<float, 4> material;
        std::array<float, 4> emitColorAndAlphaReference;
        std::array<float, 4> interpolationAndProperties;
        std::array<float, 4> reserved;
        std::array<std::uint32_t, 4> textureSlices;
        std::uint32_t gradientTextureSlice;
        float gradientRow;
        float depth;
    };
    static_assert(sizeof(CombinedMaterialData) == 92);

    struct CombinedDepthData
    {
        std::array<float, 24> reserved;
        float value;
    };
    static_assert(sizeof(CombinedDepthData) == 100);

    struct LandscapeLayerIndicesData
    {
        std::array<float, 4> reserved0;
        std::array<float, 2> reserved1;
        std::uint32_t lodTextureSlice;
        std::uint32_t diffuse0;
        std::uint32_t normal0;
        std::uint32_t specular0;
        std::uint32_t diffuse1;
        std::uint32_t normal1;
        std::uint32_t specular1;
        std::uint32_t diffuse2;
        std::uint32_t normal2;
        std::uint32_t specular2;
        std::uint32_t diffuse3;
        std::uint32_t normal3;
        std::uint32_t specular3;
    };
    static_assert(sizeof(LandscapeLayerIndicesData) == 76);

    template <class T>
    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createStructuredBuffer(
        ID3D11Device& device,
        std::span<const T> values)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(values.size_bytes());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        description.StructureByteStride = sizeof(T);
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = values.data();
        ComPtr<ID3D11Buffer> buffer;
        require(
            device.CreateBuffer(&description, &initial, &buffer),
            "CreateBuffer(structured input)");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = DXGI_FORMAT_UNKNOWN;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        viewDescription.Buffer.FirstElement = 0;
        viewDescription.Buffer.NumElements = static_cast<UINT>(values.size());
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(
                buffer.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(structured input)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device& device,
        const Pixel& pixel)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = pixel.data();
        initial.SysMemPitch = kFloat4Size;
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(input)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTexture2x2(
        ID3D11Device& device,
        const std::array<Pixel, 4>& pixels)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 2;
        description.Height = 2;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = pixels.data();
        initial.SysMemPitch = kFloat4Size * 2;
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(2x2 input)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView(2x2 input)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTextureArray(
        ID3D11Device& device,
        const Pixel& pixel)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = pixel.data();
        initial.SysMemPitch = kFloat4Size;
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(array input)");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDescription.Texture2DArray.MostDetailedMip = 0;
        viewDescription.Texture2DArray.MipLevels = 1;
        viewDescription.Texture2DArray.FirstArraySlice = 0;
        viewDescription.Texture2DArray.ArraySize = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(array input)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTextureArray(
        ID3D11Device& device,
        const std::array<Pixel, 4>& pixels)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = static_cast<UINT>(pixels.size());
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::array<D3D11_SUBRESOURCE_DATA, 4> initial{};
        for (std::size_t index = 0; index < initial.size(); ++index) {
            initial[index].pSysMem = pixels[index].data();
            initial[index].SysMemPitch = kFloat4Size;
        }
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, initial.data(), &texture),
            "CreateTexture2D(four-slice array input)");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDescription.Texture2DArray.MostDetailedMip = 0;
        viewDescription.Texture2DArray.MipLevels = 1;
        viewDescription.Texture2DArray.FirstArraySlice = 0;
        viewDescription.Texture2DArray.ArraySize = description.ArraySize;
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(four-slice array input)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createTexture2x2Array(
        ID3D11Device& device,
        const std::array<Pixel, 4>& pixels)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 2;
        description.Height = 2;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = pixels.data();
        initial.SysMemPitch = kFloat4Size * 2;
        ComPtr<ID3D11Texture2D> texture;
        require(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(2x2 array input)");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDescription.Texture2DArray.MostDetailedMip = 0;
        viewDescription.Texture2DArray.MipLevels = 1;
        viewDescription.Texture2DArray.FirstArraySlice = 0;
        viewDescription.Texture2DArray.ArraySize = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(2x2 array input)");
        return view;
    }

    [[nodiscard]] RenderTargets createRenderTargets(ID3D11Device& device)
    {
        RenderTargets result;
        D3D11_TEXTURE2D_DESC gpuDescription{};
        gpuDescription.Width = 1;
        gpuDescription.Height = 1;
        gpuDescription.MipLevels = 1;
        gpuDescription.ArraySize = 1;
        gpuDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        gpuDescription.SampleDesc.Count = 1;
        gpuDescription.Usage = D3D11_USAGE_DEFAULT;
        gpuDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

        auto stagingDescription = gpuDescription;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            require(
                device.CreateTexture2D(
                    &gpuDescription,
                    nullptr,
                    &result.gpu[index]),
                "CreateTexture2D(render target)");
            require(
                device.CreateRenderTargetView(
                    result.gpu[index].Get(),
                    nullptr,
                    &result.views[index]),
                "CreateRenderTargetView");
            require(
                device.CreateTexture2D(
                    &stagingDescription,
                    nullptr,
                    &result.staging[index]),
                "CreateTexture2D(staging)");
        }
        return result;
    }

    [[nodiscard]] RenderResult readRenderTargets(
        ID3D11DeviceContext& context,
        const RenderTargets& targets)
    {
        RenderResult result{};
        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            context.CopyResource(
                targets.staging[index].Get(),
                targets.gpu[index].Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(
                context.Map(
                    targets.staging[index].Get(),
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped),
                "Map(render target)");
            std::memcpy(
                result[index].data(),
                mapped.pData,
                sizeof(result[index]));
            context.Unmap(targets.staging[index].Get(), 0);
        }
        return result;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 12> makeMaterialData(
        UINT mrtCount,
        std::size_t caseIndex,
        bool hasAdditionalAlphaMask,
        bool hasGradientRemap,
        bool hasGradientHair,
        bool hasBoneTint,
        bool hasSkinTint,
        bool hasStandaloneHair,
        bool hasDismemberment,
        bool hasMeatCuff,
        bool hasCombinedMaterial,
        bool hasLandscapeLayers,
        AdditionalAlphaCase additionalAlphaCase,
        MeatCuffAlphaCase meatCuffAlphaCase)
    {
        std::array<std::array<float, 4>, 12> values{};
        const auto switchValue = caseIndex == 0 ? 0.0F : 0.35F;
        values[0] = { 0.4F, 0.7F, 0.25F, 0.8F };
        values[1] = { 0.3F, 0.45F, 0.6F, 0.2F };
        values[2] = mrtCount == 5 ?
            std::array<float, 4>{ 0.75F, 1.0F, 0.0F, 0.0F } :
            std::array<float, 4>{ 0.9F, 0.6F, 0.0F, 0.0F };
        values[3] = { 0.85F, 0.55F, -1.0F, 0.0F };
        values[4] = { 1.0F, 1.0F, 0.4F, caseIndex == 2 ? -1.0F : 0.6F };
        const std::array<float, 4> depthParameters{
            0.65F,
            switchValue,
            0.2F,
            0.9F,
        };
        std::array<float, 4> maskParameters{};
        if (hasAdditionalAlphaMask) {
            switch (additionalAlphaCase) {
            case AdditionalAlphaCase::disabled:
                break;
            case AdditionalAlphaCase::texturePass:
                maskParameters = { 0.8F, 0.0F, 0.0F, 1.0F };
                break;
            case AdditionalAlphaCase::textureReject:
                maskParameters = { 0.2F, 0.0F, 0.0F, 1.0F };
                break;
            case AdditionalAlphaCase::noisePass:
                maskParameters = { 0.0F, 1.0F, 1.0F, 0.0F };
                break;
            case AdditionalAlphaCase::noiseReject:
                maskParameters = { 0.0F, 1.0F, 0.0F, 0.0F };
                break;
            }
        }
        if (hasCombinedMaterial) {
            const auto flagsIndex =
                (hasGradientRemap || mrtCount == 5) ? 5u : 4u;
            values[flagsIndex] = { 1.0F, 1.0F, 0.0F, 0.0F };
            if (hasAdditionalAlphaMask) {
                values[flagsIndex + 1u] = maskParameters;
            }
            return values;
        }
        if (hasLandscapeLayers) {
            values[3] = { 1.0F, 0.0F, 0.0F, 0.0F };
            for (std::size_t index = 4; index < 8; ++index) {
                values[index] = {
                    1.0F,
                    1.0F,
                    0.4F,
                    caseIndex == 2 ? -1.0F : 0.6F,
                };
            }
            values[8] = depthParameters;
            return values;
        }
        if (hasDismemberment) {
            if (mrtCount == 5) {
                values[5] = values[4];
                values[4] = {};
                values[6] = { 1.0F, 0.0F, 0.0F, 0.0F };
                values[7] = { 0.0F, 1.0F, 0.0F, 0.0F };
                values[8] = { 0.0F, 0.0F, -1.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[10] = maskParameters;
                    values[11] = depthParameters;
                } else {
                    values[10] = depthParameters;
                }
                return values;
            }
            if (hasSkinTint || hasGradientRemap) {
                values[2] = hasSkinTint ? kSkinTintColor :
                    std::array<float, 4>{ 0.55F, 0.0F, 0.0F, 0.0F };
                values[3] = { 0.9F, 0.6F, 0.0F, 0.0F };
                values[5] = values[4];
                values[6] = { 1.0F, 0.0F, 0.0F, 0.0F };
                values[7] = { 0.0F, 1.0F, 0.0F, 0.0F };
                values[8] = { 0.0F, 0.0F, -1.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[10] = maskParameters;
                    values[11] = depthParameters;
                } else {
                    values[10] = depthParameters;
                }
            } else {
                values[5] = { 1.0F, 0.0F, 0.0F, 0.0F };
                values[6] = { 0.0F, 1.0F, 0.0F, 0.0F };
                values[7] = { 0.0F, 0.0F, -1.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[9] = maskParameters;
                    values[10] = depthParameters;
                } else {
                    values[9] = depthParameters;
                }
            }
            return values;
        }
        if (hasMeatCuff) {
            values[2][0] = meatCuffAlphaCase == MeatCuffAlphaCase::pass ?
                values[2][0] : 0.01F;
            if (hasSkinTint || hasGradientRemap) {
                values[6] = values[4];
                values[4] = values[3];
                values[3] = hasSkinTint ? kSkinTintColor :
                    std::array<float, 4>{ 0.55F, 0.0F, 0.0F, 0.0F };
                values[5] = {};
                values[7] = { 1.0F, 0.0F, 0.0F, 0.0F };
                values[8] = { 0.0F, 1.0F, 0.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[9] = maskParameters;
                    values[10] = depthParameters;
                } else {
                    values[9] = depthParameters;
                }
            } else {
                values[5] = values[4];
                values[4] = {};
                values[6] = { 1.0F, 0.0F, 0.0F, 0.0F };
                values[7] = { 0.0F, 1.0F, 0.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[8] = maskParameters;
                    values[9] = depthParameters;
                } else {
                    values[8] = depthParameters;
                }
            }
            return values;
        }
        if (hasGradientRemap) {
            if (mrtCount == 5) {
                values[2] = {
                    hasGradientHair ? 0.022F : 0.75F,
                    1.0F,
                    0.0F,
                    0.0F,
                };
                values[3] = { 0.55F, 0.0F, 0.0F, 0.0F };
                values[4] = { 0.9F, 0.6F, 0.0F, 0.0F };
                values[6] = {
                    1.0F,
                    1.0F,
                    0.4F,
                    caseIndex == 2 ? -1.0F : 0.6F,
                };
                if (hasAdditionalAlphaMask) {
                    values[7] = maskParameters;
                    values[8] = depthParameters;
                } else if (hasBoneTint) {
                    values[7] = { 1.25F, 0.0F, 0.0F, 0.0F };
                    values[8] = depthParameters;
                } else {
                    values[7] = depthParameters;
                }
                return values;
            }
            values[2] = { 0.55F, 0.0F, 0.0F, 0.0F };
            values[3] = { 0.9F, 0.6F, 0.0F, 0.0F };
            values[4] = {};
            values[5] = {
                1.0F,
                1.0F,
                0.4F,
                caseIndex == 2 ? -1.0F : 0.6F,
            };
            if (hasAdditionalAlphaMask && hasBoneTint) {
                values[6] = maskParameters;
                values[7] = { 1.25F, 0.0F, 0.0F, 0.0F };
                values[8] = depthParameters;
            } else if (hasAdditionalAlphaMask) {
                values[6] = maskParameters;
                values[7] = depthParameters;
            } else if (hasBoneTint) {
                values[6] = { 1.25F, 0.0F, 0.0F, 0.0F };
                values[7] = depthParameters;
            } else {
                values[6] = depthParameters;
            }
            return values;
        }
        if (hasStandaloneHair) {
            if (mrtCount == 5) {
                values[6] = values[4];
                values[4] = { 0.85F, 0.55F, -1.0F, 0.0F };
                if (hasAdditionalAlphaMask) {
                    values[7] = maskParameters;
                    values[8] = depthParameters;
                } else {
                    values[7] = depthParameters;
                }
            } else {
                values[5] = values[4];
                if (hasAdditionalAlphaMask) {
                    values[6] = maskParameters;
                    values[7] = depthParameters;
                } else {
                    values[6] = depthParameters;
                }
            }
            return values;
        }
        if (hasSkinTint) {
            if (mrtCount == 5) {
                values[3] = kSkinTintColor;
                values[4] = { 0.85F, 0.55F, -1.0F, 0.0F };
                values[6] = {
                    1.0F,
                    1.0F,
                    0.4F,
                    caseIndex == 2 ? -1.0F : 0.6F,
                };
                values[7] = depthParameters;
                return values;
            }
            values[2] = kSkinTintColor;
            values[3] = { 0.85F, 0.55F, -1.0F, 0.0F };
            values[4] = {};
            values[5] = {
                1.0F,
                1.0F,
                0.4F,
                caseIndex == 2 ? -1.0F : 0.6F,
            };
            if (hasAdditionalAlphaMask && hasBoneTint) {
                values[6] = maskParameters;
                values[7] = { 1.25F, 0.0F, 0.0F, 0.0F };
                values[8] = depthParameters;
            } else if (hasAdditionalAlphaMask) {
                values[6] = maskParameters;
                values[7] = depthParameters;
            } else if (hasBoneTint) {
                values[6] = { 1.25F, 0.0F, 0.0F, 0.0F };
                values[7] = depthParameters;
            } else {
                values[6] = depthParameters;
            }
            return values;
        }
        if (hasBoneTint) {
            if (mrtCount == 5) {
                values[5] = values[4];
                if (hasAdditionalAlphaMask) {
                    values[6] = maskParameters;
                    values[7] = { 1.25F, 0.0F, 0.0F, 0.0F };
                    values[8] = depthParameters;
                } else {
                    values[6] = { 1.25F, 0.0F, 0.0F, 0.0F };
                    values[7] = depthParameters;
                }
            } else {
                if (hasAdditionalAlphaMask) {
                    values[5] = maskParameters;
                    values[6] = { 1.25F, 0.0F, 0.0F, 0.0F };
                    values[7] = depthParameters;
                } else {
                    values[5] = { 1.25F, 0.0F, 0.0F, 0.0F };
                    values[6] = depthParameters;
                }
            }
            return values;
        }
        if (hasAdditionalAlphaMask) {
            if (mrtCount == 5) {
                values[5] = values[4];
                values[6] = maskParameters;
                values[7] = depthParameters;
            } else {
                values[5] = maskParameters;
                values[6] = depthParameters;
            }
        } else {
            values[5] = mrtCount == 5 ? values[4] : depthParameters;
            values[6] = depthParameters;
        }
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 71> makeGeometryData(
        std::size_t caseIndex)
    {
        std::array<std::array<float, 4>, 71> values{};
        values[50][0] = caseIndex == 0 ? 0.0F :
            (caseIndex == 1 ? 0.35F : 0.8F);
        values[51] = { 1.0F, 0.0F, 0.0F, 0.0F };
        values[52] = { 0.0F, 1.0F, 0.0F, 0.0F };
        values[54] = { 0.0F, 0.0F, 0.0F, 1.0F };
        values[55] = { 0.8F, 0.1F, 0.0F, 0.0F };
        values[56] = { -0.2F, 1.1F, 0.0F, 0.0F };
        values[58] = { 0.0F, 0.0F, 0.0F, 1.0F };
        values[63] = { 1.0F, 0.0F, 0.0F, 0.0F };
        values[64] = { 0.0F, 1.0F, 0.0F, 0.0F };
        values[66] = { 0.0F, 0.0F, 0.0F, 1.0F };
        values[67] = { 1.2F, -0.15F, 0.0F, 0.0F };
        values[68] = { 0.25F, 0.9F, 0.0F, 0.0F };
        values[70] = { 0.0F, 0.0F, 0.0F, 1.0F };
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 900> makeInstanceData()
    {
        std::array<std::array<float, 4>, 900> values{};
        constexpr std::size_t instanceBase = 2 * 6;
        values[instanceBase + 4] = { 0.4F, 0.7F, 0.65F, 0.8F };
        values[instanceBase + 5] = kInstanceEmitColor;
        return values;
    }

    [[nodiscard]] std::array<std::array<float, 4>, 7> makeFrameData(
        const LinearLightingCase& lightingCase)
    {
        const auto uintAsFloat = [] (std::uint32_t value) {
            return std::bit_cast<float>(value);
        };
        return {
            std::array<float, 4>{
                uintAsFloat(lightingCase.enabled ? 1u : 0u),
                uintAsFloat(1u),
                2.5F,
                1.9F,
            },
            std::array<float, 4>{
                lightingCase.colorGamma,
                lightingCase.emitColorGamma,
                lightingCase.glowmapGamma,
                1.7F,
            },
            std::array<float, 4>{ 1.6F, 1.5F, 1.4F, 1.3F },
            std::array<float, 4>{
                1.2F,
                1.1F,
                1.05F,
                lightingCase.vanillaDiffuseColorMult,
            },
            std::array<float, 4>{
                0.85F,
                0.75F,
                0.65F,
                lightingCase.emitColorMult,
            },
            std::array<float, 4>{
                lightingCase.glowmapMult,
                0.35F,
                0.25F,
                0.15F,
            },
            std::array<float, 4>{ 0.05F, 0.0F, 0.0F, 0.0F },
        };
    }

    [[nodiscard]] RenderResult render(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        ID3D11VertexShader& vertexShader,
        ID3D11PixelShader& pixelShader,
        UINT mrtCount,
        bool isInstanced,
        std::size_t caseIndex,
        const LinearLightingCase& lightingCase,
        bool hasAdditionalAlphaMask,
        bool hasLandscapeLod,
        bool hasGradientRemap,
        bool hasGradientHair,
        bool hasBoneTint,
        bool hasPipboyScreen,
        bool hasSkinTint,
        bool hasStandaloneHair,
        bool hasDismemberment,
        bool hasMeatCuff,
        bool hasCombinedMaterial,
        bool hasLandscapeLayers,
        bool hasInstancedLandscapeLayers,
        AdditionalAlphaCase additionalAlphaCase =
            AdditionalAlphaCase::disabled,
        MeatCuffAlphaCase meatCuffAlphaCase = MeatCuffAlphaCase::pass)
    {
        const auto materialData = makeMaterialData(
            mrtCount,
            caseIndex,
            hasAdditionalAlphaMask,
            hasGradientRemap,
            hasGradientHair,
            hasBoneTint,
            hasSkinTint,
            hasStandaloneHair,
            hasDismemberment,
            hasMeatCuff,
            hasCombinedMaterial,
            hasLandscapeLayers,
            additionalAlphaCase,
            meatCuffAlphaCase);
        const auto geometryData = makeGeometryData(caseIndex);
        const auto instanceData = makeInstanceData();
        const auto frameData = makeFrameData(lightingCase);
        const std::array<std::array<float, 4>, 1> linearGeometryData{
            std::array<float, 4>{
                lightingCase.emissiveMult,
                0.0F,
                0.0F,
                0.0F,
            },
        };
        const auto materialBuffer = createConstantBuffer(device, materialData);
        const auto geometryBuffer = createConstantBuffer(device, geometryData);
        const auto instanceBuffer = createConstantBuffer(device, instanceData);
        const auto frameBuffer = createConstantBuffer(device, frameData);
        const auto linearGeometryBuffer = createConstantBuffer(
            device,
            linearGeometryData);
        const std::array<std::array<float, 4>, 1> landscapeLodGlobalsData{
            std::array<float, 4>{ 0.0F, 0.0F, 64.0F, -32.0F },
        };
        const std::array<std::array<float, 4>, 1> pipboyScreenGlobalsData{
            std::array<float, 4>{ 0.2F, 0.5F, 0.7F, 0.4F },
        };
        const auto landscapeLodGlobalsBuffer = createConstantBuffer(
            device,
            landscapeLodGlobalsData);
        const auto pipboyScreenGlobalsBuffer = createConstantBuffer(
            device,
            pipboyScreenGlobalsData);

        const std::array combinedMaterialData{
            CombinedMaterialData{
                .material = { 0.4F, 0.7F, 0.25F, 0.8F },
                .emitColorAndAlphaReference = {
                    kEmitColor[0],
                    kEmitColor[1],
                    kEmitColor[2],
                    0.2F,
                },
                .interpolationAndProperties = {
                    0.85F,
                    0.55F,
                    0.4F,
                    caseIndex == 2 ? -1.0F : 0.6F,
                },
                .reserved = {},
                .textureSlices = {},
                .gradientTextureSlice = 0,
                .gradientRow = 0.55F,
                .depth = 0.9F,
            },
        };
        const std::array combinedDepthData{
            CombinedDepthData{
                .reserved = {},
                .value = 0.65F,
            },
        };
        const auto combinedMaterialView = createStructuredBuffer(
            device,
            std::span<const CombinedMaterialData>{
                combinedMaterialData.data(),
                combinedMaterialData.size(),
            });
        const auto combinedDepthView = createStructuredBuffer(
            device,
            std::span<const CombinedDepthData>{
                combinedDepthData.data(),
                combinedDepthData.size(),
            });
        const std::array landscapeLayerIndicesData{
            LandscapeLayerIndicesData{
                .reserved0 = {},
                .reserved1 = {},
                .lodTextureSlice = 2,
                .diffuse0 = 0,
                .normal0 = 0,
                .specular0 = 0,
                .diffuse1 = 1,
                .normal1 = 1,
                .specular1 = 1,
                .diffuse2 = 2,
                .normal2 = 2,
                .specular2 = 2,
                .diffuse3 = 3,
                .normal3 = 3,
                .specular3 = 3,
            },
        };
        const auto landscapeLayerIndicesView = createStructuredBuffer(
            device,
            std::span<const LandscapeLayerIndicesData>{
                landscapeLayerIndicesData.data(),
                landscapeLayerIndicesData.size(),
            });

        const std::array<Pixel, 4> texturePixels{
            kDiffuseTexture,
            kNormalTexture,
            kSpecularTexture,
            kGlowTexture,
        };
        std::array<ComPtr<ID3D11ShaderResourceView>, 4> textureViews;
        std::array<ID3D11ShaderResourceView*, 4> rawTextureViews{};
        for (std::size_t index = 0; index < textureViews.size(); ++index) {
            textureViews[index] = hasCombinedMaterial && index < 3 ?
                createTextureArray(device, texturePixels[index]) :
                createTexture(device, texturePixels[index]);
            rawTextureViews[index] = textureViews[index].Get();
        }
        std::array<ComPtr<ID3D11ShaderResourceView>, 12>
            landscapeLayerViews;
        std::array<ID3D11ShaderResourceView*, 12> rawLandscapeLayerViews{};
        for (std::size_t index = 0; index < 4; ++index) {
            landscapeLayerViews[index] = createTexture(
                device, kLandscapeDiffuseLayers[index]);
            landscapeLayerViews[index + 4] = createTexture(
                device, kLandscapeNormalLayers[index]);
            landscapeLayerViews[index + 8] = createTexture(
                device, kLandscapeSpecularLayers[index]);
        }
        for (std::size_t index = 0; index < landscapeLayerViews.size();
             ++index) {
            rawLandscapeLayerViews[index] = landscapeLayerViews[index].Get();
        }
        const std::array<ComPtr<ID3D11ShaderResourceView>, 4>
            instancedLandscapeLayerViews{
                createTextureArray(device, kLandscapeDiffuseLayers),
                createTextureArray(device, kLandscapeNormalLayers),
                createTextureArray(device, kLandscapeSpecularLayers),
                createTextureArray(
                    device,
                    std::array<Pixel, 4>{
                        Pixel{ 0.3F, 0.4F, 0.5F, 1.0F },
                        Pixel{ 0.6F, 0.7F, 0.8F, 1.0F },
                        kLandscapeLodMultiplier,
                        Pixel{ 0.2F, 0.3F, 0.4F, 1.0F },
                    }),
            };
        const std::array<ID3D11ShaderResourceView*, 4>
            rawInstancedLandscapeLayerViews{
                instancedLandscapeLayerViews[0].Get(),
                instancedLandscapeLayerViews[1].Get(),
                instancedLandscapeLayerViews[2].Get(),
                instancedLandscapeLayerViews[3].Get(),
            };
        const auto additionalAlphaTexture = createTexture(
            device,
            kAdditionalAlphaTexture);
        const auto additionalAlphaNoise = createTexture(
            device,
            kAdditionalAlphaNoise);
        const auto landscapeLodDiffuse = createTexture(
            device,
            kLandscapeLodDiffuse);
        const auto landscapeLodNormal = createTexture(
            device,
            kLandscapeLodNormal);
        const auto gradientRemapTexture = createTexture2x2(
            device,
            kGradientRemapTexture);
        const auto combinedGradientRemapTexture = createTexture2x2Array(
            device,
            kGradientRemapTexture);
        const auto boneTintLookup = createTexture(device, kBoneTintLookup);
        const auto boneTintPalette = createTexture2x2(
            device,
            kBoneTintPalette);
        const auto screenTexture = createTexture2x2(device, kScreenTexture);
        const auto faceDetailTexture = createTexture(
            device,
            kFaceDetailTexture);
        const auto dismembermentDiffuseTexture = createTexture2x2(
            device,
            kDismembermentDiffuseTexture);
        const auto dismembermentNormalTexture = createTexture2x2(
            device,
            kDismembermentNormalTexture);
        const auto dismembermentSpecularTexture = createTexture2x2(
            device,
            kDismembermentSpecularTexture);

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device.CreateSamplerState(&samplerDescription, &sampler),
            "CreateSamplerState");
        const std::array<ID3D11SamplerState*, 4> samplers{
            sampler.Get(),
            sampler.Get(),
            sampler.Get(),
            sampler.Get(),
        };

        auto targets = createRenderTargets(device);
        std::array<ID3D11RenderTargetView*, kRenderTargetCount> rawTargets{};
        for (UINT index = 0; index < kRenderTargetCount; ++index) {
            rawTargets[index] = targets.views[index].Get();
            context.ClearRenderTargetView(
                rawTargets[index],
                kClearColor.data());
        }

        constexpr D3D11_VIEWPORT viewport{
            0.0F,
            0.0F,
            1.0F,
            1.0F,
            0.0F,
            1.0F,
        };
        context.OMSetRenderTargets(
            kRenderTargetCount,
            rawTargets.data(),
            nullptr);
        context.RSSetViewports(1, &viewport);
        context.IASetInputLayout(nullptr);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.VSSetShader(&vertexShader, nullptr, 0);
        context.PSSetShader(&pixelShader, nullptr, 0);
        if (hasLandscapeLayers) {
            if (hasInstancedLandscapeLayers) {
                context.PSSetShaderResources(
                    0,
                    static_cast<UINT>(
                        rawInstancedLandscapeLayerViews.size()),
                    rawInstancedLandscapeLayerViews.data());
            } else {
                context.PSSetShaderResources(
                    0,
                    static_cast<UINT>(rawLandscapeLayerViews.size()),
                    rawLandscapeLayerViews.data());
            }
        } else {
            context.PSSetShaderResources(
                0,
                static_cast<UINT>(rawTextureViews.size()),
                rawTextureViews.data());
        }
        std::array<ID3D11SamplerState*, 16> allSamplers{};
        allSamplers.fill(sampler.Get());
        context.PSSetSamplers(
            0,
            hasLandscapeLayers ? 12u : static_cast<UINT>(samplers.size()),
            hasLandscapeLayers ? allSamplers.data() : samplers.data());
        auto* rawScreenTexture = screenTexture.Get();
        auto* rawScreenSampler = sampler.Get();
        if (!hasLandscapeLayers) {
            context.PSSetShaderResources(4, 1, &rawScreenTexture);
            context.PSSetSamplers(4, 1, &rawScreenSampler);
        }
        auto* rawFaceDetailTexture = faceDetailTexture.Get();
        auto* rawFaceDetailSampler = sampler.Get();
        if (!hasLandscapeLayers) {
            context.PSSetShaderResources(8, 1, &rawFaceDetailTexture);
            context.PSSetSamplers(8, 1, &rawFaceDetailSampler);
        }
        auto* rawAdditionalAlphaTexture = additionalAlphaTexture.Get();
        auto* rawAdditionalAlphaNoise = additionalAlphaNoise.Get();
        context.PSSetShaderResources(12, 1, &rawAdditionalAlphaTexture);
        context.PSSetShaderResources(15, 1, &rawAdditionalAlphaNoise);
        auto* rawAdditionalAlphaSampler = sampler.Get();
        context.PSSetSamplers(12, 1, &rawAdditionalAlphaSampler);
        auto* rawLandscapeLodDiffuse = landscapeLodDiffuse.Get();
        auto* rawLandscapeLodNormal = landscapeLodNormal.Get();
        auto* rawLandscapeLodSampler = sampler.Get();
        if (hasLandscapeLod) {
            context.PSSetShaderResources(13, 1, &rawLandscapeLodDiffuse);
            context.PSSetShaderResources(15, 1, &rawLandscapeLodNormal);
            context.PSSetSamplers(13, 1, &rawLandscapeLodSampler);
            context.PSSetSamplers(15, 1, &rawLandscapeLodSampler);
        }
        if (hasLandscapeLayers && !hasInstancedLandscapeLayers &&
            hasLandscapeLod) {
            const auto landscapeLodMultiplier = createTexture(
                device, kLandscapeLodMultiplier);
            auto* rawLandscapeLodMultiplier = landscapeLodMultiplier.Get();
            context.PSSetShaderResources(
                14, 1, &rawLandscapeLodMultiplier);
            context.PSSetSamplers(14, 1, &rawLandscapeLodSampler);
        }
        auto* rawGradientRemapTexture = gradientRemapTexture.Get();
        auto* rawGradientRemapSampler = sampler.Get();
        if (hasGradientRemap) {
            auto* gradientView = hasCombinedMaterial ?
                combinedGradientRemapTexture.Get() :
                rawGradientRemapTexture;
            context.PSSetShaderResources(5, 1, &gradientView);
            context.PSSetSamplers(5, 1, &rawGradientRemapSampler);
        }
        if (hasCombinedMaterial) {
            auto* rawCombinedMaterial = combinedMaterialView.Get();
            auto* rawCombinedDepth = combinedDepthView.Get();
            context.PSSetShaderResources(4, 1, &rawCombinedMaterial);
            context.PSSetShaderResources(6, 1, &rawCombinedDepth);
        }
        if (hasInstancedLandscapeLayers) {
            auto* rawLandscapeLayerIndices =
                landscapeLayerIndicesView.Get();
            context.PSSetShaderResources(
                4, 1, &rawLandscapeLayerIndices);
        }
        auto* rawBoneTintLookup = boneTintLookup.Get();
        auto* rawBoneTintPalette = boneTintPalette.Get();
        auto* rawBoneTintSampler = sampler.Get();
        if (hasBoneTint) {
            context.PSSetShaderResources(13, 1, &rawBoneTintLookup);
            context.PSSetShaderResources(14, 1, &rawBoneTintPalette);
            context.PSSetSamplers(13, 1, &rawBoneTintSampler);
            context.PSSetSamplers(14, 1, &rawBoneTintSampler);
        }
        if (hasDismemberment || hasMeatCuff) {
            const std::array<ID3D11ShaderResourceView*, 3>
                rawDismembermentTextures{
                    dismembermentDiffuseTexture.Get(),
                    dismembermentNormalTexture.Get(),
                    dismembermentSpecularTexture.Get(),
                };
            const std::array<ID3D11SamplerState*, 3> dismembermentSamplers{
                sampler.Get(),
                sampler.Get(),
                sampler.Get(),
            };
            context.PSSetShaderResources(
                9,
                static_cast<UINT>(rawDismembermentTextures.size()),
                rawDismembermentTextures.data());
            context.PSSetSamplers(
                9,
                static_cast<UINT>(dismembermentSamplers.size()),
                dismembermentSamplers.data());
        }

        auto* rawMaterial = materialBuffer.Get();
        auto* rawGeometry = geometryBuffer.Get();
        auto* rawFrame = frameBuffer.Get();
        auto* rawLinearGeometry = linearGeometryBuffer.Get();
        auto* rawInstance = instanceBuffer.Get();
        auto* rawLandscapeLodGlobals = landscapeLodGlobalsBuffer.Get();
        auto* rawPipboyScreenGlobals = pipboyScreenGlobalsBuffer.Get();
        if (hasLandscapeLod) {
            context.PSSetConstantBuffers(0, 1, &rawLandscapeLodGlobals);
        } else if (hasPipboyScreen) {
            context.PSSetConstantBuffers(0, 1, &rawPipboyScreenGlobals);
        }
        context.PSSetConstantBuffers(2, 1, &rawMaterial);
        context.PSSetConstantBuffers(5, 1, &rawFrame);
        context.PSSetConstantBuffers(8, 1, &rawLinearGeometry);
        context.PSSetConstantBuffers(12, 1, &rawGeometry);
        if (isInstanced) {
            context.PSSetConstantBuffers(13, 1, &rawInstance);
        }
        context.Draw(3, 0);

        const auto result = readRenderTargets(context, targets);
        constexpr std::array<ID3D11ShaderResourceView*, 16> nullViews{};
        constexpr std::array<ID3D11RenderTargetView*, kRenderTargetCount>
            nullTargets{};
        context.PSSetShaderResources(
            0,
            static_cast<UINT>(nullViews.size()),
            nullViews.data());
        constexpr std::array<ID3D11SamplerState*, 16> nullSamplers{};
        context.PSSetSamplers(
            0,
            static_cast<UINT>(nullSamplers.size()),
            nullSamplers.data());
        ID3D11ShaderResourceView* nullView{};
        context.PSSetShaderResources(4, 1, &nullView);
        context.PSSetShaderResources(6, 1, &nullView);
        context.PSSetShaderResources(8, 1, &nullView);
        context.PSSetShaderResources(12, 1, &nullView);
        context.PSSetShaderResources(15, 1, &nullView);
        ID3D11SamplerState* nullSampler{};
        context.PSSetSamplers(4, 1, &nullSampler);
        context.PSSetSamplers(8, 1, &nullSampler);
        context.PSSetSamplers(12, 1, &nullSampler);
        if (hasDismemberment || hasMeatCuff) {
            constexpr std::array<ID3D11ShaderResourceView*, 3>
                nullDismembermentTextures{};
            constexpr std::array<ID3D11SamplerState*, 3>
                nullDismembermentSamplers{};
            context.PSSetShaderResources(
                9,
                static_cast<UINT>(nullDismembermentTextures.size()),
                nullDismembermentTextures.data());
            context.PSSetSamplers(
                9,
                static_cast<UINT>(nullDismembermentSamplers.size()),
                nullDismembermentSamplers.data());
        }
        if (hasGradientRemap) {
            context.PSSetShaderResources(5, 1, &nullView);
            context.PSSetSamplers(5, 1, &nullSampler);
        }
        if (hasBoneTint) {
            context.PSSetShaderResources(13, 1, &nullView);
            context.PSSetShaderResources(14, 1, &nullView);
            context.PSSetSamplers(13, 1, &nullSampler);
            context.PSSetSamplers(14, 1, &nullSampler);
        }
        if (hasLandscapeLod) {
            context.PSSetShaderResources(13, 1, &nullView);
            context.PSSetSamplers(13, 1, &nullSampler);
            context.PSSetSamplers(15, 1, &nullSampler);
            ID3D11Buffer* nullBuffer{};
            context.PSSetConstantBuffers(0, 1, &nullBuffer);
        } else if (hasPipboyScreen) {
            ID3D11Buffer* nullBuffer{};
            context.PSSetConstantBuffers(0, 1, &nullBuffer);
        }
        if (isInstanced) {
            ID3D11Buffer* nullBuffer{};
            context.PSSetConstantBuffers(13, 1, &nullBuffer);
        }
        context.OMSetRenderTargets(
            kRenderTargetCount,
            nullTargets.data(),
            nullptr);
        return result;
    }

    [[nodiscard]] bool approximatelyEqual(float lhs, float rhs)
    {
        if (std::isnan(lhs) || std::isnan(rhs)) {
            return std::isnan(lhs) && std::isnan(rhs);
        }
        if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
            return false;
        }
        const auto difference = std::abs(lhs - rhs);
        const auto scale = (std::max)(std::abs(lhs), std::abs(rhs));
        return difference <= kAbsoluteTolerance + kRelativeTolerance * scale;
    }

    [[nodiscard]] bool isClearResult(const RenderResult& result)
    {
        for (const auto& target : result) {
            for (std::size_t channel = 0; channel < target.size(); ++channel) {
                if (!approximatelyEqual(target[channel], kClearColor[channel])) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::string compare(
        const ShaderContract& contract,
        std::string_view scenario,
        std::uint32_t eyeIndex,
        std::size_t caseIndex,
        const RenderResult& expected,
        const RenderResult& actual)
    {
        for (UINT target = 0; target < kRenderTargetCount; ++target) {
            for (UINT channel = 0; channel < 4; ++channel) {
                if (approximatelyEqual(
                        expected[target][channel],
                        actual[target][channel])) {
                    continue;
                }
                return std::string(contract.name) + " " +
                    std::string(scenario) + " case " +
                    std::to_string(caseIndex) + " eye " +
                    std::to_string(eyeIndex) + " differs at target " +
                    std::to_string(target) + " channel " +
                    std::to_string(channel) + ": expected=" +
                    std::to_string(expected[target][channel]) +
                    " actual=" + std::to_string(actual[target][channel]);
            }
        }
        return {};
    }

    [[nodiscard]] float transformedValue(
        float value,
        float gamma,
        float multiplier)
    {
        return std::pow(std::abs(value), gamma / 2.2F) * multiplier;
    }

    [[nodiscard]] float decodedDiffuseValue(
        float value,
        float multiplier)
    {
        return value * multiplier;
    }

    [[nodiscard]] float skinTintValue(
        float diffuse,
        float tint,
        float opacity)
    {
        const auto baseGamma = std::pow(std::abs(diffuse), 0.454545F);
        const auto tintGamma = std::pow(std::abs(tint), 0.454545F);
        const auto blendedGamma = tintGamma < 0.5F ?
            (2.0F * baseGamma * tintGamma) +
                (baseGamma * baseGamma * (1.0F - (2.0F * tintGamma))) :
            (std::sqrt(baseGamma) * ((2.0F * tintGamma) - 1.0F)) +
                (2.0F * baseGamma * (1.0F - tintGamma));
        const auto tinted = std::pow(std::abs(blendedGamma), 2.2F);
        return diffuse + ((tinted - diffuse) * opacity);
    }

    [[nodiscard]] RenderResult makeEnabledExpected(
        const ShaderContract& contract,
        std::size_t caseIndex,
        bool usesGlowmap,
        bool usesDiffuseTexture,
        bool useDismemberment,
        std::uint32_t surfaceVariant,
        const RenderResult& vanilla,
        const LinearLightingCase& lightingCase)
    {
        auto expected = vanilla;
        const auto geometrySwitch = caseIndex == 0 ? 0.0F :
            (caseIndex == 1 ? 0.35F : 0.8F);
        const auto fadeControl = caseIndex == 2 ? -1.0F : 0.6F;
        const auto fade = fadeControl == -1.0F ? 1.0F :
            (-fadeControl * geometrySwitch) + 1.0F;
        const auto& emitColor = contract.isInstanced ?
            kInstanceEmitColor : kEmitColor;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            float diffuse{};
            if (contract.hasLandscapeLayers) {
                for (std::size_t layer = 0;
                     layer < kLandscapeLayerWeights.size();
                     ++layer) {
                    diffuse += kLandscapeDiffuseLayers[layer][channel] *
                        kLandscapeLayerWeights[layer];
                }
                if (contract.hasLandscapeLod) {
                    const auto landscapeLodDiffuse =
                        (kLandscapeLodDiffuse[channel] * 3.777778F) -
                        2.006F;
                    const auto lodDiffuse =
                        kLandscapeLodMultiplier[channel] *
                        landscapeLodDiffuse;
                    diffuse += (lodDiffuse - diffuse) *
                        kLandscapeLodBlend;
                }
                diffuse *= kVertexColor[channel];
            } else if (contract.hasGradientRemap && !useDismemberment &&
                !contract.hasMeatCuff) {
                const auto gradientTexel = contract.hasVertexColor ? 1u : 3u;
                diffuse = kGradientRemapTexture[gradientTexel][channel];
                if (contract.hasGradientHair && contract.mrtCount == 5) {
                    diffuse *= kDiffuseTexture[1] * 1.8F;
                }
            } else {
                const auto meatCuffTexel = surfaceVariant == 0 ? 3u : 2u;
                diffuse = useDismemberment ?
                    kDismembermentDiffuseTexture[1][channel] :
                    (contract.hasMeatCuff ?
                            kDismembermentDiffuseTexture[meatCuffTexel][channel] :
                            (usesDiffuseTexture ?
                                    kDiffuseTexture[channel] : 1.0F));
                if (contract.hasLandscapeLod) {
                    diffuse *=
                        (kLandscapeLodDiffuse[channel] * 3.777778F) - 2.006F;
                }
                if (contract.hasVertexColor && !useDismemberment &&
                    !contract.hasMeatCuff) {
                    diffuse *= kVertexColor[channel];
                }
            }
            if (contract.hasSkinTint && !useDismemberment &&
                !contract.hasMeatCuff) {
                diffuse = skinTintValue(
                    diffuse,
                    kSkinTintColor[channel],
                    kSkinTintColor[3]);
            }
            if (contract.hasFaceDetail) {
                constexpr auto faceFactor = 0.6F;
                const auto faceDiffuseMask =
                    contract.faceUsesModelSpaceNormals ?
                    (kFaceDetailTexture[3] * 2.0F) - 1.0F :
                    kFaceDetailTexture[3];
                diffuse *= 1.0F -
                    ((1.0F - faceDiffuseMask) * faceFactor * 0.3F);
            }
            const auto transformedDiffuseSource =
                useDismemberment || contract.hasMeatCuff ?
                transformedValue(
                    diffuse,
                    lightingCase.colorGamma,
                    lightingCase.vanillaDiffuseColorMult) :
                decodedDiffuseValue(
                    diffuse,
                    lightingCase.vanillaDiffuseColorMult);
            auto transformedDiffuse = transformedDiffuseSource;
            constexpr auto screenTexel = 2u;
            if (contract.hasMenuScreen) {
                transformedDiffuse += transformedValue(
                    kScreenTexture[screenTexel][channel],
                    lightingCase.colorGamma,
                    lightingCase.vanillaDiffuseColorMult);
            }
            if (!(contract.hasStandaloneHair && contract.mrtCount == 5)) {
                expected[0][channel] = fade * transformedDiffuse;
            }
            const auto pipboyScreen = std::pow(
                kScreenTexture[screenTexel][channel],
                2.2F);
            if (contract.hasPipboyScreen) {
                expected[0][channel] += pipboyScreen * 0.7F;
            }
            if (contract.hasBoneTint) {
                constexpr auto boneTintPaletteTexel = 1u;
                expected[0][channel] += transformedValue(
                    kBoneTintPalette[boneTintPaletteTexel][channel],
                    lightingCase.colorGamma,
                    lightingCase.vanillaDiffuseColorMult) *
                    kBoneTintPalette[boneTintPaletteTexel][3] *
                    kBoneTintLookup[3] * kBoneTintVertexColor[3] * 4.0F;
            }

            const auto safeEmissiveMult = (std::max)(
                lightingCase.emissiveMult,
                1.0e-5F);
            auto emission = transformedValue(
                emitColor[channel] / safeEmissiveMult,
                lightingCase.emitColorGamma,
                lightingCase.emissiveMult * lightingCase.emitColorMult);
            if (usesGlowmap) {
                emission *= transformedValue(
                    kGlowTexture[channel],
                    lightingCase.glowmapGamma,
                    lightingCase.glowmapMult);
            }
            if (contract.hasPipboyScreen) {
                emission += pipboyScreen * 0.4F;
            }
            expected[4][channel] = emission;
        }
        return expected;
    }

    void run(const std::filesystem::path& root)
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel{};
        constexpr std::array requestedFeatureLevels{
            D3D_FEATURE_LEVEL_11_0,
        };
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                D3D11_CREATE_DEVICE_SINGLETHREADED,
                requestedFeatureLevels.data(),
                static_cast<UINT>(requestedFeatureLevels.size()),
                D3D11_SDK_VERSION,
                &device,
                &featureLevel,
                &context),
            "D3D11CreateDevice(WARP)");
        if (featureLevel != D3D_FEATURE_LEVEL_11_0) {
            fail("D3D11 WARP did not provide feature level 11_0");
        }

        std::array<ComPtr<ID3D11VertexShader>, 65536> vertexShaders;
        const auto getVertexShader = [&device, &vertexShaders] (
                                         std::size_t index) {
            if (vertexShaders[index].Get() != nullptr) {
                return vertexShaders[index].Get();
            }
            const auto hasVertexColor = (index & 1u) != 0;
            const auto isInstanced = (index & 2u) != 0;
            const auto usesTessellatedInputs = (index & 4u) != 0;
            const auto hasLandscapeLod = (index & 8u) != 0;
            const auto hasBoneTint = (index & 16u) != 0;
            const auto hasPipboyScreen = (index & 32u) != 0;
            const auto hasFaceDetail = (index & 64u) != 0;
            const auto hasDismemberment = (index & 128u) != 0;
            const auto hasCombinedMaterial = (index & 8192u) != 0;
            const auto hasLandscapeLayers = (index & 16384u) != 0;
            const auto hasInstancedLandscapeLayers =
                (index & 32768u) != 0;
            const auto eyeIndex = static_cast<std::uint32_t>(
                (index >> 8u) & 1u);
            const auto useDismemberment = (index & 512u) != 0;
            const auto hasMeatCuff = (index & 1024u) != 0;
            const auto meatCuffCase = static_cast<std::uint32_t>(
                (index >> 11u) & 3u);
            const auto bytecode = compileVertexShader(
                hasVertexColor,
                isInstanced,
                usesTessellatedInputs,
                hasLandscapeLod,
                hasBoneTint,
                hasPipboyScreen,
                hasFaceDetail,
                hasDismemberment,
                hasCombinedMaterial,
                hasLandscapeLayers,
                hasInstancedLandscapeLayers,
                useDismemberment,
                hasMeatCuff,
                meatCuffCase,
                eyeIndex);
            require(
                device->CreateVertexShader(
                    bytecode->GetBufferPointer(),
                    bytecode->GetBufferSize(),
                    nullptr,
                    &vertexShaders[index]),
                std::string("CreateVertexShader(") +
                    (hasVertexColor ? "COLOR0" : "no COLOR0") +
                    (isInstanced ? ", instanced" : ", non-instanced") +
                    (usesTessellatedInputs ?
                            ", tessellated inputs" :
                            ", standard inputs") +
                    (hasLandscapeLod ? ", landscape LOD" : "") +
                    (hasBoneTint ? ", COLOR1" : "") +
                    (hasPipboyScreen ? ", Pip-Boy TEXCOORD6" : "") +
                    (hasFaceDetail ? ", face TEXCOORD6" : "") +
                    (hasDismemberment ? ", dismemberment inputs" : "") +
                    (hasCombinedMaterial ? ", combined material" : "") +
                    (hasLandscapeLayers ? ", four-layer landscape" : "") +
                    (hasInstancedLandscapeLayers ?
                            ", instanced landscape layout" :
                            "") +
                    (useDismemberment ? ", dismemberment layer" : "") +
                    (hasMeatCuff ? ", meat-cuff inputs" : "") +
                    (hasMeatCuff ?
                            ", meat-cuff case " + std::to_string(meatCuffCase) :
                            "") +
                    ", eye " + std::to_string(eyeIndex) + ")");
            return vertexShaders[index].Get();
        };

        const auto verified = root / "package" / "Shaders" / "Community" /
            "VerifiedLinearLighting";
        const auto reconstruction = root / "package" / "Shaders" /
            "Community" / "Reconstruction";
        constexpr std::size_t kCaseCount = 3;
        std::vector<std::string> failures;
        for (const auto& contract : kShaderContracts) {
            const auto vanillaBytes = readFile(
                verified / (std::string(contract.name) + ".dxbc"));
            const auto replacementBytes = readFile(
                reconstruction /
                (std::string(contract.name) +
                    ".LinearLightingCandidate.dxbc"));
            const auto usesGlowmap = usesTextureSlot(vanillaBytes, 3) &&
                !contract.hasStandaloneHair &&
                !contract.hasLandscapeLayers;
            const auto usesDiffuseTexture = usesTextureSlot(vanillaBytes, 0);
            ComPtr<ID3D11PixelShader> vanillaShader;
            ComPtr<ID3D11PixelShader> replacementShader;
            require(
                device->CreatePixelShader(
                    vanillaBytes.data(),
                    vanillaBytes.size(),
                    nullptr,
                    &vanillaShader),
                std::string("CreatePixelShader(vanilla ") +
                    std::string(contract.name) + ")");
            require(
                device->CreatePixelShader(
                    replacementBytes.data(),
                    replacementBytes.size(),
                    nullptr,
                    &replacementShader),
                std::string("CreatePixelShader(replacement ") +
                    std::string(contract.name) + ")");

            std::array<Pixel, 2> leftEyeMotion{};
            for (std::uint32_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
                const auto surfaceVariantCount =
                    (contract.hasDismemberment || contract.hasMeatCuff) ? 2u : 1u;
                for (std::uint32_t surfaceVariant = 0;
                     surfaceVariant < surfaceVariantCount;
                     ++surfaceVariant) {
                const auto useDismemberment =
                    contract.hasDismemberment && surfaceVariant != 0;
                const auto meatCuffCase = contract.hasMeatCuff ?
                    surfaceVariant : 0u;
                auto* vertexShader = getVertexShader(
                    (contract.hasVertexColor ? 1u : 0u) |
                    (contract.isInstanced ? 2u : 0u) |
                    (contract.usesTessellatedInputs ? 4u : 0u) |
                    (contract.hasLandscapeLod ? 8u : 0u) |
                    (contract.hasBoneTint ? 16u : 0u) |
                    (contract.hasPipboyScreen ? 32u : 0u) |
                    (contract.hasFaceDetail ? 64u : 0u) |
                    (contract.hasDismemberment ? 128u : 0u) |
                    (static_cast<std::size_t>(eyeIndex) << 8u) |
                    (useDismemberment ? 512u : 0u) |
                    (contract.hasMeatCuff ? 1024u : 0u) |
                    (static_cast<std::size_t>(meatCuffCase) << 11u) |
                    (contract.hasCombinedMaterial ? 8192u : 0u) |
                    (contract.hasLandscapeLayers ? 16384u : 0u) |
                    (contract.hasInstancedLandscapeLayers ? 32768u : 0u));
                for (std::size_t caseIndex = 0; caseIndex < kCaseCount;
                     ++caseIndex) {
                const auto vanilla = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *vanillaShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kDisabledCase,
                    contract.hasAdditionalAlphaMask,
                    contract.hasLandscapeLod,
                    contract.hasGradientRemap,
                    contract.hasGradientHair,
                    contract.hasBoneTint,
                    contract.hasPipboyScreen,
                    contract.hasSkinTint,
                    contract.hasStandaloneHair,
                    contract.hasDismemberment,
                    contract.hasMeatCuff,
                    contract.hasCombinedMaterial,
                    contract.hasLandscapeLayers,
                    contract.hasInstancedLandscapeLayers);
                if (contract.hasMeatCuff && caseIndex == 0 &&
                    isClearResult(vanilla)) {
                    failures.push_back(
                        std::string(contract.name) +
                        " valid meat-cuff atlas case unexpectedly discarded");
                }
                if (contract.mrtCount == 6 && caseIndex == 0) {
                    if (eyeIndex == 0) {
                        leftEyeMotion[surfaceVariant] = vanilla[5];
                    } else if (
                        approximatelyEqual(
                            leftEyeMotion[surfaceVariant][0], vanilla[5][0]) &&
                        approximatelyEqual(
                            leftEyeMotion[surfaceVariant][1], vanilla[5][1])) {
                        failures.push_back(
                            std::string(contract.name) +
                            " paired-eye fixture produced identical motion vectors");
                    }
                }
                const auto disabled = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kDisabledCase,
                    contract.hasAdditionalAlphaMask,
                    contract.hasLandscapeLod,
                    contract.hasGradientRemap,
                    contract.hasGradientHair,
                    contract.hasBoneTint,
                    contract.hasPipboyScreen,
                    contract.hasSkinTint,
                    contract.hasStandaloneHair,
                    contract.hasDismemberment,
                    contract.hasMeatCuff,
                    contract.hasCombinedMaterial,
                    contract.hasLandscapeLayers,
                    contract.hasInstancedLandscapeLayers);
                auto mismatch = compare(
                    contract,
                    kDisabledCase.name,
                    eyeIndex,
                    caseIndex,
                    vanilla,
                    disabled);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }

                const auto identity = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kIdentityCase,
                    contract.hasAdditionalAlphaMask,
                    contract.hasLandscapeLod,
                    contract.hasGradientRemap,
                    contract.hasGradientHair,
                    contract.hasBoneTint,
                    contract.hasPipboyScreen,
                    contract.hasSkinTint,
                    contract.hasStandaloneHair,
                    contract.hasDismemberment,
                    contract.hasMeatCuff,
                    contract.hasCombinedMaterial,
                    contract.hasLandscapeLayers,
                    contract.hasInstancedLandscapeLayers);
                mismatch = compare(
                    contract,
                    kIdentityCase.name,
                    eyeIndex,
                    caseIndex,
                    vanilla,
                    identity);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }

                const auto transformed = render(
                    *device.Get(),
                    *context.Get(),
                    *vertexShader,
                    *replacementShader.Get(),
                    contract.mrtCount,
                    contract.isInstanced,
                    caseIndex,
                    kTransformedCase,
                    contract.hasAdditionalAlphaMask,
                    contract.hasLandscapeLod,
                    contract.hasGradientRemap,
                    contract.hasGradientHair,
                    contract.hasBoneTint,
                    contract.hasPipboyScreen,
                    contract.hasSkinTint,
                    contract.hasStandaloneHair,
                    contract.hasDismemberment,
                    contract.hasMeatCuff,
                    contract.hasCombinedMaterial,
                    contract.hasLandscapeLayers,
                    contract.hasInstancedLandscapeLayers);
                const auto expected = makeEnabledExpected(
                    contract,
                    caseIndex,
                    usesGlowmap,
                    usesDiffuseTexture,
                    useDismemberment,
                    surfaceVariant,
                    vanilla,
                    kTransformedCase);
                mismatch = compare(
                    contract,
                    kTransformedCase.name,
                    eyeIndex,
                    caseIndex,
                    expected,
                    transformed);
                if (!mismatch.empty()) {
                    failures.push_back(std::move(mismatch));
                }
                }

                if (contract.hasAdditionalAlphaMask) {
                struct MaskProofCase
                {
                    AdditionalAlphaCase value;
                    std::string_view name;
                    bool expectsDiscard;
                };
                constexpr std::array maskProofCases{
                    MaskProofCase{ AdditionalAlphaCase::texturePass,
                        "additional-alpha-texture-pass", false },
                    MaskProofCase{ AdditionalAlphaCase::textureReject,
                        "additional-alpha-texture-reject", true },
                    MaskProofCase{ AdditionalAlphaCase::noisePass,
                        "additional-alpha-noise-pass", false },
                    MaskProofCase{ AdditionalAlphaCase::noiseReject,
                        "additional-alpha-noise-reject", true },
                };
                constexpr std::size_t maskCaseIndex = 1;
                for (const auto& maskCase : maskProofCases) {
                    const auto vanilla = render(
                        *device.Get(),
                        *context.Get(),
                        *vertexShader,
                        *vanillaShader.Get(),
                        contract.mrtCount,
                        contract.isInstanced,
                        maskCaseIndex,
                        kDisabledCase,
                        true,
                        contract.hasLandscapeLod,
                        contract.hasGradientRemap,
                        contract.hasGradientHair,
                        contract.hasBoneTint,
                        contract.hasPipboyScreen,
                        contract.hasSkinTint,
                        contract.hasStandaloneHair,
                        contract.hasDismemberment,
                        contract.hasMeatCuff,
                        contract.hasCombinedMaterial,
                        contract.hasLandscapeLayers,
                        contract.hasInstancedLandscapeLayers,
                        maskCase.value);
                    const auto replacement = render(
                        *device.Get(),
                        *context.Get(),
                        *vertexShader,
                        *replacementShader.Get(),
                        contract.mrtCount,
                        contract.isInstanced,
                        maskCaseIndex,
                        kDisabledCase,
                        true,
                        contract.hasLandscapeLod,
                        contract.hasGradientRemap,
                        contract.hasGradientHair,
                        contract.hasBoneTint,
                        contract.hasPipboyScreen,
                        contract.hasSkinTint,
                        contract.hasStandaloneHair,
                        contract.hasDismemberment,
                        contract.hasMeatCuff,
                        contract.hasCombinedMaterial,
                        contract.hasLandscapeLayers,
                        contract.hasInstancedLandscapeLayers,
                        maskCase.value);
                    auto mismatch = compare(
                        contract,
                        maskCase.name,
                        eyeIndex,
                        maskCaseIndex,
                        vanilla,
                        replacement);
                    if (!mismatch.empty()) {
                        failures.push_back(std::move(mismatch));
                    }
                    if (isClearResult(vanilla) != maskCase.expectsDiscard) {
                        failures.push_back(
                            std::string(contract.name) + " " +
                            std::string(maskCase.name) +
                            " eye " + std::to_string(eyeIndex) +
                            " did not exercise the expected discard state");
                    }
                }
                }
            }
                if (contract.hasMeatCuff) {
                    struct MeatCuffProofCase
                    {
                        std::uint32_t inputCase;
                        MeatCuffAlphaCase alphaCase;
                        std::string_view name;
                    };
                    constexpr std::array meatCuffProofCases{
                        MeatCuffProofCase{ 2, MeatCuffAlphaCase::pass,
                            "meat-cuff-invalid-low" },
                        MeatCuffProofCase{ 3, MeatCuffAlphaCase::pass,
                            "meat-cuff-invalid-high" },
                        MeatCuffProofCase{ 0, MeatCuffAlphaCase::reject,
                            "meat-cuff-alpha-reject" },
                    };
                    constexpr std::size_t proofCaseIndex = 1;
                    for (const auto& proofCase : meatCuffProofCases) {
                        auto* proofVertexShader = getVertexShader(
                            (contract.hasVertexColor ? 1u : 0u) |
                            (contract.isInstanced ? 2u : 0u) |
                            (contract.usesTessellatedInputs ? 4u : 0u) |
                            (contract.hasLandscapeLod ? 8u : 0u) |
                            (contract.hasBoneTint ? 16u : 0u) |
                            (contract.hasPipboyScreen ? 32u : 0u) |
                            (contract.hasFaceDetail ? 64u : 0u) |
                            (contract.hasDismemberment ? 128u : 0u) |
                            (static_cast<std::size_t>(eyeIndex) << 8u) |
                            (contract.hasMeatCuff ? 1024u : 0u) |
                            (static_cast<std::size_t>(proofCase.inputCase) <<
                                11u) |
                            (contract.hasCombinedMaterial ? 8192u : 0u));
                        const auto vanilla = render(
                            *device.Get(),
                            *context.Get(),
                            *proofVertexShader,
                            *vanillaShader.Get(),
                            contract.mrtCount,
                            contract.isInstanced,
                            proofCaseIndex,
                            kDisabledCase,
                            contract.hasAdditionalAlphaMask,
                            contract.hasLandscapeLod,
                            contract.hasGradientRemap,
                            contract.hasGradientHair,
                            contract.hasBoneTint,
                            contract.hasPipboyScreen,
                            contract.hasSkinTint,
                            contract.hasStandaloneHair,
                            contract.hasDismemberment,
                            true,
                            contract.hasCombinedMaterial,
                            contract.hasLandscapeLayers,
                            contract.hasInstancedLandscapeLayers,
                            AdditionalAlphaCase::disabled,
                            proofCase.alphaCase);
                        const auto replacement = render(
                            *device.Get(),
                            *context.Get(),
                            *proofVertexShader,
                            *replacementShader.Get(),
                            contract.mrtCount,
                            contract.isInstanced,
                            proofCaseIndex,
                            kDisabledCase,
                            contract.hasAdditionalAlphaMask,
                            contract.hasLandscapeLod,
                            contract.hasGradientRemap,
                            contract.hasGradientHair,
                            contract.hasBoneTint,
                            contract.hasPipboyScreen,
                            contract.hasSkinTint,
                            contract.hasStandaloneHair,
                            contract.hasDismemberment,
                            true,
                            contract.hasCombinedMaterial,
                            contract.hasLandscapeLayers,
                            contract.hasInstancedLandscapeLayers,
                            AdditionalAlphaCase::disabled,
                            proofCase.alphaCase);
                        auto mismatch = compare(
                            contract,
                            proofCase.name,
                            eyeIndex,
                            proofCaseIndex,
                            vanilla,
                            replacement);
                        if (!mismatch.empty()) {
                            failures.push_back(std::move(mismatch));
                        }
                        if (!isClearResult(vanilla)) {
                            failures.push_back(
                                std::string(contract.name) + " " +
                                std::string(proofCase.name) + " eye " +
                                std::to_string(eyeIndex) +
                                " did not exercise the expected discard state");
                        }
                    }
                }
            }
        }
        if (!failures.empty()) {
            std::string message = "shader parity/oracle mismatches:";
            for (const auto& failure : failures) {
                message += "\n  " + failure;
            }
            fail(message);
        }
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "Usage: LinearLightingShaderParityTests <repo-root>\n";
        return EXIT_FAILURE;
    }
    try {
        run(std::filesystem::absolute(arguments[1]));
        std::cout << "Linear Lighting disabled and enabled shader paths verified across both VR eyes: "
                  << kShaderContracts.size() << " contracts\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
