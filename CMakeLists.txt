cmake_minimum_required(VERSION 4.2)

set(NAME "CSAH")
set(FRIENDLY_NAME "Community Shaders at Home (CSAH) VR")
set(VERSION 0.0.6)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)

set(PALM_SETTINGS_SOURCE
  "${ROOT_DIR}/settings/PALMSettings.json")
if(NOT EXISTS "${PALM_SETTINGS_SOURCE}")
  message(FATAL_ERROR
    "CSAH owns its PALM settings manifest, but it is missing: "
    "'${PALM_SETTINGS_SOURCE}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PALM_SETTINGS_SOURCE}")

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(DEFINED VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE
    "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
  set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
  set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "")
else()
  message(FATAL_ERROR "VCPKG_ROOT is not set")
endif()

if(DEFINED COMMON_LIB_F4VR_PATH AND
    NOT COMMON_LIB_F4VR_PATH STREQUAL "")
  set(COMMON_LIB_F4VR_PATH_SELECTED
    "${COMMON_LIB_F4VR_PATH}"
    CACHE PATH "" FORCE)
else()
  set(COMMON_LIB_F4VR_PATH_SELECTED
    "${WORKSPACE_ROOT}/libraries_and_tools/CommonLibF4VR"
    CACHE PATH "" FORCE)
endif()
if(NOT EXISTS
    "${COMMON_LIB_F4VR_PATH_SELECTED}/CommonLibF4/include/REL/Relocation.h")
  message(FATAL_ERROR
    "CommonLibF4VR path not found in '${COMMON_LIB_F4VR_PATH_SELECTED}'")
endif()

set_from_environment(STREAMLINE_SDK_ROOT)
if(NOT DEFINED STREAMLINE_SDK_ROOT OR STREAMLINE_SDK_ROOT STREQUAL "")
  set(STREAMLINE_SDK_ROOT
    "${WORKSPACE_ROOT}/../devtools/streamline-sdk-v2.12.0"
    CACHE PATH "NVIDIA Streamline 2.12 SDK root")
endif()
set(STREAMLINE_INCLUDE_DIR "${STREAMLINE_SDK_ROOT}/include")
set(STREAMLINE_RUNTIME_DIR "${STREAMLINE_SDK_ROOT}/bin/x64")
set(STREAMLINE_RUNTIME_FILES
  "${STREAMLINE_RUNTIME_DIR}/sl.interposer.dll"
  "${STREAMLINE_RUNTIME_DIR}/sl.common.dll"
  "${STREAMLINE_RUNTIME_DIR}/sl.dlss.dll"
  "${STREAMLINE_RUNTIME_DIR}/nvngx_dlss.dll")
set(STREAMLINE_LICENSE_FILES
  "${STREAMLINE_SDK_ROOT}/license.txt"
  "${STREAMLINE_SDK_ROOT}/3rd-party-licenses.md"
  "${STREAMLINE_RUNTIME_DIR}/nvngx_dlss.license.txt")
foreach(required IN ITEMS
    "${STREAMLINE_INCLUDE_DIR}/sl.h"
    ${STREAMLINE_RUNTIME_FILES}
    ${STREAMLINE_LICENSE_FILES})
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR
      "Required Streamline 2.12 file not found: '${required}'")
  endif()
endforeach()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)

set(DLAA_GENERATED_DIR "${BUILD_DIR}/generated/dlaa")
file(MAKE_DIRECTORY "${DLAA_GENERATED_DIR}")
include("${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake")
csah_verify_fo4vr_loader("${SOURCE_DIR}/XSEPlugin.cpp")
set(RENDERER_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp")
set(DFPREPASS_HOOK_SOURCE
  "${SOURCE_DIR}/render/BSDFPrePassShaderHook.cpp")
include("${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake")
set(SETTINGS_PATH_SOURCE "${SOURCE_DIR}/support/SettingsPath.cpp")
set(SETTINGS_PATH_HEADER "${SOURCE_DIR}/support/SettingsPath.h")
set(SHARED_SETTINGS_RUNTIME_SOURCE
  "${SOURCE_DIR}/settings/SharedSettingsRuntime.cpp")
set(SHARED_SETTINGS_RUNTIME_HEADER
  "${SOURCE_DIR}/settings/SharedSettingsRuntime.h")
set(PALM_SETTINGS_VERIFY_SCRIPT "${ROOT_DIR}/cmake/VerifyPALMSettings.cmake")
set(PROJECT_CMAKE_SOURCE "${ROOT_DIR}/CMakeLists.txt")
set(SHARED_SETTINGS_SOURCE "${SHARED_SETTINGS_RUNTIME_SOURCE}")
set(SHARED_SETTINGS_HEADER "${SHARED_SETTINGS_RUNTIME_HEADER}")
set(SOURCE_ROOT "${ROOT_DIR}")
set(D3D11_HOOK_SOURCE "${SOURCE_DIR}/render/D3D11Hooks.cpp")
set(DLAA_SOURCE_DIR "${SOURCE_DIR}/Features/dlaa")
set(DLAA_RUNTIME_SOURCE "${DLAA_SOURCE_DIR}/DlaaRuntime.cpp")
set(DLAA_FRAME_HEADER "${SOURCE_DIR}/render/Fo4VrCamera.h")
set(DLAA_ENGINE_HOOK_SOURCE "${DLAA_SOURCE_DIR}/DlaaEngineHooks.cpp")
set(DLAA_D3D11_HOOK_SOURCE "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.cpp")
set(DLAA_STREAMLINE_SOURCE "${DLAA_SOURCE_DIR}/StreamlineBackend.cpp")
set(DLAA_SETTINGS_STORE_SOURCE "${DLAA_SOURCE_DIR}/DlaaSettingsStore.cpp")
set(UPSCALING_COMPOSE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/ComposeUpscalingCS.hlsl")
set(UPSCALING_COMPOSE_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/ComposeUpscalingCS.dxbc")
set(UPSCALING_COMPOSE_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingComposeShader.h")
set(UPSCALING_MOTION_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/RepairMotionVectorsCS.hlsl")
set(UPSCALING_MOTION_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/RepairMotionVectorsCS.dxbc")
set(UPSCALING_MOTION_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingMotionRepairShader.h")
set(UPSCALING_DEPTH_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/PrepareDepthCS.hlsl")
set(UPSCALING_DEPTH_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/PrepareDepthCS.dxbc")
set(UPSCALING_DEPTH_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingDepthShader.h")
set(UPSCALING_REACTIVE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Upscaling/BuildReactiveMaskCS.hlsl")
set(UPSCALING_REACTIVE_SHADER_BINARY
  "${DLAA_GENERATED_DIR}/BuildReactiveMaskCS.dxbc")
set(UPSCALING_REACTIVE_SHADER_HEADER
  "${DLAA_GENERATED_DIR}/GeneratedUpscalingReactiveMaskShader.h")
set_source_files_properties(
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_DEPTH_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(DLAA_VERIFY_SCRIPT "${ROOT_DIR}/cmake/VerifyDlaa.cmake")
set(DLAA_ENGINE_SOURCE "${DLAA_ENGINE_HOOK_SOURCE}")
set(DLAA_D3D_SOURCE "${DLAA_D3D11_HOOK_SOURCE}")
set(DLAA_SETTINGS_SOURCE "${DLAA_SETTINGS_STORE_SOURCE}")
set(RENDERER_D3D_SOURCE "${D3D11_HOOK_SOURCE}")
set_source_files_properties("${DLAA_STREAMLINE_SOURCE}" PROPERTIES
  SKIP_PRECOMPILE_HEADERS ON)
set(VANILLA_FIXES_SOURCE_DIR
  "${SOURCE_DIR}/Features/vanilla_fixes")
set(VANILLA_REFLECTION_PATCH_TOOL_SOURCE
  "${ROOT_DIR}/tools/VanillaReflectionPatchTool.cpp")
set(VANILLA_REFLECTION_PATCH_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.cpp")
set(VANILLA_REFLECTION_PATCH_HEADER
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.h")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/DirectionalLightDiagnosticSurface.cpp")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_HEADER
  "${VANILLA_FIXES_SOURCE_DIR}/DirectionalLightDiagnosticSurface.h")
set(VANILLA_SAO_BLUR_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SaoBlurHCS.hlsl")
set(VANILLA_SAO_RAW_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SaoRawAOCS.hlsl")
set(VANILLA_SSLR_BLUR_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrBlurHVS.hlsl")
set(VANILLA_SSLR_PREPASS_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrPrepassPS.hlsl")
set(VANILLA_SSLR_RAYTRACE_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/SslrRaytracePS.hlsl")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/DirectionalDiagnosticCompositePS.hlsl")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/DirectionalDiagnosticCoverageVS.hlsl")
set(VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VanillaFixes/LightingOwnershipDiagnosticPS.hlsl")
set_source_files_properties(
  "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(VANILLA_SHADER_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.cpp")
set(VANILLA_FOCUS_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.cpp")
set(VANILLA_SUN_OCCLUSION_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/SunOcclusionRuntime.cpp")
set(VANILLA_RUNTIME_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.cpp")
set(VANILLA_SETTINGS_STORE_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.cpp")
set(VANILLA_SSLR_ENVIRONMENT_SOURCE
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.cpp")
set(VANILLA_IBL_GENERATOR_SOURCE
  "${ROOT_DIR}/tools/generate_ibl_material_contracts.py")
set(VANILLA_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyVanillaFixes.cmake")
set(NATIVE_SHADOW_SOURCE_DIR
  "${SOURCE_DIR}/Features/native_shadows")
set(NATIVE_SHADOW_RUNTIME_SOURCE
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowRuntime.cpp")
set(NATIVE_SHADOW_RUNTIME_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowRuntime.h")
set(NATIVE_SHADOW_SETTINGS_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettings.h")
set(NATIVE_SHADOW_SETTINGS_STORE_SOURCE
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettingsStore.cpp")
set(NATIVE_SHADOW_SETTINGS_STORE_HEADER
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowSettingsStore.h")
set(NATIVE_SHADOW_PATCH_MODEL
  "${NATIVE_SHADOW_SOURCE_DIR}/NativeShadowPatchModel.h")
set(NATIVE_SHADOW_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyNativeShadows.cmake")
set_source_files_properties(
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
  "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(LINEAR_LIGHTING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
set(LINEAR_LIGHTING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h")
set(POINT_LIGHT_HOOK_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightHook.cpp")
set(QUALIFICATION_SOURCE
  "${SOURCE_DIR}/diagnostics/LinearLightingQualification.cpp")
set(PLUGIN_SOURCE "${SOURCE_DIR}/XSEPlugin.cpp")
set(VANILLA_D3D11_HOOK_SOURCE "${D3D11_HOOK_SOURCE}")
set(VANILLA_PLUGIN_SOURCE "${PLUGIN_SOURCE}")
set(DFLIGHT_AMBIENT_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/DFLightAmbientContracts.json")
set(DFLIGHT_AMBIENT_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_dflight_ambient_contracts.py")
set(DFLIGHT_AMBIENT_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedDFLightAmbientContracts.inl")
set(DFLIGHT_AMBIENT_PATCH_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
file(GLOB DFLIGHT_AMBIENT_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedDFLightAmbient/*.dxbc")
set(SKY_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLightingContracts.json")
set(SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_sky_linear_lighting_contracts.py")
set(SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedSkyLinearLightingContracts.inl")
set(SKY_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLighting/SkyLinearLighting.hlsl")
set(RESOURCE_SOURCE "${SOURCE_DIR}/resources.rc")
file(GLOB SKY_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/SkyLinearLighting/*.dxbc")
file(GLOB SKY_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedSkyLinearLighting/*.dxbc")
set(DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLightingContracts.json")
set(DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_distant_tree_linear_lighting_contracts.py")
set(DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedDistantTreeLinearLightingContract.inl")
set(DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLighting/DistantTreeLinearLighting.hlsl")
set(FIXED_SHADER_BINDING_LOOKUP_HEADER
  "${SOURCE_DIR}/Features/linear_lighting/FixedShaderBindingLookup.h")
file(GLOB DISTANT_TREE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/DistantTreeLinearLighting/*.dxbc")
file(GLOB DISTANT_TREE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedDistantTreeLinearLighting/*.dxbc")
set(PARTICLE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLightingContracts.json")
set(PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_particle_linear_lighting_contracts.py")
set(PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedParticleLinearLightingContracts.inl")
set(PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLighting/ParticleLinearLighting.hlsl")
file(GLOB PARTICLE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/ParticleLinearLighting/*.dxbc")
file(GLOB PARTICLE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedParticleLinearLighting/*.dxbc")
set(WATER_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLightingContracts.json")
set(WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_water_linear_lighting_contracts.py")
set(WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedWaterLinearLightingContracts.inl")
set(WATER_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLighting/WaterColorTransformTemplate.hlsl")
file(GLOB WATER_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/WaterLinearLighting/*.dxbc")
file(GLOB WATER_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedWaterLinearLighting/*.dxbc")
set(VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLightingContracts.json")
set(VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_vls_composite_linear_lighting_contract.py")
set(VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedVLSCompositeLinearLightingContract.inl")
set(VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLighting/VLSCompositeLinearLighting.hlsl")
file(GLOB VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VLSCompositeLinearLighting/*.dxbc")
file(GLOB VLS_COMPOSITE_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedVLSCompositeLinearLighting/*.dxbc")
set(EFFECT_LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLightingContracts.json")
set(EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_effect_linear_lighting_contracts.py")
set(EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedEffectLinearLightingContracts.inl")
set(EFFECT_LINEAR_LIGHTING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLighting/EffectLinearLighting.hlsl")
file(GLOB EFFECT_LINEAR_LIGHTING_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/EffectLinearLighting/*.dxbc")
file(GLOB EFFECT_LINEAR_LIGHTING_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedEffectLinearLighting/*.dxbc")
set(IBL_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblRuntime.cpp")
set(VANILLA_IBL_RUNTIME_SOURCE "${IBL_RUNTIME_SOURCE}")
set(IBL_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/ibl/IblRuntime.h")
set(IBL_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/ibl/IblSettings.h")
set(IBL_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblSettingsStore.cpp")
set(IBL_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblSettingsStore.h")
set(IBL_PROVIDER_MODEL
  "${SOURCE_DIR}/Features/ibl/IblProviderModel.h")
set(IBL_ENVIRONMENT_PROVIDER_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentProvider.cpp")
set(IBL_ENVIRONMENT_PROVIDER_HEADER
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentProvider.h")
set(IBL_ENVIRONMENT_UPDATER_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentUpdater.cpp")
set(IBL_ENVIRONMENT_UPDATER_HEADER
  "${SOURCE_DIR}/Features/ibl/IblEnvironmentUpdater.h")
set(IBL_COMPUTE_STATE_SCOPE_SOURCE
  "${SOURCE_DIR}/render/ComputeStateScope.cpp")
set(IBL_COMPUTE_STATE_SCOPE_HEADER
  "${SOURCE_DIR}/render/ComputeStateScope.h")
set(GPU_TIMING_PROFILER_SOURCE
  "${SOURCE_DIR}/render/GpuTimingProfiler.cpp")
set(GPU_TIMING_PROFILER_HEADER
  "${SOURCE_DIR}/render/GpuTimingProfiler.h")
set(IBL_PROJECTION_MODEL
  "${SOURCE_DIR}/Features/ibl/IblProjectionModel.h")
set(IBL_CAPTURE_PROBE_MODEL
  "${SOURCE_DIR}/Features/ibl/IblCaptureProbeModel.h")
set(IBL_SCENE_RADIANCE_PROBE_MODEL
  "${SOURCE_DIR}/Features/ibl/IblSceneRadianceProbeModel.h")
set(IBL_REFLECTION_FREE_CAPTURE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblReflectionFreeCapture.cpp")
set(IBL_REFLECTION_FREE_CAPTURE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblReflectionFreeCapture.h")
set(IBL_CAPTURE_PROBE_VERIFIER
  "${ROOT_DIR}/tools/verify_ibl_capture_probe_contracts.py")
set(IBL_MATERIAL_BINDING_SCOPE_SOURCE
  "${SOURCE_DIR}/Features/ibl/IblMaterialBindingScope.cpp")
set(IBL_MATERIAL_BINDING_SCOPE_HEADER
  "${SOURCE_DIR}/Features/ibl/IblMaterialBindingScope.h")
set(IBL_MATERIAL_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_ibl_material_contracts.py")
set(IBL_MATERIAL_DXBC_TRANSFORM
  "${ROOT_DIR}/tools/dxbc_transform.py")
set(IBL_MATERIAL_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/ibl/GeneratedIblMaterialContracts.inl")
set(PBR_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/pbr/PbrRuntime.cpp")
set(PBR_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/pbr/PbrRuntime.h")
set(PBR_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/pbr/PbrSettings.h")
set(PBR_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/pbr/PbrSettingsStore.cpp")
set(PBR_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/pbr/PbrSettingsStore.h")
set(PBR_MATERIAL_MODEL_HEADER
  "${SOURCE_DIR}/Features/pbr/PbrMaterialModel.h")
set(AUTHORED_PBR_PACK_BUILDER
  "${ROOT_DIR}/tools/build_authored_pbr_pack.py")
set(AUTHORED_PBR_PACK_TEST
  "${ROOT_DIR}/tests/test_build_authored_pbr_pack.py")
set(AUTHORED_PBR_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyAuthoredPbrRuntime.cmake")
set(PBR_COMMON_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/PBR/PbrCommon.hlsli")
set(PBR_DIRECTIONAL_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/PBR/PbrDirectionalTransform.hlsl")
set(PBR_MATERIAL_OUTPUT_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/PBR/PbrMaterialOutput.hlsl")
set_source_files_properties(
  "${PBR_COMMON_SHADER_SOURCE}"
  "${PBR_DIRECTIONAL_SHADER_SOURCE}"
  "${PBR_MATERIAL_OUTPUT_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(CONTACT_SHADOW_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowRuntime.cpp")
set(CONTACT_SHADOW_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowRuntime.h")
set(CONTACT_SHADOW_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettings.h")
set(CONTACT_SHADOW_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettingsStore.cpp")
set(CONTACT_SHADOW_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/contact_shadows/ContactShadowSettingsStore.h")
set(CONTACT_SHADOW_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowTransform.hlsl")
set(CONTACT_SHADOW_WRAPPED_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/WrappedGrassTransform.hlsl")
set(CONTACT_SHADOW_HAIR_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/HairSpecularTransform.hlsl")
set(CONTACT_SHADOW_WETNESS_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/BasicWetnessTransform.hlsl")
set(CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/DirectionalLightingDiagnosticTransform.hlsl")
set(CONTACT_SHADOW_MASK_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowMaskCS.hlsl")
set(CONTACT_SHADOW_DISPATCH_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowDispatchCS.hlsl")
set(CONTACT_SHADOW_RESOLVE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/ContactShadows/ContactShadowResolveCS.hlsl")
set_source_files_properties(
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
  "${CONTACT_SHADOW_HAIR_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(CONTACT_SHADOW_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_contact_shadow_shader.py")
set(CONTACT_SHADOW_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyContactShadows.cmake")
set(SKYLIGHTING_SOURCE_DIR
  "${SOURCE_DIR}/Features/skylighting")
set(SKYLIGHTING_RUNTIME_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingRuntime.cpp")
set(SKYLIGHTING_RUNTIME_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingRuntime.h")
set(SKYLIGHTING_NATIVE_HOOK_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingNativeHooks.cpp")
set(SKYLIGHTING_NATIVE_HOOK_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingNativeHooks.h")
set(SKYLIGHTING_SETTINGS_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettings.h")
set(SKYLIGHTING_SETTINGS_STORE_SOURCE
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettingsStore.cpp")
set(SKYLIGHTING_SETTINGS_STORE_HEADER
  "${SKYLIGHTING_SOURCE_DIR}/SkylightingSettingsStore.h")
set(SKYLIGHTING_SAMPLING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/SkylightingSampling.hlsli")
set(SKYLIGHTING_RECEIVER_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/SkylightingReceiver.hlsli")
set(SKYLIGHTING_SUN_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/UpdateSunProbesCS.hlsl")
set(SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/SkylightingDirectionalTransform.hlsl")
set(SKYLIGHTING_AMBIENT_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/SkylightingAmbientTransform.hlsl")
set(SKYLIGHTING_COMPUTE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Skylighting/UpdateProbesCS.hlsl")
set(SKYLIGHTING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_skylighting_shaders.py")
set(SKYLIGHTING_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifySkylighting.cmake")
set_source_files_properties(
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_SAMPLING_SHADER_SOURCE}"
  "${SKYLIGHTING_RECEIVER_SHADER_SOURCE}"
  "${SKYLIGHTING_SUN_SHADER_SOURCE}"
  "${SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(SKY_SYNC_SOURCE_DIR
  "${SOURCE_DIR}/Features/sky_sync")
set(SKY_SYNC_RUNTIME_SOURCE
  "${SKY_SYNC_SOURCE_DIR}/SkySyncRuntime.cpp")
set(SKY_SYNC_RUNTIME_HEADER
  "${SKY_SYNC_SOURCE_DIR}/SkySyncRuntime.h")
set(SKY_SYNC_SETTINGS_HEADER
  "${SKY_SYNC_SOURCE_DIR}/SkySyncSettings.h")
set(SKY_SYNC_SETTINGS_STORE_SOURCE
  "${SKY_SYNC_SOURCE_DIR}/SkySyncSettingsStore.cpp")
set(SKY_SYNC_SETTINGS_STORE_HEADER
  "${SKY_SYNC_SOURCE_DIR}/SkySyncSettingsStore.h")
set(FILMIC_TONEMAPPING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingRuntime.cpp")
set(HDR_OUTPUT_PROBE_SOURCE
  "${SOURCE_DIR}/diagnostics/HdrOutputProbe.cpp")
set(HDR_OUTPUT_PROBE_HEADER
  "${SOURCE_DIR}/diagnostics/HdrOutputProbe.h")
set(FILMIC_TONEMAPPING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingRuntime.h")
set(FILMIC_TONEMAPPING_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettings.h")
set(FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettingsStore.cpp")
set(FILMIC_TONEMAPPING_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h")
set(FILMIC_TONEMAPPING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/FilmicTonemapping/FilmicTonemappingPS.hlsl")
set(FILMIC_TONEMAPPING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_filmic_tonemapping_shader.py")
set(FILMIC_TONEMAPPING_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyFilmicTonemapping.cmake")
set_source_files_properties(
  "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(BLOOM_GLARE_SOURCE_DIR
  "${SOURCE_DIR}/Features/bloom_glare")
set(BLOOM_GLARE_RUNTIME_SOURCE
  "${BLOOM_GLARE_SOURCE_DIR}/BloomGlareRuntime.cpp")
set(BLOOM_GLARE_RUNTIME_HEADER
  "${BLOOM_GLARE_SOURCE_DIR}/BloomGlareRuntime.h")
set(BLOOM_GLARE_SETTINGS_HEADER
  "${BLOOM_GLARE_SOURCE_DIR}/BloomGlareSettings.h")
set(BLOOM_GLARE_SETTINGS_STORE_SOURCE
  "${BLOOM_GLARE_SOURCE_DIR}/BloomGlareSettingsStore.cpp")
set(BLOOM_GLARE_SETTINGS_STORE_HEADER
  "${BLOOM_GLARE_SOURCE_DIR}/BloomGlareSettingsStore.h")
set(BLOOM_GLARE_SHADER_DIR
  "${ROOT_DIR}/package/Shaders/Community/BloomGlare")
set(BLOOM_GLARE_SHADER_SOURCES
  "${BLOOM_GLARE_SHADER_DIR}/BloomGlareCommon.hlsli"
  "${BLOOM_GLARE_SHADER_DIR}/BloomCS.hlsl"
  "${BLOOM_GLARE_SHADER_DIR}/GlareThresholdCS.hlsl"
  "${BLOOM_GLARE_SHADER_DIR}/GlareApertureCS.hlsl"
  "${BLOOM_GLARE_SHADER_DIR}/GlarePsfCS.hlsl"
  "${BLOOM_GLARE_SHADER_DIR}/GlareFftCS.hlsl"
  "${BLOOM_GLARE_SHADER_DIR}/GlareCompositeCS.hlsl")
set(BLOOM_GLARE_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_bloom_glare_shaders.py")
set(BLOOM_GLARE_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyBloomGlare.cmake")
set_source_files_properties(
  ${BLOOM_GLARE_SHADER_SOURCES}
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(SURFACE_CLASSIFICATION_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/surface_classification/SurfaceClassificationRuntime.cpp")
set(SURFACE_CLASSIFICATION_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/surface_classification/SurfaceClassificationRuntime.h")
set(SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.cpp")
set(SURFACE_CLASSIFICATION_GENERATOR
  "${ROOT_DIR}/tools/generate_surface_classification_shaders.py")
set(SURFACE_CLASSIFICATION_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/surface_classification/GeneratedSurfaceClassContracts.inl")
set(SURFACE_CLASSIFICATION_RUNTIME_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifySurfaceClassificationRuntime.cmake")
set(WRAPPED_GRASS_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassRuntime.cpp")
set(WRAPPED_GRASS_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassRuntime.h")
set(WRAPPED_GRASS_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettings.h")
set(WRAPPED_GRASS_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettingsStore.cpp")
set(WRAPPED_GRASS_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/wrapped_grass/WrappedGrassSettingsStore.h")
set(HAIR_SPECULAR_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularRuntime.cpp")
set(HAIR_SPECULAR_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularRuntime.h")
set(HAIR_SPECULAR_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettings.h")
set(HAIR_SPECULAR_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettingsStore.cpp")
set(HAIR_SPECULAR_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/hair_specular/HairSpecularSettingsStore.h")
set(BASIC_WETNESS_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessRuntime.cpp")
set(BASIC_WETNESS_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessRuntime.h")
set(BASIC_WETNESS_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettings.h")
set(BASIC_WETNESS_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettingsStore.cpp")
set(BASIC_WETNESS_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/basic_wetness/BasicWetnessSettingsStore.h")
set(CLOUD_SHADOW_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowRuntime.cpp")
set(CLOUD_SHADOW_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowRuntime.h")
set(CLOUD_SHADOW_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettings.h")
set(CLOUD_SHADOW_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettingsStore.cpp")
set(CLOUD_SHADOW_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowSettingsStore.h")
set(VOLUMETRIC_LIGHTING_SOURCE_DIR
  "${SOURCE_DIR}/Features/volumetric_lighting")
set(VOLUMETRIC_LIGHTING_RUNTIME_SOURCE
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricLightingRuntime.cpp")
set(VOLUMETRIC_LIGHTING_RUNTIME_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricLightingRuntime.h")
set(VOLUMETRIC_LIGHTING_SETTINGS_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricLightingSettings.h")
set(VOLUMETRIC_LIGHTING_SETTINGS_STORE_SOURCE
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricLightingSettingsStore.cpp")
set(VOLUMETRIC_LIGHTING_SETTINGS_STORE_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricLightingSettingsStore.h")
set(VOLUMETRIC_LIGHTING_ENGINE_DATA_SOURCE
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricEngineData.cpp")
set(VOLUMETRIC_LIGHTING_ENGINE_DATA_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricEngineData.h")
set(VOLUMETRIC_LIGHTING_STATE_SCOPE_SOURCE
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricStateScope.cpp")
set(VOLUMETRIC_LIGHTING_STATE_SCOPE_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricStateScope.h")
set(VOLUMETRIC_LIGHTING_OUTPUT_QUALIFICATION_HEADER
  "${VOLUMETRIC_LIGHTING_SOURCE_DIR}/VolumetricOutputQualification.h")
set(VOLUMETRIC_SHADER_DIR
  "${ROOT_DIR}/package/Shaders/Community/VolumetricLighting")
set(VOLUMETRIC_COMMON_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/VolumetricCommon.hlsli")
set(VOLUMETRIC_HOST_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/HostPassThroughPS.hlsl")
set(VOLUMETRIC_FULLSCREEN_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/FullscreenVS.hlsl")
set(VOLUMETRIC_GENERATE_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/GenerateVolumeCS.hlsl")
set(VOLUMETRIC_INTEGRATE_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/IntegrateVolumeCS.hlsl")
set(VOLUMETRIC_QUALIFY_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/QualifyVolumeCS.hlsl")
set(VOLUMETRIC_RESOLVE_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/ResolveVolumePS.hlsl")
set(VOLUMETRIC_TEMPORAL_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/TemporalVolumePS.hlsl")
set(VOLUMETRIC_BLUR_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/BlurVolumePS.hlsl")
set(VOLUMETRIC_COMPOSITE_SHADER_SOURCE
  "${VOLUMETRIC_SHADER_DIR}/CompositeVolumePS.hlsl")
set_source_files_properties(
  "${VOLUMETRIC_COMMON_SHADER_SOURCE}"
  "${VOLUMETRIC_HOST_SHADER_SOURCE}"
  "${VOLUMETRIC_FULLSCREEN_SHADER_SOURCE}"
  "${VOLUMETRIC_GENERATE_SHADER_SOURCE}"
  "${VOLUMETRIC_INTEGRATE_SHADER_SOURCE}"
  "${VOLUMETRIC_QUALIFY_SHADER_SOURCE}"
  "${VOLUMETRIC_RESOLVE_SHADER_SOURCE}"
  "${VOLUMETRIC_TEMPORAL_SHADER_SOURCE}"
  "${VOLUMETRIC_BLUR_SHADER_SOURCE}"
  "${VOLUMETRIC_COMPOSITE_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(SUBSURFACE_SCATTERING_RUNTIME_SOURCE
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringRuntime.cpp")
set(SUBSURFACE_SCATTERING_RUNTIME_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringRuntime.h")
set(SUBSURFACE_SCATTERING_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettings.h")
set(SUBSURFACE_SCATTERING_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.cpp")
set(SUBSURFACE_SCATTERING_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h")
set(SUBSURFACE_SCATTERING_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/SubsurfaceScattering/SubsurfaceScatteringCS.hlsl")
set(SUBSURFACE_SCATTERING_SHADER_GENERATOR
  "${ROOT_DIR}/tools/generate_subsurface_scattering_shader.py")
set_source_files_properties(
  "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  PROPERTIES HEADER_FILE_ONLY TRUE)
set(COMPLEX_MATERIAL_PRODUCER_MODEL
  "${SOURCE_DIR}/Features/complex_materials/ComplexMaterialProducerModel.h")
set(COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_complex_material_producer_contracts.py")
set(COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/complex_materials/GeneratedComplexMaterialProducerContracts.inl")
set(COMPLEX_ENVIRONMENT_MODEL
  "${SOURCE_DIR}/Features/complex_materials/ComplexEnvironmentMaterialModel.h")
set(COMPLEX_ENVIRONMENT_GENERATOR
  "${ROOT_DIR}/tools/generate_complex_environment_contracts.py")
set(COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/complex_materials/GeneratedComplexEnvironmentContracts.inl")
set(COMPLEX_ENVIRONMENT_HLSL
  "${ROOT_DIR}/package/Shaders/Community/ComplexMaterials/ComplexEnvironmentMaterials.hlsli")
set_source_files_properties(
  "${COMPLEX_ENVIRONMENT_HLSL}" PROPERTIES HEADER_FILE_ONLY TRUE)
set(COMPLEX_PARALLAX_HLSL_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/Reconstruction/LandscapeFourLayerSixMrt_L3_00000023.LinearLightingCandidate.hlsl")
set(COMPLEX_PARALLAX_SETTINGS_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettings.h")
set(COMPLEX_PARALLAX_MODEL_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxModel.h")
set(COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettingsStore.cpp")
set(COMPLEX_PARALLAX_SETTINGS_STORE_HEADER
  "${SOURCE_DIR}/Features/complex_materials/ComplexParallaxSettingsStore.h")
set(COMPLEX_PARALLAX_TEST_SOURCE
  "${ROOT_DIR}/tests/ComplexParallaxModelTests.cpp")
set(COMPLEX_PARALLAX_VERIFY_SCRIPT
  "${ROOT_DIR}/cmake/VerifyComplexParallax.cmake")
set(IBL_MATERIAL_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/IblMaterialBlendTemplate.hlsl")
set_source_files_properties(
  "${IBL_MATERIAL_SHADER_SOURCE}" PROPERTIES HEADER_FILE_ONLY TRUE)
file(GLOB IBL_MATERIAL_SHADER_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/IBLMaterial/*.dxbc")
file(GLOB IBL_MATERIAL_VERIFIED_ASSETS CONFIGURE_DEPENDS
  "${ROOT_DIR}/package/Shaders/Community/VerifiedIBLMaterial/*.dxbc")
set(IBL_PROJECTION_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/DiffuseIblProjectionCS.hlsl")
set(IBL_PROJECTION_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/DiffuseIblProjectionCS.dxbc")
set(IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/UpdateEnvironmentCS.hlsl")
set(IBL_ENVIRONMENT_UPDATE_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/UpdateEnvironmentCS.dxbc")
set(IBL_ENVIRONMENT_FILTER_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/FilterEnvironmentCS.hlsl")
set(IBL_ENVIRONMENT_FILTER_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/FilterEnvironmentCS.dxbc")
set(IBL_ENVIRONMENT_REDUCTION_SHADER_SOURCE
  "${ROOT_DIR}/package/Shaders/Community/IBL/ReduceEnvironmentCS.hlsl")
set(IBL_ENVIRONMENT_REDUCTION_SHADER_ASSET
  "${ROOT_DIR}/package/Shaders/Community/IBL/ReduceEnvironmentCS.dxbc")
set(IBL_PROJECTION_SHADER_VERIFIER
  "${ROOT_DIR}/tools/verify_ibl_projection_shader.py")
set(GEOMETRY_HOOK_SOURCE "${RENDERER_HOOK_SOURCE}")
set(VANILLA_PALM_SETTINGS_SOURCE "${PALM_SETTINGS_SOURCE}")
set(VANILLA_SHARED_SETTINGS_SOURCE "${SHARED_SETTINGS_RUNTIME_SOURCE}")
include("${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake")
include("${AUTHORED_PBR_VERIFY_SCRIPT}")
include("${DLAA_VERIFY_SCRIPT}")
include("${VANILLA_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyIblFoundation.cmake")
include("${CONTACT_SHADOW_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake")
include("${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake")
include("${PALM_SETTINGS_VERIFY_SCRIPT}")
include("${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake")
include("${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake")
include("${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake")
include("${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  "${RENDERER_HOOK_SOURCE}"
  "${DFPREPASS_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyBSDFPrePassHook.cmake"
  "${PBR_RUNTIME_SOURCE}"
  "${AUTHORED_PBR_PACK_BUILDER}"
  "${AUTHORED_PBR_PACK_TEST}"
  "${AUTHORED_PBR_VERIFY_SCRIPT}"
  "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  "${ROOT_DIR}/cmake/TestVerifyFo4VrRendererHookNegative.cmake"
  "${SETTINGS_PATH_SOURCE}"
  "${SETTINGS_PATH_HEADER}"
  "${SHARED_SETTINGS_RUNTIME_SOURCE}"
  "${SHARED_SETTINGS_RUNTIME_HEADER}"
  "${PALM_SETTINGS_VERIFY_SCRIPT}"
  "${D3D11_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  "${DLAA_RUNTIME_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaRuntime.h"
  "${DLAA_SOURCE_DIR}/DlaaSettings.h"
  "${DLAA_SETTINGS_STORE_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaSettingsStore.h"
  "${SOURCE_DIR}/render/Fo4VrCamera.h"
  "${DLAA_ENGINE_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaEngineHooks.h"
  "${DLAA_D3D11_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.h"
  "${DLAA_STREAMLINE_SOURCE}"
  "${DLAA_SOURCE_DIR}/StreamlineBackend.h"
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_DEPTH_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  "${DLAA_VERIFY_SCRIPT}"
  "${ROOT_DIR}/cmake/TestVerifyDlaaNegative.cmake"
  "${ROOT_DIR}/cmake/VerifyStreamlinePayload.ps1"
  "${VANILLA_VERIFY_SCRIPT}"
  "${VANILLA_SHADER_RUNTIME_SOURCE}"
  "${VANILLA_FOCUS_RUNTIME_SOURCE}"
  "${VANILLA_RUNTIME_SOURCE}"
  "${VANILLA_SETTINGS_STORE_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_SOURCE}"
  "${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
  "${VANILLA_IBL_GENERATOR_SOURCE}"
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  "${IBL_RUNTIME_SOURCE}"
  "${IBL_RUNTIME_HEADER}"
  "${IBL_SETTINGS_HEADER}"
  "${IBL_SETTINGS_STORE_SOURCE}"
  "${IBL_SETTINGS_STORE_HEADER}"
  "${IBL_PROVIDER_MODEL}"
  "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
  "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
  "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATER_HEADER}"
  "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
  "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
  "${GPU_TIMING_PROFILER_SOURCE}"
  "${GPU_TIMING_PROFILER_HEADER}"
  "${IBL_PROJECTION_MODEL}"
  "${IBL_CAPTURE_PROBE_MODEL}"
  "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
  "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
  "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
  "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
  "${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
  "${SOURCE_DIR}/Features/ibl/IblMaterialStyle.cpp"
  "${SOURCE_DIR}/Features/ibl/IblMaterialStyle.h"
  "${IBL_MATERIAL_CONTRACT_GENERATOR}"
  "${IBL_MATERIAL_DXBC_TRANSFORM}"
  "${IBL_MATERIAL_GENERATED_CONTRACTS}"
  "${PBR_RUNTIME_SOURCE}"
  "${PBR_RUNTIME_HEADER}"
  "${PBR_SETTINGS_HEADER}"
  "${PBR_SETTINGS_STORE_SOURCE}"
  "${PBR_SETTINGS_STORE_HEADER}"
  "${PBR_MATERIAL_MODEL_HEADER}"
  "${PBR_COMMON_SHADER_SOURCE}"
  "${PBR_DIRECTIONAL_SHADER_SOURCE}"
  "${PBR_MATERIAL_OUTPUT_SHADER_SOURCE}"
  "${SKYLIGHTING_RUNTIME_SOURCE}"
  "${SKYLIGHTING_RUNTIME_HEADER}"
  "${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
  "${SKYLIGHTING_NATIVE_HOOK_HEADER}"
  "${SKYLIGHTING_SETTINGS_HEADER}"
  "${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
  "${SKYLIGHTING_SETTINGS_STORE_HEADER}"
  "${SKYLIGHTING_SHADER_GENERATOR}"
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_SAMPLING_SHADER_SOURCE}"
  "${SKYLIGHTING_RECEIVER_SHADER_SOURCE}"
  "${SKYLIGHTING_SUN_SHADER_SOURCE}"
  "${SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  "${SKYLIGHTING_AMBIENT_HEADER}"
  "${SKYLIGHTING_COMPUTE_HEADER}"
  "${SKY_SYNC_RUNTIME_SOURCE}"
  "${SKY_SYNC_RUNTIME_HEADER}"
  "${SKY_SYNC_SETTINGS_HEADER}"
  "${SKY_SYNC_SETTINGS_STORE_SOURCE}"
  "${SKY_SYNC_SETTINGS_STORE_HEADER}"
  "${HDR_OUTPUT_PROBE_SOURCE}"
  "${HDR_OUTPUT_PROBE_HEADER}"
  "${BLOOM_GLARE_RUNTIME_SOURCE}"
  "${BLOOM_GLARE_RUNTIME_HEADER}"
  "${BLOOM_GLARE_SETTINGS_HEADER}"
  "${BLOOM_GLARE_SETTINGS_STORE_SOURCE}"
  "${BLOOM_GLARE_SETTINGS_STORE_HEADER}"
  "${BLOOM_GLARE_SHADER_GENERATOR}"
  ${BLOOM_GLARE_SHADER_SOURCES}
  "${CONTACT_SHADOW_RUNTIME_SOURCE}"
  "${CONTACT_SHADOW_RUNTIME_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_SHADER_GENERATOR}"
  "${CONTACT_SHADOW_VERIFY_SCRIPT}"
  "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
  "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
  "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_MODEL}"
  "${COMPLEX_ENVIRONMENT_GENERATOR}"
  "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_HLSL}"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_HEADER}"
  "${IBL_MATERIAL_SHADER_SOURCE}"
  ${IBL_MATERIAL_SHADER_ASSETS}
  ${IBL_MATERIAL_VERIFIED_ASSETS}
  "${IBL_CAPTURE_PROBE_VERIFIER}"
  "${IBL_PROJECTION_SHADER_SOURCE}"
  "${IBL_PROJECTION_SHADER_ASSET}"
  "${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}"
  "${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}"
  "${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}"
  "${IBL_ENVIRONMENT_REDUCTION_SHADER_SOURCE}"
  "${IBL_ENVIRONMENT_REDUCTION_SHADER_ASSET}"
  "${IBL_PROJECTION_SHADER_VERIFIER}"
  "${ROOT_DIR}/cmake/VerifyIblFoundation.cmake"
  "${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${LINEAR_LIGHTING_RUNTIME_HEADER}"
  "${POINT_LIGHT_HOOK_SOURCE}"
  "${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake"
  "${PALM_SETTINGS_VERIFY_SCRIPT}"
  "${QUALIFICATION_SOURCE}"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingContractMask.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h"
  "${DFLIGHT_AMBIENT_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h"
  "${DFLIGHT_AMBIENT_CONTRACT_MANIFEST}"
  "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
  "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
  ${DFLIGHT_AMBIENT_SHADER_ASSETS}
  "${SKY_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${SKY_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${SKY_LINEAR_LIGHTING_SHADER_ASSETS}
  ${SKY_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE}"
  "${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
  ${DISTANT_TREE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${DISTANT_TREE_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${PARTICLE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${PARTICLE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${PARTICLE_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${WATER_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${WATER_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${WATER_LINEAR_LIGHTING_SHADER_ASSETS}
  ${WATER_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_ASSETS}
  ${VLS_COMPOSITE_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${EFFECT_LINEAR_LIGHTING_CONTRACT_MANIFEST}"
  "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
  "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${EFFECT_LINEAR_LIGHTING_SHADER_SOURCE}"
  ${EFFECT_LINEAR_LIGHTING_SHADER_ASSETS}
  ${EFFECT_LINEAR_LIGHTING_VERIFIED_ASSETS}
  "${SOURCE_DIR}/support/NearAllocation.h"
  "${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake"
  "${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake"
  "${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake"
  "${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake")

add_custom_target(VerifyFo4VrLoader ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLOADER_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp"
    -P "${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake"
  COMMENT "Verifying FO4VR F4SE loader compatibility contract"
  VERBATIM)

add_custom_target(VerifyFo4VrRendererHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
  COMMENT "Verifying FO4VR renderer hook identity contract"
  VERBATIM)

add_custom_target(VerifyBSDFPrePassHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DDFPREPASS_HOOK_SOURCE=${DFPREPASS_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyBSDFPrePassHook.cmake"
  COMMENT "Verifying FO4VR BSDFPrePass descriptor hook contract"
  VERBATIM)

add_custom_target(VerifyAuthoredPbrRuntime ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPBR_RUNTIME_SOURCE=${PBR_RUNTIME_SOURCE}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DDFPREPASS_HOOK_SOURCE=${DFPREPASS_HOOK_SOURCE}"
    -P "${AUTHORED_PBR_VERIFY_SCRIPT}"
  COMMENT "Verifying lazy authored-PBR material runtime"
  VERBATIM)

add_custom_target(VerifyPALMSettings ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    "-DPROJECT_CMAKE_SOURCE=${ROOT_DIR}/CMakeLists.txt"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DSOURCE_ROOT=${ROOT_DIR}"
    -P "${PALM_SETTINGS_VERIFY_SCRIPT}"
  COMMENT "Verifying PALM settings runtime and package ownership"
  VERBATIM)

add_custom_target(VerifyD3D11Hooks ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake"
  COMMENT "Verifying D3D11 method-detour ownership contract"
  VERBATIM)

add_custom_target(VerifyDlaa ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DDLAA_ENGINE_SOURCE=${DLAA_ENGINE_HOOK_SOURCE}"
    "-DDLAA_D3D_SOURCE=${DLAA_D3D11_HOOK_SOURCE}"
    "-DDLAA_RUNTIME_SOURCE=${DLAA_RUNTIME_SOURCE}"
    "-DDLAA_FRAME_HEADER=${SOURCE_DIR}/render/Fo4VrCamera.h"
    "-DDLAA_STREAMLINE_SOURCE=${DLAA_STREAMLINE_SOURCE}"
    "-DDLAA_SETTINGS_SOURCE=${DLAA_SETTINGS_STORE_SOURCE}"
    "-DUPSCALING_COMPOSE_SHADER_SOURCE=${UPSCALING_COMPOSE_SHADER_SOURCE}"
    "-DUPSCALING_MOTION_SHADER_SOURCE=${UPSCALING_MOTION_SHADER_SOURCE}"
    "-DUPSCALING_REACTIVE_SHADER_SOURCE=${UPSCALING_REACTIVE_SHADER_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRENDERER_D3D_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${DLAA_VERIFY_SCRIPT}"
  COMMAND "${CMAKE_COMMAND}"
    "-DDLAA_VALIDATOR=${DLAA_VERIFY_SCRIPT}"
    "-DDLAA_ENGINE_SOURCE=${DLAA_ENGINE_HOOK_SOURCE}"
    "-DDLAA_D3D_SOURCE=${DLAA_D3D11_HOOK_SOURCE}"
    "-DDLAA_RUNTIME_SOURCE=${DLAA_RUNTIME_SOURCE}"
    "-DDLAA_FRAME_HEADER=${SOURCE_DIR}/render/Fo4VrCamera.h"
    "-DDLAA_STREAMLINE_SOURCE=${DLAA_STREAMLINE_SOURCE}"
    "-DDLAA_SETTINGS_SOURCE=${DLAA_SETTINGS_STORE_SOURCE}"
    "-DUPSCALING_COMPOSE_SHADER_SOURCE=${UPSCALING_COMPOSE_SHADER_SOURCE}"
    "-DUPSCALING_MOTION_SHADER_SOURCE=${UPSCALING_MOTION_SHADER_SOURCE}"
    "-DUPSCALING_REACTIVE_SHADER_SOURCE=${UPSCALING_REACTIVE_SHADER_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRENDERER_D3D_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/TestVerifyDlaaNegative.cmake"
  COMMENT "Verifying FO4VR-native DLAA ownership and negative fixtures"
  VERBATIM)

add_custom_target(VerifyStreamlinePayload ALL
  COMMAND "${POWERSHELL_EXECUTABLE}"
    -NoProfile
    -NonInteractive
    -ExecutionPolicy Bypass
    -File "${ROOT_DIR}/cmake/VerifyStreamlinePayload.ps1"
    -RuntimeDirectory "${STREAMLINE_RUNTIME_DIR}"
  COMMENT "Verifying NVIDIA signatures for Streamline DLAA payload"
  VERBATIM)

add_executable(VanillaReflectionPatchTool
  "${VANILLA_REFLECTION_PATCH_TOOL_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_SOURCE}"
  "${VANILLA_REFLECTION_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h")
target_compile_features(VanillaReflectionPatchTool PRIVATE cxx_std_23)
target_include_directories(VanillaReflectionPatchTool PRIVATE "${SOURCE_DIR}")
target_compile_options(VanillaReflectionPatchTool PRIVATE
  /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)

add_custom_target(VerifyIblFoundation ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DIBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
    "-DIBL_RUNTIME_HEADER=${IBL_RUNTIME_HEADER}"
    "-DIBL_SETTINGS_HEADER=${IBL_SETTINGS_HEADER}"
    "-DIBL_SETTINGS_STORE_SOURCE=${IBL_SETTINGS_STORE_SOURCE}"
    "-DIBL_SETTINGS_STORE_HEADER=${IBL_SETTINGS_STORE_HEADER}"
    "-DIBL_PROVIDER_MODEL=${IBL_PROVIDER_MODEL}"
    "-DIBL_ENVIRONMENT_PROVIDER_SOURCE=${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "-DIBL_ENVIRONMENT_PROVIDER_HEADER=${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "-DIBL_ENVIRONMENT_UPDATER_SOURCE=${IBL_ENVIRONMENT_UPDATER_SOURCE}"
    "-DIBL_ENVIRONMENT_UPDATER_HEADER=${IBL_ENVIRONMENT_UPDATER_HEADER}"
    "-DIBL_COMPUTE_STATE_SCOPE_SOURCE=${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "-DIBL_COMPUTE_STATE_SCOPE_HEADER=${IBL_COMPUTE_STATE_SCOPE_HEADER}"
    "-DIBL_PROJECTION_MODEL=${IBL_PROJECTION_MODEL}"
    "-DIBL_CAPTURE_PROBE_MODEL=${IBL_CAPTURE_PROBE_MODEL}"
    "-DIBL_SCENE_RADIANCE_PROBE_MODEL=${IBL_SCENE_RADIANCE_PROBE_MODEL}"
    "-DIBL_REFLECTION_FREE_CAPTURE_SOURCE=${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "-DIBL_REFLECTION_FREE_CAPTURE_HEADER=${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
    "-DIBL_MATERIAL_BINDING_SCOPE_SOURCE=${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
    "-DIBL_MATERIAL_BINDING_SCOPE_HEADER=${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
    "-DIBL_MATERIAL_GENERATED_CONTRACTS=${IBL_MATERIAL_GENERATED_CONTRACTS}"
    "-DIBL_PROJECTION_SHADER_SOURCE=${IBL_PROJECTION_SHADER_SOURCE}"
    "-DIBL_PROJECTION_SHADER_ASSET=${IBL_PROJECTION_SHADER_ASSET}"
    "-DIBL_ENVIRONMENT_UPDATE_SHADER_SOURCE=${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}"
    "-DIBL_ENVIRONMENT_UPDATE_SHADER_ASSET=${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}"
    "-DIBL_ENVIRONMENT_FILTER_SHADER_SOURCE=${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}"
    "-DIBL_ENVIRONMENT_FILTER_SHADER_ASSET=${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DGEOMETRY_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyIblFoundation.cmake"
  COMMENT "Verifying FO4VR-native IBL provider and diffuse foundation"
  VERBATIM)

add_custom_target(VerifyIblProjectionShaderAsset ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_PROJECTION_SHADER_VERIFIER}"
    --root "${ROOT_DIR}"
    --check
  COMMENT "Verifying generated IBL projection shader identity"
  VERBATIM)

add_custom_target(VerifyIblCaptureProbeContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_CAPTURE_PROBE_VERIFIER}"
    --root "${ROOT_DIR}"
  COMMENT "Verifying exact DFComposite IBL capture contracts"
  VERBATIM)

add_custom_target(VerifyIblMaterialContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${IBL_MATERIAL_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${IBL_MATERIAL_GENERATED_CONTRACTS}"
    --surface-anchor-tool "$<TARGET_FILE:VanillaReflectionPatchTool>"
    --check
  DEPENDS VanillaReflectionPatchTool
  COMMENT "Verifying exact DFComposite IBL material contracts"
  VERBATIM)

add_custom_target(VerifyComplexMaterialProducerContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying exact FO4VR complex-material producer contracts"
  VERBATIM)

add_custom_target(VerifyComplexEnvironmentContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${COMPLEX_ENVIRONMENT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying descriptor-gated complex environment contracts"
  VERBATIM)

add_custom_target(VerifyLinearLightingConstantScope ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake"
  COMMENT "Verifying Linear Lighting constant-buffer restoration contract"
  VERBATIM)

add_custom_target(VerifyDFTiledPointLightHook ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPOINT_LIGHT_HOOK_SOURCE=${POINT_LIGHT_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake"
  COMMENT "Verifying FO4VR DFTiled point-light producer contract"
  VERBATIM)

add_custom_target(VerifyLinearLightingQualification ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DPLUGIN_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DQUALIFICATION_SOURCE=${QUALIFICATION_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake"
  COMMENT "Verifying unattended Linear Lighting qualification contract"
  VERBATIM)

add_custom_target(VerifyDFLightAmbientReplacement ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DDFLIGHT_AMBIENT_PATCH_HEADER=${DFLIGHT_AMBIENT_PATCH_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDFLightAmbientReplacement.cmake"
  COMMENT "Verifying exact DFLight ambient replacement contract"
  VERBATIM)

add_custom_target(VerifySkyLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DSKY_LINEAR_LIGHTING_SHADER_SOURCE=${SKY_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DCLOUD_SHADOW_RUNTIME_SOURCE=${CLOUD_SHADOW_RUNTIME_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifySkyLinearLighting.cmake"
  COMMENT "Verifying frame-only Sky Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyDistantTreeLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DDISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE=${DISTANT_TREE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyDistantTreeLinearLighting.cmake"
  COMMENT "Verifying frame-only DistantTree Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyParticleLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DPARTICLE_LINEAR_LIGHTING_SHADER_SOURCE=${PARTICLE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyParticleLinearLighting.cmake"
  COMMENT "Verifying frame-only Particle Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyWaterLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DWATER_LINEAR_LIGHTING_SHADER_SOURCE=${WATER_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyWaterLinearLighting.cmake"
  COMMENT "Verifying frame-only Water Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyEffectLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DEFFECT_LINEAR_LIGHTING_SHADER_SOURCE=${EFFECT_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyEffectLinearLighting.cmake"
  COMMENT "Verifying frame-only Effect Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyVLSCompositeLinearLighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DVLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE=${VLS_COMPOSITE_LINEAR_LIGHTING_SHADER_SOURCE}"
    "-DRESOURCE_SOURCE=${RESOURCE_SOURCE}"
    -P "${ROOT_DIR}/cmake/VerifyVLSCompositeLinearLighting.cmake"
  COMMENT "Verifying frame-only VLS composite Linear Lighting ownership"
  VERBATIM)

add_custom_target(VerifyFixedShaderBindingLookup ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_HEADER=${LINEAR_LIGHTING_RUNTIME_HEADER}"
    "-DFIXED_SHADER_BINDING_LOOKUP_HEADER=${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
    -P "${ROOT_DIR}/cmake/VerifyFixedShaderBindingLookup.cmake"
  COMMENT "Verifying allocation-free shader-binding lookup"
  VERBATIM)

set(LINEAR_LIGHTING_CONTRACT_MANIFEST
  "${ROOT_DIR}/package/Shaders/Community/LinearLightingContracts.json")
set(LINEAR_LIGHTING_CONTRACT_GENERATOR
  "${ROOT_DIR}/tools/generate_linear_lighting_runtime_contracts.py")
set(LINEAR_LIGHTING_GENERATED_CONTRACTS
  "${SOURCE_DIR}/Features/linear_lighting/GeneratedLinearLightingContracts.inl")
add_custom_target(VerifyLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying generated Linear Lighting shader identities"
  VERBATIM)

add_custom_target(VerifyDFLightAmbientContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying generated DFLight ambient shader identities"
  VERBATIM)

add_custom_target(VerifySkyLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Sky Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyDistantTreeLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
    --check
  COMMENT "Verifying FO4VR DistantTree Linear Lighting contract"
  VERBATIM)

add_custom_target(VerifyParticleLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Particle Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyWaterLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Water Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyEffectLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
    --check
  COMMENT "Verifying FO4VR Effect Linear Lighting contracts"
  VERBATIM)

add_custom_target(VerifyVLSCompositeLinearLightingContracts ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
    --root "${ROOT_DIR}"
    --output "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
    --check
  COMMENT "Verifying FO4VR VLS composite Linear Lighting contract"
  VERBATIM)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)

add_subdirectory(
  "${COMMON_LIB_F4VR_PATH_SELECTED}"
  "CommonLibF4"
  EXCLUDE_FROM_ALL)

find_program(CSAH_FXC_EXECUTABLE
  NAMES fxc.exe fxc
  HINTS
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
  REQUIRED)
set(VANILLA_FIXES_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/VanillaFixes")
set(VANILLA_SAO_BLUR_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoBlurHCS.dxbc")
set(VANILLA_SAO_BLUR_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoBlurHCS.h")
set(VANILLA_SAO_RAW_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoRawAOCS.dxbc")
set(VANILLA_SAO_RAW_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSaoRawAOCS.h")
set(VANILLA_SSLR_BLUR_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrBlurHVS.dxbc")
set(VANILLA_SSLR_BLUR_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrBlurHVS.h")
set(VANILLA_SSLR_PREPASS_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrPrepassPS.dxbc")
set(VANILLA_SSLR_PREPASS_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrPrepassPS.h")
set(VANILLA_SSLR_RAYTRACE_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrRaytracePS.dxbc")
set(VANILLA_SSLR_RAYTRACE_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesSslrRaytracePS.h")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesDirectionalDiagnosticCompositePS.dxbc")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesDirectionalDiagnosticCompositePS.h")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesDirectionalDiagnosticCoverageVS.dxbc")
set(VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesDirectionalDiagnosticCoverageVS.h")
set(VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipDiffusePS.dxbc")
set(VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipDiffusePS.h")
set(VANILLA_LIGHTING_OWNERSHIP_SPECULAR_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipSpecularPS.dxbc")
set(VANILLA_LIGHTING_OWNERSHIP_SPECULAR_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipSpecularPS.h")
set(VANILLA_LIGHTING_OWNERSHIP_BLACK_BINARY
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipBlackPS.dxbc")
set(VANILLA_LIGHTING_OWNERSHIP_BLACK_HEADER
  "${VANILLA_FIXES_GENERATED_DIR}/VanillaFixesLightingOwnershipBlackPS.h")
add_custom_command(
  OUTPUT
    "${VANILLA_SAO_BLUR_BINARY}" "${VANILLA_SAO_BLUR_HEADER}"
    "${VANILLA_SAO_RAW_BINARY}" "${VANILLA_SAO_RAW_HEADER}"
    "${VANILLA_SSLR_BLUR_BINARY}" "${VANILLA_SSLR_BLUR_HEADER}"
    "${VANILLA_SSLR_PREPASS_BINARY}" "${VANILLA_SSLR_PREPASS_HEADER}"
    "${VANILLA_SSLR_RAYTRACE_BINARY}" "${VANILLA_SSLR_RAYTRACE_HEADER}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_HEADER}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_BLACK_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_BLACK_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${VANILLA_FIXES_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SAO_BLUR_BINARY}"
    "/Fh${VANILLA_SAO_BLUR_HEADER}"
    /Vncsah_vanilla_sao_blur_h_cs
    "${VANILLA_SAO_BLUR_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SAO_RAW_BINARY}"
    "/Fh${VANILLA_SAO_RAW_HEADER}"
    /Vncsah_vanilla_sao_raw_ao_cs
    "${VANILLA_SAO_RAW_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T vs_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SSLR_BLUR_BINARY}"
    "/Fh${VANILLA_SSLR_BLUR_HEADER}"
    /Vncsah_vanilla_sslr_blur_h_vs
    "${VANILLA_SSLR_BLUR_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges
    "/Fo${VANILLA_SSLR_PREPASS_BINARY}"
    "/Fh${VANILLA_SSLR_PREPASS_HEADER}"
    /Vncsah_vanilla_sslr_prepass_ps
    "${VANILLA_SSLR_PREPASS_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/Fo${VANILLA_SSLR_RAYTRACE_BINARY}"
    "/Fh${VANILLA_SSLR_RAYTRACE_HEADER}"
    /Vncsah_vanilla_sslr_raytrace_ps
    "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/Fo${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY}"
    "/Fh${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_HEADER}"
    /Vncsah_vanilla_directional_diagnostic_composite_ps
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T vs_5_0 /E main /O3 /Ges /WX
    "/Fo${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY}"
    "/Fh${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_HEADER}"
    /Vncsah_vanilla_directional_diagnostic_coverage_vs
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    /DLIGHTING_OWNERSHIP_MODE=1
    "/Fo${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_BINARY}"
    "/Fh${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_HEADER}"
    /Vncsah_vanilla_lighting_ownership_diffuse_ps
    "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    /DLIGHTING_OWNERSHIP_MODE=2
    "/Fo${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_BINARY}"
    "/Fh${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_HEADER}"
    /Vncsah_vanilla_lighting_ownership_specular_ps
    "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    /DLIGHTING_OWNERSHIP_MODE=5
    "/Fo${VANILLA_LIGHTING_OWNERSHIP_BLACK_BINARY}"
    "/Fh${VANILLA_LIGHTING_OWNERSHIP_BLACK_HEADER}"
    /Vncsah_vanilla_lighting_ownership_black_ps
    "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  DEPENDS
    "${VANILLA_SAO_BLUR_SOURCE}"
    "${VANILLA_SAO_RAW_SOURCE}"
    "${VANILLA_SSLR_BLUR_SOURCE}"
    "${VANILLA_SSLR_PREPASS_SOURCE}"
    "${VANILLA_SSLR_RAYTRACE_SOURCE}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
    "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  COMMENT "Compiling stereo-safe Vanilla Fixes shaders"
  VERBATIM)
add_custom_target(VanillaFixesShaders
  DEPENDS
    "${VANILLA_SAO_BLUR_BINARY}" "${VANILLA_SAO_BLUR_HEADER}"
    "${VANILLA_SAO_RAW_BINARY}" "${VANILLA_SAO_RAW_HEADER}"
    "${VANILLA_SSLR_BLUR_BINARY}" "${VANILLA_SSLR_BLUR_HEADER}"
    "${VANILLA_SSLR_PREPASS_BINARY}" "${VANILLA_SSLR_PREPASS_HEADER}"
    "${VANILLA_SSLR_RAYTRACE_BINARY}" "${VANILLA_SSLR_RAYTRACE_HEADER}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_HEADER}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY}"
    "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_DIFFUSE_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_SPECULAR_HEADER}"
    "${VANILLA_LIGHTING_OWNERSHIP_BLACK_BINARY}"
    "${VANILLA_LIGHTING_OWNERSHIP_BLACK_HEADER}")
add_custom_command(
  OUTPUT
    "${UPSCALING_COMPOSE_SHADER_BINARY}"
    "${UPSCALING_COMPOSE_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges
    "/Fo${UPSCALING_COMPOSE_SHADER_BINARY}"
    "/Fh${UPSCALING_COMPOSE_SHADER_HEADER}"
    /Vncsah_upscaling_compose_cs
    "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  COMMENT "Compiling stereo upscaling compositor"
  VERBATIM)
add_custom_command(
  OUTPUT
    "${UPSCALING_MOTION_SHADER_BINARY}"
    "${UPSCALING_MOTION_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${UPSCALING_MOTION_SHADER_BINARY}"
    "/Fh${UPSCALING_MOTION_SHADER_HEADER}"
    /Vncsah_upscaling_motion_repair_cs
    "${UPSCALING_MOTION_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_MOTION_SHADER_SOURCE}"
  COMMENT "Compiling per-eye FO4VR motion-vector repair"
  VERBATIM)
add_custom_command(
  OUTPUT
    "${UPSCALING_REACTIVE_SHADER_BINARY}"
    "${UPSCALING_REACTIVE_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${UPSCALING_REACTIVE_SHADER_BINARY}"
    "/Fh${UPSCALING_REACTIVE_SHADER_HEADER}"
    /Vncsah_upscaling_reactive_mask_cs
    "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  COMMENT "Compiling FO4VR TAA-derived DLSS reactive mask"
  VERBATIM)
add_custom_command(
  OUTPUT "${UPSCALING_DEPTH_SHADER_BINARY}" "${UPSCALING_DEPTH_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}" /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${UPSCALING_DEPTH_SHADER_BINARY}" "/Fh${UPSCALING_DEPTH_SHADER_HEADER}"
    /Vncsah_upscaling_depth_cs "${UPSCALING_DEPTH_SHADER_SOURCE}"
  DEPENDS "${UPSCALING_DEPTH_SHADER_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/Common/Fo4VrDepth.hlsli"
  COMMENT "Compiling stereo world/first-person depth preparation"
  VERBATIM)
foreach(stage IN ITEMS VS PS)
  string(TOLOWER "${stage}" profile)
  add_custom_command(
    OUTPUT "${DLAA_GENERATED_DIR}/GeneratedUpscalingDepth${stage}.h"
      "${DLAA_GENERATED_DIR}/PublishDepth${stage}.dxbc"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLAA_GENERATED_DIR}"
    COMMAND "${CSAH_FXC_EXECUTABLE}" /nologo /T "${profile}_5_0" /E "main${stage}" /O3 /Ges /WX
      "/Fo${DLAA_GENERATED_DIR}/PublishDepth${stage}.dxbc"
      "/Fh${DLAA_GENERATED_DIR}/GeneratedUpscalingDepth${stage}.h"
      "/Vncsah_upscaling_depth_${profile}"
      "${ROOT_DIR}/package/Shaders/Community/Upscaling/PublishDepth.hlsl"
    DEPENDS "${ROOT_DIR}/package/Shaders/Community/Upscaling/PublishDepth.hlsl"
    COMMENT "Compiling native stereo depth publication ${stage}"
    VERBATIM)
endforeach()
add_custom_target(UpscalingShaders
  DEPENDS
    "${DLAA_GENERATED_DIR}/GeneratedUpscalingDepthVS.h"
    "${DLAA_GENERATED_DIR}/GeneratedUpscalingDepthPS.h"
    "${UPSCALING_DEPTH_SHADER_BINARY}"
    "${UPSCALING_DEPTH_SHADER_HEADER}"
    "${UPSCALING_COMPOSE_SHADER_BINARY}"
    "${UPSCALING_COMPOSE_SHADER_HEADER}"
    "${UPSCALING_MOTION_SHADER_BINARY}"
    "${UPSCALING_MOTION_SHADER_HEADER}"
    "${UPSCALING_REACTIVE_SHADER_BINARY}"
    "${UPSCALING_REACTIVE_SHADER_HEADER}")
add_custom_target(VerifyVanillaFixes ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DVANILLA_SHADER_RUNTIME_SOURCE=${VANILLA_SHADER_RUNTIME_SOURCE}"
    "-DVANILLA_FOCUS_RUNTIME_SOURCE=${VANILLA_FOCUS_RUNTIME_SOURCE}"
    "-DVANILLA_SUN_OCCLUSION_RUNTIME_SOURCE=${VANILLA_SUN_OCCLUSION_RUNTIME_SOURCE}"
    "-DVANILLA_RUNTIME_SOURCE=${VANILLA_RUNTIME_SOURCE}"
    "-DVANILLA_SETTINGS_STORE_SOURCE=${VANILLA_SETTINGS_STORE_SOURCE}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
    "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY}"
    "-DVANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE=${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
    "-DVANILLA_REFLECTION_PATCH_SOURCE=${VANILLA_REFLECTION_PATCH_SOURCE}"
    "-DVANILLA_REFLECTION_PATCH_TOOL_SOURCE=${VANILLA_REFLECTION_PATCH_TOOL_SOURCE}"
    "-DVANILLA_SSLR_ENVIRONMENT_SOURCE=${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
    "-DVANILLA_IBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
    "-DVANILLA_D3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DVANILLA_PALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    "-DVANILLA_SHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DVANILLA_PLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DVANILLA_IBL_GENERATOR_SOURCE=${VANILLA_IBL_GENERATOR_SOURCE}"
    "-DVANILLA_SAO_BLUR_SOURCE=${VANILLA_SAO_BLUR_SOURCE}"
    "-DVANILLA_SAO_RAW_SOURCE=${VANILLA_SAO_RAW_SOURCE}"
    "-DVANILLA_SSLR_BLUR_SOURCE=${VANILLA_SSLR_BLUR_SOURCE}"
    "-DVANILLA_SSLR_PREPASS_SOURCE=${VANILLA_SSLR_PREPASS_SOURCE}"
    "-DVANILLA_SSLR_RAYTRACE_SOURCE=${VANILLA_SSLR_RAYTRACE_SOURCE}"
    "-DVANILLA_FXC_EXECUTABLE=${CSAH_FXC_EXECUTABLE}"
    "-DVANILLA_SAO_BLUR_BINARY=${VANILLA_SAO_BLUR_BINARY}"
    "-DVANILLA_SAO_RAW_BINARY=${VANILLA_SAO_RAW_BINARY}"
    "-DVANILLA_SSLR_BLUR_BINARY=${VANILLA_SSLR_BLUR_BINARY}"
    "-DVANILLA_SSLR_PREPASS_BINARY=${VANILLA_SSLR_PREPASS_BINARY}"
    "-DVANILLA_SSLR_RAYTRACE_BINARY=${VANILLA_SSLR_RAYTRACE_BINARY}"
    -P "${VANILLA_VERIFY_SCRIPT}"
  DEPENDS VanillaFixesShaders
  COMMENT "Verifying Vanilla Fixes native, shader, settings, and PALM settings contracts"
  VERBATIM)
add_custom_target(VerifyNativeShadows ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DNATIVE_SHADOW_RUNTIME_SOURCE=${NATIVE_SHADOW_RUNTIME_SOURCE}"
    "-DNATIVE_SHADOW_RUNTIME_HEADER=${NATIVE_SHADOW_RUNTIME_HEADER}"
    "-DNATIVE_SHADOW_SETTINGS_HEADER=${NATIVE_SHADOW_SETTINGS_HEADER}"
    "-DNATIVE_SHADOW_SETTINGS_STORE_SOURCE=${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
    "-DNATIVE_SHADOW_PATCH_MODEL=${NATIVE_SHADOW_PATCH_MODEL}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    -P "${NATIVE_SHADOW_VERIFY_SCRIPT}"
  COMMENT "Verifying fixed native shadow contracts"
  VERBATIM)
add_custom_target(VerifySurfaceClassification ALL
  COMMAND "${Python3_EXECUTABLE}"
    "${SURFACE_CLASSIFICATION_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --check
  COMMAND "${CMAKE_COMMAND}"
    -DSURFACE_CLASSIFICATION_RUNTIME_SOURCE=${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}
    -DSURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE=${SURFACE_CLASSIFICATION_LINEAR_RUNTIME_SOURCE}
    -DSURFACE_CLASSIFICATION_GENERATOR=${SURFACE_CLASSIFICATION_GENERATOR}
    -P "${SURFACE_CLASSIFICATION_RUNTIME_VERIFY_SCRIPT}"
  COMMENT "Verifying exact material surface-classification shaders"
  VERBATIM)
set(CONTACT_SHADOW_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ContactShadows")
set(CONTACT_SHADOW_SHADER_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowsDFLight.dxbc")
set(CONTACT_SHADOW_SHADER_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowsDFLight.h")
set(CONTACT_SHADOW_MASK_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowMaskCS.dxbc")
set(CONTACT_SHADOW_MASK_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowMaskCS.h")
set(CONTACT_SHADOW_DISPATCH_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowDispatchCS.dxbc")
set(CONTACT_SHADOW_DISPATCH_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowDispatchCS.h")
set(CONTACT_SHADOW_RESOLVE_BINARY
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowResolveCS.dxbc")
set(CONTACT_SHADOW_RESOLVE_HEADER
  "${CONTACT_SHADOW_GENERATED_DIR}/ContactShadowResolveCS.h")
add_custom_command(
  OUTPUT
    "${CONTACT_SHADOW_SHADER_BINARY}"
    "${CONTACT_SHADOW_SHADER_HEADER}"
    "${CONTACT_SHADOW_MASK_BINARY}"
    "${CONTACT_SHADOW_MASK_HEADER}"
    "${CONTACT_SHADOW_DISPATCH_BINARY}"
    "${CONTACT_SHADOW_DISPATCH_HEADER}"
    "${CONTACT_SHADOW_RESOLVE_BINARY}"
    "${CONTACT_SHADOW_RESOLVE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${CONTACT_SHADOW_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --binary "${CONTACT_SHADOW_SHADER_BINARY}"
    --header "${CONTACT_SHADOW_SHADER_HEADER}"
    --compute-binary "${CONTACT_SHADOW_MASK_BINARY}"
    --compute-header "${CONTACT_SHADOW_MASK_HEADER}"
    --dispatch-binary "${CONTACT_SHADOW_DISPATCH_BINARY}"
    --dispatch-header "${CONTACT_SHADOW_DISPATCH_HEADER}"
    --resolve-binary "${CONTACT_SHADOW_RESOLVE_BINARY}"
    --resolve-header "${CONTACT_SHADOW_RESOLVE_HEADER}"
  DEPENDS
    "${CONTACT_SHADOW_SHADER_GENERATOR}"
    "${CONTACT_SHADOW_SHADER_SOURCE}"
    "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
    "${SKYLIGHTING_SAMPLING_SHADER_SOURCE}"
    "${SKYLIGHTING_RECEIVER_SHADER_SOURCE}"
    "${SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/WaterEffects/WaterCaustics.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainShadows/TerrainShadows.hlsli"
    "${CONTACT_SHADOW_HAIR_SHADER_SOURCE}"
    "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
    "${PBR_DIRECTIONAL_SHADER_SOURCE}"
    "${PBR_COMMON_SHADER_SOURCE}"
    "${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
    "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
    "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/CloudShadows/CloudProjection.hlsli"
    "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
    "${ROOT_DIR}/tools/dxbc_transform.py"
    "${ROOT_DIR}/Shaders012_VR.fxp"
  COMMENT "Generating exact stereo Contact Shadows DFLight shader"
  VERBATIM)
add_custom_target(ContactShadowShader
  DEPENDS
    "${CONTACT_SHADOW_SHADER_BINARY}"
    "${CONTACT_SHADOW_SHADER_HEADER}"
    "${CONTACT_SHADOW_MASK_BINARY}"
    "${CONTACT_SHADOW_MASK_HEADER}"
    "${CONTACT_SHADOW_DISPATCH_BINARY}"
    "${CONTACT_SHADOW_DISPATCH_HEADER}"
    "${CONTACT_SHADOW_RESOLVE_BINARY}"
    "${CONTACT_SHADOW_RESOLVE_HEADER}")
set(VOLUMETRIC_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/VolumetricLighting")
set(VOLUMETRIC_HOST_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricHostPassThroughPS.dxbc")
set(VOLUMETRIC_HOST_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricHostPassThroughPS.h")
set(VOLUMETRIC_FULLSCREEN_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricFullscreenVS.dxbc")
set(VOLUMETRIC_FULLSCREEN_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricFullscreenVS.h")
set(VOLUMETRIC_GENERATE_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricGenerateCS.dxbc")
set(VOLUMETRIC_GENERATE_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricGenerateCS.h")
set(VOLUMETRIC_INTEGRATE_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricIntegrateCS.dxbc")
set(VOLUMETRIC_INTEGRATE_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricIntegrateCS.h")
set(VOLUMETRIC_QUALIFY_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricQualifyCS.dxbc")
set(VOLUMETRIC_QUALIFY_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricQualifyCS.h")
set(VOLUMETRIC_RESOLVE_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricResolvePS.dxbc")
set(VOLUMETRIC_RESOLVE_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricResolvePS.h")
set(VOLUMETRIC_TEMPORAL_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricTemporalPS.dxbc")
set(VOLUMETRIC_TEMPORAL_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricTemporalPS.h")
set(VOLUMETRIC_BLUR_H_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricBlurHPS.dxbc")
set(VOLUMETRIC_BLUR_H_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricBlurHPS.h")
set(VOLUMETRIC_BLUR_V_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricBlurVPS.dxbc")
set(VOLUMETRIC_BLUR_V_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricBlurVPS.h")
set(VOLUMETRIC_COMPOSITE_SHADER_BINARY
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricCompositePS.dxbc")
set(VOLUMETRIC_COMPOSITE_SHADER_HEADER
  "${VOLUMETRIC_GENERATED_DIR}/VolumetricCompositePS.h")
add_custom_command(
  OUTPUT
    "${VOLUMETRIC_HOST_SHADER_BINARY}"
    "${VOLUMETRIC_HOST_SHADER_HEADER}"
    "${VOLUMETRIC_FULLSCREEN_SHADER_BINARY}"
    "${VOLUMETRIC_FULLSCREEN_SHADER_HEADER}"
    "${VOLUMETRIC_GENERATE_SHADER_BINARY}"
    "${VOLUMETRIC_GENERATE_SHADER_HEADER}"
    "${VOLUMETRIC_INTEGRATE_SHADER_BINARY}"
    "${VOLUMETRIC_INTEGRATE_SHADER_HEADER}"
    "${VOLUMETRIC_QUALIFY_SHADER_BINARY}"
    "${VOLUMETRIC_QUALIFY_SHADER_HEADER}"
    "${VOLUMETRIC_RESOLVE_SHADER_BINARY}"
    "${VOLUMETRIC_RESOLVE_SHADER_HEADER}"
    "${VOLUMETRIC_TEMPORAL_SHADER_BINARY}"
    "${VOLUMETRIC_TEMPORAL_SHADER_HEADER}"
    "${VOLUMETRIC_BLUR_H_SHADER_BINARY}"
    "${VOLUMETRIC_BLUR_H_SHADER_HEADER}"
    "${VOLUMETRIC_BLUR_V_SHADER_BINARY}"
    "${VOLUMETRIC_BLUR_V_SHADER_HEADER}"
    "${VOLUMETRIC_COMPOSITE_SHADER_BINARY}"
    "${VOLUMETRIC_COMPOSITE_SHADER_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${VOLUMETRIC_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/Fo${VOLUMETRIC_HOST_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_HOST_SHADER_HEADER}"
    /Vncsah_volumetric_host_ps
    "${VOLUMETRIC_HOST_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T vs_5_0 /E main /O3 /Ges /WX
    "/Fo${VOLUMETRIC_FULLSCREEN_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_FULLSCREEN_SHADER_HEADER}"
    /Vncsah_volumetric_fullscreen_vs
    "${VOLUMETRIC_FULLSCREEN_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/I${VOLUMETRIC_SHADER_DIR}"
    "/Fo${VOLUMETRIC_GENERATE_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_GENERATE_SHADER_HEADER}"
    /Vncsah_volumetric_generate_cs
    "${VOLUMETRIC_GENERATE_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${VOLUMETRIC_INTEGRATE_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_INTEGRATE_SHADER_HEADER}"
    /Vncsah_volumetric_integrate_cs
    "${VOLUMETRIC_INTEGRATE_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T cs_5_0 /E main /O3 /Ges /WX
    "/Fo${VOLUMETRIC_QUALIFY_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_QUALIFY_SHADER_HEADER}"
    /Vncsah_volumetric_qualify_cs
    "${VOLUMETRIC_QUALIFY_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/I${VOLUMETRIC_SHADER_DIR}"
    "/Fo${VOLUMETRIC_RESOLVE_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_RESOLVE_SHADER_HEADER}"
    /Vncsah_volumetric_resolve_ps
    "${VOLUMETRIC_RESOLVE_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/I${VOLUMETRIC_SHADER_DIR}"
    "/Fo${VOLUMETRIC_TEMPORAL_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_TEMPORAL_SHADER_HEADER}"
    /Vncsah_volumetric_temporal_ps
    "${VOLUMETRIC_TEMPORAL_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX /DBLUR_HORIZONTAL=1
    "/Fo${VOLUMETRIC_BLUR_H_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_BLUR_H_SHADER_HEADER}"
    /Vncsah_volumetric_blur_h_ps
    "${VOLUMETRIC_BLUR_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX /DBLUR_HORIZONTAL=0
    "/Fo${VOLUMETRIC_BLUR_V_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_BLUR_V_SHADER_HEADER}"
    /Vncsah_volumetric_blur_v_ps
    "${VOLUMETRIC_BLUR_SHADER_SOURCE}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E main /O3 /Ges /WX
    "/I${VOLUMETRIC_SHADER_DIR}"
    "/Fo${VOLUMETRIC_COMPOSITE_SHADER_BINARY}"
    "/Fh${VOLUMETRIC_COMPOSITE_SHADER_HEADER}"
    /Vncsah_volumetric_composite_ps
    "${VOLUMETRIC_COMPOSITE_SHADER_SOURCE}"
  DEPENDS
    "${VOLUMETRIC_COMMON_SHADER_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/CloudShadows/CloudProjection.hlsli"
    "${VOLUMETRIC_HOST_SHADER_SOURCE}"
    "${VOLUMETRIC_FULLSCREEN_SHADER_SOURCE}"
    "${VOLUMETRIC_GENERATE_SHADER_SOURCE}"
    "${VOLUMETRIC_INTEGRATE_SHADER_SOURCE}"
    "${VOLUMETRIC_QUALIFY_SHADER_SOURCE}"
    "${VOLUMETRIC_RESOLVE_SHADER_SOURCE}"
    "${VOLUMETRIC_TEMPORAL_SHADER_SOURCE}"
    "${VOLUMETRIC_BLUR_SHADER_SOURCE}"
    "${VOLUMETRIC_COMPOSITE_SHADER_SOURCE}"
    "${VOLUMETRIC_SHADER_DIR}/HeightFog.hlsli" "${VOLUMETRIC_SHADER_DIR}/HeightFogMath.hlsli"
  COMMENT "Compiling full stereo Volumetric Lighting suite"
  VERBATIM)
set(HEIGHT_FOG_HEADER "${VOLUMETRIC_GENERATED_DIR}/VolumetricHeightFogPS.h")
add_custom_command(OUTPUT "${HEIGHT_FOG_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${VOLUMETRIC_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}" /nologo /T ps_5_0 /E main /O3 /Ges /WX
    /Vncsah_height_fog_ps /Fh "${HEIGHT_FOG_HEADER}" "${VOLUMETRIC_SHADER_DIR}/HeightFogPS.hlsl"
  DEPENDS "${VOLUMETRIC_SHADER_DIR}/HeightFogPS.hlsl" "${VOLUMETRIC_SHADER_DIR}/HeightFog.hlsli"
    "${VOLUMETRIC_SHADER_DIR}/HeightFogMath.hlsli" VERBATIM)
add_custom_target(HeightFogShader DEPENDS "${HEIGHT_FOG_HEADER}")
add_custom_target(VolumetricLightingShaders
  DEPENDS
    "${VOLUMETRIC_HOST_SHADER_BINARY}"
    "${VOLUMETRIC_HOST_SHADER_HEADER}"
    "${VOLUMETRIC_FULLSCREEN_SHADER_BINARY}"
    "${VOLUMETRIC_FULLSCREEN_SHADER_HEADER}"
    "${VOLUMETRIC_GENERATE_SHADER_BINARY}"
    "${VOLUMETRIC_GENERATE_SHADER_HEADER}"
    "${VOLUMETRIC_INTEGRATE_SHADER_BINARY}"
    "${VOLUMETRIC_INTEGRATE_SHADER_HEADER}"
    "${VOLUMETRIC_QUALIFY_SHADER_BINARY}"
    "${VOLUMETRIC_QUALIFY_SHADER_HEADER}"
    "${VOLUMETRIC_RESOLVE_SHADER_BINARY}"
    "${VOLUMETRIC_RESOLVE_SHADER_HEADER}"
    "${VOLUMETRIC_TEMPORAL_SHADER_BINARY}"
    "${VOLUMETRIC_TEMPORAL_SHADER_HEADER}"
    "${VOLUMETRIC_BLUR_H_SHADER_BINARY}"
    "${VOLUMETRIC_BLUR_H_SHADER_HEADER}"
    "${VOLUMETRIC_BLUR_V_SHADER_BINARY}"
    "${VOLUMETRIC_BLUR_V_SHADER_HEADER}"
    "${VOLUMETRIC_COMPOSITE_SHADER_BINARY}"
    "${VOLUMETRIC_COMPOSITE_SHADER_HEADER}")
set(SKYLIGHTING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/Skylighting")
set(SKYLIGHTING_AMBIENT_HEADER
  "${SKYLIGHTING_GENERATED_DIR}/SkylightingAmbient.h")
set(SKYLIGHTING_COMPUTE_BINARY
  "${SKYLIGHTING_GENERATED_DIR}/UpdateProbesCS.dxbc")
set(SKYLIGHTING_COMPUTE_HEADER
  "${SKYLIGHTING_GENERATED_DIR}/UpdateProbesCS.h")
set(SKYLIGHTING_SUN_BINARY "${SKYLIGHTING_GENERATED_DIR}/UpdateSunProbesCS.dxbc")
set(SKYLIGHTING_SUN_HEADER "${SKYLIGHTING_GENERATED_DIR}/UpdateSunProbesCS.h")
add_custom_command(
  OUTPUT
    "${SKYLIGHTING_AMBIENT_HEADER}"
    "${SKYLIGHTING_SUN_BINARY}"
    "${SKYLIGHTING_SUN_HEADER}"
    "${SKYLIGHTING_COMPUTE_BINARY}"
    "${SKYLIGHTING_COMPUTE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${SKYLIGHTING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --ambient-header "${SKYLIGHTING_AMBIENT_HEADER}"
    --compute-binary "${SKYLIGHTING_COMPUTE_BINARY}"
    --compute-header "${SKYLIGHTING_COMPUTE_HEADER}"
    --sun-compute-binary "${SKYLIGHTING_SUN_BINARY}"
    --sun-compute-header "${SKYLIGHTING_SUN_HEADER}"
  DEPENDS
    "${SKYLIGHTING_SHADER_GENERATOR}"
    "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/ScreenSpaceGI/Ambient.hlsli"
  "${SKYLIGHTING_SAMPLING_SHADER_SOURCE}"
  "${SKYLIGHTING_RECEIVER_SHADER_SOURCE}"
  "${SKYLIGHTING_SUN_SHADER_SOURCE}"
  "${SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE}"
    "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/dxbc_transform.py"
    "${DFLIGHT_AMBIENT_CONTRACT_MANIFEST}"
    ${DFLIGHT_AMBIENT_SHADER_ASSETS}
  COMMENT "Generating stereo world-space Skylighting shaders"
  VERBATIM)
add_custom_target(SkylightingShaders
  DEPENDS
    "${SKYLIGHTING_AMBIENT_HEADER}"
    "${SKYLIGHTING_SUN_BINARY}"
    "${SKYLIGHTING_SUN_HEADER}"
    "${SKYLIGHTING_COMPUTE_BINARY}"
    "${SKYLIGHTING_COMPUTE_HEADER}")
add_custom_target(VerifySkylighting ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DSKYLIGHTING_RUNTIME_SOURCE=${SKYLIGHTING_RUNTIME_SOURCE}"
    "-DSKYLIGHTING_RUNTIME_HEADER=${SKYLIGHTING_RUNTIME_HEADER}"
    "-DSKYLIGHTING_NATIVE_HOOK_SOURCE=${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
    "-DSKYLIGHTING_SETTINGS_HEADER=${SKYLIGHTING_SETTINGS_HEADER}"
    "-DSKYLIGHTING_SETTINGS_STORE_SOURCE=${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
    "-DSKYLIGHTING_AMBIENT_SHADER_SOURCE=${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
    "-DSKYLIGHTING_COMPUTE_SHADER_SOURCE=${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
    "-DSKYLIGHTING_SHADER_GENERATOR=${SKYLIGHTING_SHADER_GENERATOR}"
    "-DSKYLIGHTING_AMBIENT_HEADER=${SKYLIGHTING_AMBIENT_HEADER}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    -P "${SKYLIGHTING_VERIFY_SCRIPT}"
  DEPENDS SkylightingShaders
  COMMENT "Verifying FO4VR-native stereo Skylighting contracts"
  VERBATIM)
set(FILMIC_TONEMAPPING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/FilmicTonemapping")
set(FILMIC_TONEMAPPING_BASE_SHADER_BINARY
  "${FILMIC_TONEMAPPING_GENERATED_DIR}/FilmicTonemappingBasePS.dxbc")
set(FILMIC_TONEMAPPING_FADE_SHADER_BINARY
  "${FILMIC_TONEMAPPING_GENERATED_DIR}/FilmicTonemappingFadePS.dxbc")
set(FILMIC_TONEMAPPING_SHADER_HEADER
  "${FILMIC_TONEMAPPING_GENERATED_DIR}/FilmicTonemappingPS.h")
add_custom_command(
  OUTPUT
    "${FILMIC_TONEMAPPING_BASE_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_FADE_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --base-binary "${FILMIC_TONEMAPPING_BASE_SHADER_BINARY}"
    --fade-binary "${FILMIC_TONEMAPPING_FADE_SHADER_BINARY}"
    --header "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  DEPENDS
    "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
    "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
    "${ROOT_DIR}/Shaders012_VR.fxp"
  COMMENT "Compiling exact FO4VR base/fade filmic HDR replacements"
  VERBATIM)
add_custom_target(FilmicTonemappingShader
  DEPENDS
    "${FILMIC_TONEMAPPING_BASE_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_FADE_SHADER_BINARY}"
    "${FILMIC_TONEMAPPING_SHADER_HEADER}")
add_custom_target(VerifyFilmicTonemapping ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DFILMIC_RUNTIME_SOURCE=${FILMIC_TONEMAPPING_RUNTIME_SOURCE}"
    "-DFILMIC_RUNTIME_HEADER=${FILMIC_TONEMAPPING_RUNTIME_HEADER}"
    "-DFILMIC_SETTINGS_HEADER=${FILMIC_TONEMAPPING_SETTINGS_HEADER}"
    "-DFILMIC_SETTINGS_STORE_SOURCE=${FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE}"
    "-DFILMIC_SHADER_SOURCE=${FILMIC_TONEMAPPING_SHADER_SOURCE}"
    "-DFILMIC_SHADER_GENERATOR=${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    -P "${FILMIC_TONEMAPPING_VERIFY_SCRIPT}"
  COMMENT "Verifying filmic tonemapping output-stage ownership"
  VERBATIM)
set(STEREO_MEASUREMENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/StereoMeasurement")
set(STEREO_MEASUREMENT_HEADER "${STEREO_MEASUREMENT_DIR}/StereoMeasurementCS.h")
add_custom_command(OUTPUT "${STEREO_MEASUREMENT_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${STEREO_MEASUREMENT_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}" /nologo /T cs_5_0 /E main /O3 /Ges /WX
    /Vncsah_stereo_measurement_cs /Fh "${STEREO_MEASUREMENT_HEADER}"
    /Fo "${STEREO_MEASUREMENT_DIR}/StereoMeasurementCS.dxbc"
    "${ROOT_DIR}/package/Shaders/Community/StereoMeasurement/CompareCS.hlsl"
  DEPENDS "${ROOT_DIR}/package/Shaders/Community/StereoMeasurement/CompareCS.hlsl"
  VERBATIM)
add_custom_target(StereoMeasurementShader DEPENDS "${STEREO_MEASUREMENT_HEADER}")

set(BLOOM_GLARE_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/BloomGlare")
set(BLOOM_GLARE_SHADER_HEADER
  "${BLOOM_GLARE_GENERATED_DIR}/BloomGlareShaders.h")
add_custom_command(
  OUTPUT "${BLOOM_GLARE_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${BLOOM_GLARE_SHADER_GENERATOR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --source-dir "${BLOOM_GLARE_SHADER_DIR}"
    --header "${BLOOM_GLARE_SHADER_HEADER}"
  DEPENDS
    "${BLOOM_GLARE_SHADER_GENERATOR}"
    ${BLOOM_GLARE_SHADER_SOURCES}
  COMMENT "Compiling stereo HDR bloom and physical-glare shaders"
  VERBATIM)
add_custom_target(BloomGlareShaders
  DEPENDS "${BLOOM_GLARE_SHADER_HEADER}")
add_custom_target(VerifyBloomGlare ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DBLOOM_GLARE_RUNTIME_SOURCE=${BLOOM_GLARE_RUNTIME_SOURCE}"
    "-DBLOOM_GLARE_RUNTIME_HEADER=${BLOOM_GLARE_RUNTIME_HEADER}"
    "-DBLOOM_GLARE_SETTINGS_HEADER=${BLOOM_GLARE_SETTINGS_HEADER}"
    "-DBLOOM_GLARE_SETTINGS_STORE_SOURCE=${BLOOM_GLARE_SETTINGS_STORE_SOURCE}"
    "-DBLOOM_GLARE_SHADER_GENERATOR=${BLOOM_GLARE_SHADER_GENERATOR}"
    "-DBLOOM_GLARE_SHADER_DIR=${BLOOM_GLARE_SHADER_DIR}"
    "-DFILMIC_RUNTIME_SOURCE=${FILMIC_TONEMAPPING_RUNTIME_SOURCE}"
    "-DFILMIC_SHADER_SOURCE=${FILMIC_TONEMAPPING_SHADER_SOURCE}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
    "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    -P "${BLOOM_GLARE_VERIFY_SCRIPT}"
  DEPENDS BloomGlareShaders FilmicTonemappingShader
  COMMENT "Verifying stereo Bloom and Physical Glare contracts"
  VERBATIM)
set(SUBSURFACE_SCATTERING_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/SubsurfaceScattering")
set(SUBSURFACE_SCATTERING_SHADER_BINARY
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}/SubsurfaceScatteringCS.dxbc")
set(SUBSURFACE_SCATTERING_SHADER_HEADER
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}/SubsurfaceScatteringCS.h")
add_custom_command(
  OUTPUT
    "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${SUBSURFACE_SCATTERING_SHADER_GENERATOR}"
    --root "${ROOT_DIR}"
    --fxc "${CSAH_FXC_EXECUTABLE}"
    --binary "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    --header "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  DEPENDS
    "${SUBSURFACE_SCATTERING_SHADER_GENERATOR}"
    "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  COMMENT "Compiling stereo-safe subsurface scattering compute pass"
  VERBATIM)
add_custom_target(SubsurfaceScatteringShader
  DEPENDS
    "${SUBSURFACE_SCATTERING_SHADER_BINARY}"
    "${SUBSURFACE_SCATTERING_SHADER_HEADER}")
add_custom_target(VerifyContactShadows ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DCONTACT_SHADOW_RUNTIME_SOURCE=${CONTACT_SHADOW_RUNTIME_SOURCE}"
    "-DCONTACT_SHADOW_RUNTIME_HEADER=${CONTACT_SHADOW_RUNTIME_HEADER}"
    "-DCONTACT_SHADOW_SETTINGS_HEADER=${CONTACT_SHADOW_SETTINGS_HEADER}"
    "-DCONTACT_SHADOW_SETTINGS_STORE_SOURCE=${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "-DCONTACT_SHADOW_SETTINGS_STORE_HEADER=${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "-DCONTACT_SHADOW_SHADER_SOURCE=${CONTACT_SHADOW_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE=${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_MASK_SHADER_SOURCE=${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_DISPATCH_SHADER_SOURCE=${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_RESOLVE_SHADER_SOURCE=${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
    "-DCONTACT_SHADOW_SHADER_GENERATOR=${CONTACT_SHADOW_SHADER_GENERATOR}"
    "-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
    -P "${CONTACT_SHADOW_VERIFY_SCRIPT}"
  COMMENT "Verifying stereo Contact Shadows ownership contract"
  VERBATIM)
set(COMPLEX_PARALLAX_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ComplexParallax")
set(COMPLEX_PARALLAX_BASE_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeBase.dxbc")
set(COMPLEX_PARALLAX_BASE_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeBase.h")
set(COMPLEX_PARALLAX_LOD_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeLod.dxbc")
set(COMPLEX_PARALLAX_LOD_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeLod.h")
set(COMPLEX_PARALLAX_INSTANCED_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeInstancedLod.dxbc")
set(COMPLEX_PARALLAX_INSTANCED_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/ComplexParallaxLandscapeInstancedLod.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeBase.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeBase.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeLod.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeLod.h")
set(SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeInstancedLod.dxbc")
set(SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER
  "${COMPLEX_PARALLAX_GENERATED_DIR}/SurfaceClassComplexParallaxLandscapeInstancedLod.h")

add_custom_command(
  OUTPUT "${COMPLEX_PARALLAX_BASE_BINARY}" "${COMPLEX_PARALLAX_BASE_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    "/Fo${COMPLEX_PARALLAX_BASE_BINARY}"
    "/Fh${COMPLEX_PARALLAX_BASE_HEADER}"
    /Vncsah_complex_parallax_landscape_base
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling combined base landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT "${COMPLEX_PARALLAX_LOD_BINARY}" "${COMPLEX_PARALLAX_LOD_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    "/Fo${COMPLEX_PARALLAX_LOD_BINARY}"
    "/Fh${COMPLEX_PARALLAX_LOD_HEADER}"
    /Vncsah_complex_parallax_landscape_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling combined LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${COMPLEX_PARALLAX_INSTANCED_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_INSTANCED_LANDSCAPE=1
    "/Fo${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "/Fh${COMPLEX_PARALLAX_INSTANCED_HEADER}"
    /Vncsah_complex_parallax_landscape_instanced_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling combined instanced LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
    /Vncsah_surface_class_complex_parallax_landscape_base
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling classified base landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
    /Vncsah_surface_class_complex_parallax_landscape_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling classified LOD landscape complex parallax"
  VERBATIM)

add_custom_command(
  OUTPUT
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${COMPLEX_PARALLAX_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}"
    /nologo /T ps_5_0 /E PSMain /O3 /Ges /WX
    /DLINEAR_LIGHTING_COMPLEX_PARALLAX=1
    /DLINEAR_LIGHTING_LAND_LOD_BLEND=1
    /DLINEAR_LIGHTING_INSTANCED_LANDSCAPE=1
    /DLINEAR_LIGHTING_SURFACE_CLASSIFICATION=1
    "/Fo${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "/Fh${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}"
    /Vncsah_surface_class_complex_parallax_landscape_instanced_lod
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
  DEPENDS
    "${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/LODBlending/LODBlending.hlsli"
    "${ROOT_DIR}/package/Shaders/Community/TerrainVariation/TerrainVariation.hlsli"
  COMMENT "Compiling classified instanced LOD landscape complex parallax"
  VERBATIM)

add_custom_target(ComplexParallaxShaders
  DEPENDS
    "${COMPLEX_PARALLAX_BASE_BINARY}"
    "${COMPLEX_PARALLAX_BASE_HEADER}"
    "${COMPLEX_PARALLAX_LOD_BINARY}"
    "${COMPLEX_PARALLAX_LOD_HEADER}"
    "${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${COMPLEX_PARALLAX_INSTANCED_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_BASE_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_LOD_HEADER}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_BINARY}"
    "${SURFACE_CLASS_COMPLEX_PARALLAX_INSTANCED_HEADER}")

add_custom_target(VerifyComplexParallax ALL
  COMMAND "${CMAKE_COMMAND}"
    "-DFXC_EXECUTABLE=${CSAH_FXC_EXECUTABLE}"
    "-DHLSL_SOURCE=${COMPLEX_PARALLAX_HLSL_SOURCE}"
    "-DLINEAR_LIGHTING_INCLUDE=${ROOT_DIR}/package/Shaders/Community/LinearLighting/LinearLighting.hlsli"
    "-DRUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}"
    "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
    "-DBASE_BINARY=${COMPLEX_PARALLAX_BASE_BINARY}"
    "-DLOD_BINARY=${COMPLEX_PARALLAX_LOD_BINARY}"
    "-DINSTANCED_BINARY=${COMPLEX_PARALLAX_INSTANCED_BINARY}"
    -P "${COMPLEX_PARALLAX_VERIFY_SCRIPT}"
  DEPENDS ComplexParallaxShaders
  COMMENT "Verifying combined stereo landscape complex parallax"
  VERBATIM)

add_library(CSAHCore STATIC
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettings.h"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
)
target_compile_features(CSAHCore PUBLIC cxx_std_23)
target_include_directories(CSAHCore PUBLIC "${SOURCE_DIR}")
target_compile_options(CSAHCore PRIVATE
  /sdl
  /utf-8
  /permissive-
  /Zc:__cplusplus
  /Zc:preprocessor
  /W4
)

set(WATER_EFFECTS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/WaterEffects")
set(WATER_CAUSTICS_HEADER "${WATER_EFFECTS_GENERATED_DIR}/WaterCausticsTexture.h")
add_custom_command(OUTPUT "${WATER_CAUSTICS_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${ROOT_DIR}/tools/generate_water_caustics.py"
    --source "${ROOT_DIR}/package/Shaders/Community/WaterEffects/watercaustics.dds"
    --output "${WATER_CAUSTICS_HEADER}"
  DEPENDS "${ROOT_DIR}/tools/generate_water_caustics.py" "${ROOT_DIR}/package/Shaders/Community/WaterEffects/watercaustics.dds"
  VERBATIM)
add_custom_target(WaterEffectsAssets DEPENDS "${WATER_CAUSTICS_HEADER}")

set(INVERSE_SQUARE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/InverseSquare")
set(INVERSE_SQUARE_HEADER "${INVERSE_SQUARE_GENERATED_DIR}/InverseSquareShaders.h")
add_custom_command(OUTPUT "${INVERSE_SQUARE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${ROOT_DIR}/tools/generate_inverse_square_shaders.py"
    --root "${ROOT_DIR}" --fxc "${CSAH_FXC_EXECUTABLE}" --output "${INVERSE_SQUARE_HEADER}"
  DEPENDS "${ROOT_DIR}/tools/generate_inverse_square_shaders.py" "${ROOT_DIR}/tools/dxbc_transform.py"
    "${ROOT_DIR}/tools/water_parallax_transform.py" "${ROOT_DIR}/Shaders012_VR.fxp"
    "${ROOT_DIR}/package/Shaders/Community/InverseSquareLighting/Attenuation.hlsl"
    "${ROOT_DIR}/package/Shaders/Community/LocalLighting/LocalMaterial.hlsl"
    "${ROOT_DIR}/package/Shaders/Community/PBR/PbrCommon.hlsli"
    "${ROOT_DIR}/tools/local_light_material_transform.py"
    "${ROOT_DIR}/tools/generate_contact_shadow_shader.py" VERBATIM)
add_custom_target(InverseSquareShaders DEPENDS "${INVERSE_SQUARE_HEADER}")
set(TERRAIN_SHADOW_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/TerrainShadows")
set(TERRAIN_SHADOW_HEADER "${TERRAIN_SHADOW_GENERATED_DIR}/TerrainShadowUpdate.h")
add_custom_command(OUTPUT "${TERRAIN_SHADOW_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${TERRAIN_SHADOW_GENERATED_DIR}"
  COMMAND "${CSAH_FXC_EXECUTABLE}" /nologo /T cs_5_0 /E main /O3 /Ges /WX
    /Vn kTerrainShadowUpdate /Fh "${TERRAIN_SHADOW_HEADER}"
    "${ROOT_DIR}/package/Shaders/Community/TerrainShadows/ShadowUpdateCS.hlsl"
  DEPENDS "${ROOT_DIR}/package/Shaders/Community/TerrainShadows/ShadowUpdateCS.hlsl" VERBATIM)
add_custom_target(TerrainShadowShaders DEPENDS "${TERRAIN_SHADOW_HEADER}")
set(SSGI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/ScreenSpaceGI")
set(SSGI_GENERATED_HEADER "${SSGI_GENERATED_DIR}/ScreenSpaceGIShaders.h")
file(GLOB SSGI_SHADER_SOURCES CONFIGURE_DEPENDS "${ROOT_DIR}/package/Shaders/Community/ScreenSpaceGI/*.hlsl" "${ROOT_DIR}/package/Shaders/Community/ScreenSpaceGI/*.hlsli")
add_custom_command(OUTPUT "${SSGI_GENERATED_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${ROOT_DIR}/tools/generate_screen_space_gi_shaders.py"
    --root "${ROOT_DIR}" --fxc "${CSAH_FXC_EXECUTABLE}" --output "${SSGI_GENERATED_HEADER}"
  DEPENDS "${ROOT_DIR}/tools/generate_screen_space_gi_shaders.py" ${SSGI_SHADER_SOURCES}
    "${ROOT_DIR}/package/Shaders/Community/Common/Fo4VrDepth.hlsli"
  VERBATIM)
add_custom_target(ScreenSpaceGIShaders DEPENDS "${SSGI_GENERATED_HEADER}")

add_library(${PROJECT_NAME} SHARED
  "${SOURCE_DIR}/diagnostics/stereo_measurement/StereoMeasurement.cpp"
  "${SOURCE_DIR}/Features/water_effects/WaterEffectsRuntime.cpp"
  "${SOURCE_DIR}/Features/terrain_shadows/TerrainShadowRuntime.cpp"
  "${SOURCE_DIR}/Features/inverse_square/InverseSquareRuntime.cpp"
  "${SOURCE_DIR}/Features/inverse_square/InverseSquareSettings.cpp"
  "${SOURCE_DIR}/Features/terrain_shadows/TerrainShadowSettings.cpp"
  "${SOURCE_DIR}/Features/water_effects/WaterWorldData.cpp"
  "${SOURCE_DIR}/Features/water_effects/WaterWorldData.h"
  "${SOURCE_DIR}/Features/water_effects/WaterEffectsRuntime.h"
  "${SOURCE_DIR}/Features/water_effects/WaterEffectsSettings.cpp"
  "${SOURCE_DIR}/Features/water_effects/WaterEffectsSettings.h"
  "${SOURCE_DIR}/Features/screen_space_gi/ScreenSpaceGIRuntime.cpp"
  "${SOURCE_DIR}/Features/screen_space_gi/ScreenSpaceGIRuntime.h"
  "${SOURCE_DIR}/Features/screen_space_gi/ScreenSpaceGISettings.cpp"
  "${SOURCE_DIR}/Features/screen_space_gi/ScreenSpaceGISettings.h"
  "${SOURCE_DIR}/Features/terrain_variation/TerrainVariationRuntime.cpp"
  "${SOURCE_DIR}/Features/terrain_variation/TerrainVariationRuntime.h"
  "${SOURCE_DIR}/Features/terrain_variation/TerrainVariationSettings.cpp"
  "${SOURCE_DIR}/Features/terrain_variation/TerrainVariationSettings.h"
  "${SOURCE_DIR}/Features/lod_blending/LodBlendingRuntime.cpp"
  "${SOURCE_DIR}/Features/lod_blending/LodBlendingRuntime.h"
  "${SOURCE_DIR}/Features/lod_blending/LodBlendingSettings.h"
  "${SOURCE_DIR}/Features/lod_blending/LodClassification.h"
  "${SOURCE_DIR}/Features/lod_blending/LodBlendingSettingsStore.cpp"
  "${SOURCE_DIR}/Features/lod_blending/LodBlendingSettingsStore.h"
  "${SOURCE_DIR}/PCH.h"
  "${SOURCE_DIR}/XSEPlugin.cpp"
  "${SOURCE_DIR}/exports.def"
  "${SOURCE_DIR}/resources.h"
  "${SOURCE_DIR}/resources.rc"
  "${QUALIFICATION_SOURCE}"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualification.h"
  "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingContractMask.h"
  "${IBL_RUNTIME_SOURCE}"
  "${IBL_RUNTIME_HEADER}"
  "${IBL_SETTINGS_HEADER}"
  "${IBL_SETTINGS_STORE_SOURCE}"
  "${IBL_SETTINGS_STORE_HEADER}"
  "${IBL_PROVIDER_MODEL}"
  "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
  "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
  "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
  "${IBL_ENVIRONMENT_UPDATER_HEADER}"
  "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
  "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
  "${GPU_TIMING_PROFILER_SOURCE}"
  "${GPU_TIMING_PROFILER_HEADER}"
  "${SOURCE_DIR}/diagnostics/RenderDocCapture.cpp"
  "${SOURCE_DIR}/diagnostics/RenderDocCapture.h"
  "${IBL_PROJECTION_MODEL}"
  "${IBL_CAPTURE_PROBE_MODEL}"
  "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
  "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
  "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
  "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
  "${IBL_MATERIAL_BINDING_SCOPE_HEADER}"
  "${SOURCE_DIR}/Features/ibl/IblMaterialStyle.cpp"
  "${SOURCE_DIR}/Features/ibl/IblMaterialStyle.h"
  "${IBL_MATERIAL_CONTRACT_GENERATOR}"
  "${IBL_MATERIAL_DXBC_TRANSFORM}"
  "${IBL_MATERIAL_GENERATED_CONTRACTS}"
  "${PBR_RUNTIME_SOURCE}"
  "${PBR_RUNTIME_HEADER}"
  "${PBR_SETTINGS_HEADER}"
  "${PBR_SETTINGS_STORE_SOURCE}"
  "${PBR_SETTINGS_STORE_HEADER}"
  "${PBR_MATERIAL_MODEL_HEADER}"
  "${PBR_COMMON_SHADER_SOURCE}"
  "${PBR_DIRECTIONAL_SHADER_SOURCE}"
  "${PBR_MATERIAL_OUTPUT_SHADER_SOURCE}"
  "${SKYLIGHTING_RUNTIME_SOURCE}"
  "${SKYLIGHTING_RUNTIME_HEADER}"
  "${SKYLIGHTING_NATIVE_HOOK_SOURCE}"
  "${SKYLIGHTING_NATIVE_HOOK_HEADER}"
  "${SKYLIGHTING_SETTINGS_HEADER}"
  "${SKYLIGHTING_SETTINGS_STORE_SOURCE}"
  "${SKYLIGHTING_SETTINGS_STORE_HEADER}"
  "${SKYLIGHTING_SHADER_GENERATOR}"
  "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}"
  "${SKYLIGHTING_SAMPLING_SHADER_SOURCE}"
  "${SKYLIGHTING_RECEIVER_SHADER_SOURCE}"
  "${SKYLIGHTING_SUN_SHADER_SOURCE}"
  "${SKYLIGHTING_DIRECTIONAL_SHADER_SOURCE}"
  "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}"
  "${SKYLIGHTING_AMBIENT_HEADER}"
  "${SKYLIGHTING_COMPUTE_HEADER}"
  "${SKY_SYNC_RUNTIME_SOURCE}"
  "${SKY_SYNC_RUNTIME_HEADER}"
  "${SKY_SYNC_SETTINGS_HEADER}"
  "${SKY_SYNC_SETTINGS_STORE_SOURCE}"
  "${SKY_SYNC_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_RUNTIME_SOURCE}"
  "${CONTACT_SHADOW_RUNTIME_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_HEADER}"
  "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_SHADER_GENERATOR}"
  "${CONTACT_SHADOW_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
  "${CONTACT_SHADOW_WRAPPED_SHADER_SOURCE}"
  "${CONTACT_SHADOW_SHADER_HEADER}"
  "${CONTACT_SHADOW_MASK_SHADER_SOURCE}"
  "${CONTACT_SHADOW_MASK_HEADER}"
  "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}"
  "${CONTACT_SHADOW_DISPATCH_HEADER}"
  "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}"
  "${CONTACT_SHADOW_RESOLVE_HEADER}"
  "${HDR_OUTPUT_PROBE_SOURCE}"
  "${HDR_OUTPUT_PROBE_HEADER}"
  "${FILMIC_TONEMAPPING_RUNTIME_SOURCE}"
  "${FILMIC_TONEMAPPING_RUNTIME_HEADER}"
  "${FILMIC_TONEMAPPING_SETTINGS_HEADER}"
  "${FILMIC_TONEMAPPING_SETTINGS_STORE_SOURCE}"
  "${FILMIC_TONEMAPPING_SETTINGS_STORE_HEADER}"
  "${FILMIC_TONEMAPPING_SHADER_GENERATOR}"
  "${FILMIC_TONEMAPPING_SHADER_SOURCE}"
  "${FILMIC_TONEMAPPING_SHADER_HEADER}"
  "${BLOOM_GLARE_RUNTIME_SOURCE}"
  "${BLOOM_GLARE_RUNTIME_HEADER}"
  "${BLOOM_GLARE_SETTINGS_HEADER}"
  "${BLOOM_GLARE_SETTINGS_STORE_SOURCE}"
  "${BLOOM_GLARE_SETTINGS_STORE_HEADER}"
  "${BLOOM_GLARE_SHADER_GENERATOR}"
  ${BLOOM_GLARE_SHADER_SOURCES}
  "${BLOOM_GLARE_SHADER_HEADER}"
  "${SURFACE_CLASSIFICATION_RUNTIME_SOURCE}"
  "${SURFACE_CLASSIFICATION_RUNTIME_HEADER}"
  "${SURFACE_CLASSIFICATION_GENERATOR}"
  "${SURFACE_CLASSIFICATION_GENERATED_CONTRACTS}"
  "${SOURCE_DIR}/GeneratedSurfaceClassificationResources.h"
  "${WRAPPED_GRASS_RUNTIME_SOURCE}"
  "${WRAPPED_GRASS_RUNTIME_HEADER}"
  "${WRAPPED_GRASS_SETTINGS_HEADER}"
  "${WRAPPED_GRASS_SETTINGS_STORE_SOURCE}"
  "${WRAPPED_GRASS_SETTINGS_STORE_HEADER}"
  "${HAIR_SPECULAR_RUNTIME_SOURCE}"
  "${HAIR_SPECULAR_RUNTIME_HEADER}"
  "${HAIR_SPECULAR_SETTINGS_HEADER}"
  "${HAIR_SPECULAR_SETTINGS_STORE_SOURCE}"
  "${HAIR_SPECULAR_SETTINGS_STORE_HEADER}"
  "${BASIC_WETNESS_RUNTIME_SOURCE}"
  "${BASIC_WETNESS_RUNTIME_HEADER}"
  "${BASIC_WETNESS_SETTINGS_HEADER}"
  "${BASIC_WETNESS_SETTINGS_STORE_SOURCE}"
  "${BASIC_WETNESS_SETTINGS_STORE_HEADER}"
  "${CONTACT_SHADOW_WETNESS_SHADER_SOURCE}"
  "${SOURCE_DIR}/Features/cloud_shadows/CloudGeometryCapture.cpp"
  "${CLOUD_SHADOW_RUNTIME_SOURCE}"
  "${CLOUD_SHADOW_RUNTIME_HEADER}"
  "${CLOUD_SHADOW_SETTINGS_HEADER}"
  "${CLOUD_SHADOW_SETTINGS_STORE_SOURCE}"
  "${CLOUD_SHADOW_SETTINGS_STORE_HEADER}"
  "${VOLUMETRIC_LIGHTING_RUNTIME_SOURCE}"
  "${VOLUMETRIC_LIGHTING_RUNTIME_HEADER}"
  "${VOLUMETRIC_LIGHTING_SETTINGS_HEADER}"
  "${VOLUMETRIC_LIGHTING_SETTINGS_STORE_SOURCE}"
  "${VOLUMETRIC_LIGHTING_SETTINGS_STORE_HEADER}"
  "${VOLUMETRIC_LIGHTING_ENGINE_DATA_SOURCE}"
  "${VOLUMETRIC_LIGHTING_ENGINE_DATA_HEADER}"
  "${VOLUMETRIC_LIGHTING_STATE_SCOPE_SOURCE}"
  "${VOLUMETRIC_LIGHTING_STATE_SCOPE_HEADER}"
  "${VOLUMETRIC_LIGHTING_OUTPUT_QUALIFICATION_HEADER}"
  "${VOLUMETRIC_COMMON_SHADER_SOURCE}"
  "${VOLUMETRIC_HOST_SHADER_SOURCE}"
  "${VOLUMETRIC_FULLSCREEN_SHADER_SOURCE}"
  "${VOLUMETRIC_GENERATE_SHADER_SOURCE}"
  "${VOLUMETRIC_INTEGRATE_SHADER_SOURCE}"
  "${VOLUMETRIC_QUALIFY_SHADER_SOURCE}"
  "${VOLUMETRIC_RESOLVE_SHADER_SOURCE}"
  "${VOLUMETRIC_TEMPORAL_SHADER_SOURCE}"
  "${VOLUMETRIC_BLUR_SHADER_SOURCE}"
  "${VOLUMETRIC_COMPOSITE_SHADER_SOURCE}"
  "${VOLUMETRIC_HOST_SHADER_HEADER}"
  "${VOLUMETRIC_FULLSCREEN_SHADER_HEADER}"
  "${VOLUMETRIC_GENERATE_SHADER_HEADER}"
  "${VOLUMETRIC_INTEGRATE_SHADER_HEADER}"
  "${VOLUMETRIC_QUALIFY_SHADER_HEADER}"
  "${VOLUMETRIC_RESOLVE_SHADER_HEADER}"
  "${VOLUMETRIC_TEMPORAL_SHADER_HEADER}"
  "${VOLUMETRIC_BLUR_H_SHADER_HEADER}"
  "${VOLUMETRIC_BLUR_V_SHADER_HEADER}"
  "${VOLUMETRIC_COMPOSITE_SHADER_HEADER}"
  "${SUBSURFACE_SCATTERING_RUNTIME_SOURCE}"
  "${SUBSURFACE_SCATTERING_RUNTIME_HEADER}"
  "${SUBSURFACE_SCATTERING_SETTINGS_HEADER}"
  "${SUBSURFACE_SCATTERING_SETTINGS_STORE_SOURCE}"
  "${SUBSURFACE_SCATTERING_SETTINGS_STORE_HEADER}"
  "${SUBSURFACE_SCATTERING_SHADER_SOURCE}"
  "${SUBSURFACE_SCATTERING_SHADER_HEADER}"
  "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
  "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
  "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_MODEL}"
  "${COMPLEX_ENVIRONMENT_GENERATOR}"
  "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
  "${COMPLEX_ENVIRONMENT_HLSL}"
  "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
  "${COMPLEX_PARALLAX_MODEL_HEADER}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_SOURCE}"
  "${COMPLEX_PARALLAX_SETTINGS_STORE_HEADER}"
  "${IBL_MATERIAL_SHADER_SOURCE}"
  ${IBL_MATERIAL_SHADER_ASSETS}
  ${IBL_MATERIAL_VERIFIED_ASSETS}
  "${POINT_LIGHT_HOOK_SOURCE}"
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightHook.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightModel.h"
  "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h"
  "${DFLIGHT_AMBIENT_PATCH_HEADER}"
  "${SOURCE_DIR}/Features/linear_lighting/DxbcChecksum.h"
  "${LINEAR_LIGHTING_RUNTIME_SOURCE}"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingRuntime.h"
  "${FIXED_SHADER_BINDING_LOOKUP_HEADER}"
  "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
  "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
  "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.cpp"
  "${SOURCE_DIR}/Features/linear_lighting/LinearLightingSettingsStore.h"
  "${SETTINGS_PATH_SOURCE}"
  "${SETTINGS_PATH_HEADER}"
  "${SHARED_SETTINGS_RUNTIME_SOURCE}"
  "${SHARED_SETTINGS_RUNTIME_HEADER}"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.cpp"
  "${SOURCE_DIR}/render/BSLightingGeometryHook.h"
  "${DFPREPASS_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/BSDFPrePassShaderHook.h"
  "${SOURCE_DIR}/render/NativeShadowProperty.cpp"
  "${SOURCE_DIR}/render/NativeShadowProperty.h"
  "${SOURCE_DIR}/render/NativeEmissionRead.cpp"
  "${SOURCE_DIR}/render/NativeEmissionRead.h"
  "${D3D11_HOOK_SOURCE}"
  "${SOURCE_DIR}/render/D3D11Hooks.h"
  "${DLAA_RUNTIME_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaRuntime.h"
  "${DLAA_SOURCE_DIR}/DlaaSettings.h"
  "${DLAA_SETTINGS_STORE_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaSettingsStore.h"
  "${SOURCE_DIR}/render/Fo4VrCamera.h"
  "${DLAA_ENGINE_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaEngineHooks.h"
  "${DLAA_D3D11_HOOK_SOURCE}"
  "${DLAA_SOURCE_DIR}/DlaaD3D11Hooks.h"
  "${DLAA_STREAMLINE_SOURCE}"
  "${DLAA_SOURCE_DIR}/StreamlineBackend.h"
  "${UPSCALING_COMPOSE_SHADER_SOURCE}"
  "${UPSCALING_COMPOSE_SHADER_HEADER}"
  "${UPSCALING_MOTION_SHADER_SOURCE}"
  "${UPSCALING_MOTION_SHADER_HEADER}"
  "${UPSCALING_REACTIVE_SHADER_SOURCE}"
  "${UPSCALING_REACTIVE_SHADER_HEADER}"
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/FocusShadowRuntime.h"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_HEADER}"
  "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
  "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/ReflectionCompositePatch.h"
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/SslrEnvironmentBinding.h"
  "${VANILLA_FIXES_SOURCE_DIR}/SunOcclusionRuntime.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/SunOcclusionRuntime.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesRuntime.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettings.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.h"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.cpp"
  "${VANILLA_FIXES_SOURCE_DIR}/VanillaShaderFixes.h"
  "${NATIVE_SHADOW_RUNTIME_SOURCE}"
  "${NATIVE_SHADOW_RUNTIME_HEADER}"
  "${NATIVE_SHADOW_SETTINGS_HEADER}"
  "${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
  "${NATIVE_SHADOW_SETTINGS_STORE_HEADER}"
  "${NATIVE_SHADOW_PATCH_MODEL}"
  "${VANILLA_SAO_BLUR_SOURCE}"
  "${VANILLA_SAO_RAW_SOURCE}"
  "${VANILLA_SSLR_BLUR_SOURCE}"
  "${VANILLA_SSLR_PREPASS_SOURCE}"
  "${VANILLA_SSLR_RAYTRACE_SOURCE}"
  "${VANILLA_SAO_BLUR_HEADER}"
  "${VANILLA_SAO_RAW_HEADER}"
  "${VANILLA_SSLR_BLUR_HEADER}"
  "${VANILLA_SSLR_PREPASS_HEADER}"
  "${VANILLA_SSLR_RAYTRACE_HEADER}"
  "${SOURCE_DIR}/support/Logger.h"
  "${SOURCE_DIR}/support/NearAllocation.h"
)
add_dependencies(${PROJECT_NAME}
  WaterEffectsAssets
  ScreenSpaceGIShaders
  TerrainShadowShaders
  InverseSquareShaders
  VanillaFixesShaders
  UpscalingShaders
  VanillaReflectionPatchTool
  ContactShadowShader
  VolumetricLightingShaders
  HeightFogShader
  SkylightingShaders
  FilmicTonemappingShader
  BloomGlareShaders
  SubsurfaceScatteringShader
  ComplexParallaxShaders
  VerifyComplexParallax
  VerifyFo4VrLoader
  VerifyFo4VrRendererHook
  VerifyBSDFPrePassHook
  VerifyAuthoredPbrRuntime
  VerifyD3D11Hooks
  VerifyDlaa
  VerifyStreamlinePayload
  VerifyVanillaFixes
  VerifyNativeShadows
  VerifyIblFoundation
  VerifyIblCaptureProbeContracts
  VerifyIblMaterialContracts
  VerifyComplexMaterialProducerContracts
  VerifyComplexEnvironmentContracts
  VerifyContactShadows
  VerifySkylighting
  VerifyFilmicTonemapping
  VerifyBloomGlare
  VerifySurfaceClassification
  VerifyIblProjectionShaderAsset
  VerifyDistantTreeLinearLighting
  VerifyDistantTreeLinearLightingContracts
  VerifyParticleLinearLighting
  VerifyParticleLinearLightingContracts
  VerifyWaterLinearLighting
  VerifyWaterLinearLightingContracts
  VerifyVLSCompositeLinearLighting
  VerifyVLSCompositeLinearLightingContracts
  VerifyEffectLinearLighting
  VerifyEffectLinearLightingContracts
  VerifyDFLightAmbientReplacement
  VerifyDFLightAmbientContracts
  VerifyDFTiledPointLightHook
  VerifyFixedShaderBindingLookup
  VerifyLinearLightingConstantScope
  VerifyLinearLightingContracts
  VerifyLinearLightingQualification
  VerifySkyLinearLighting
  VerifySkyLinearLightingContracts
  VerifyPALMSettings
  StereoMeasurementShader)
target_include_directories(${PROJECT_NAME} PRIVATE "${STEREO_MEASUREMENT_DIR}")

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${DLAA_GENERATED_DIR}"
  "${COMPLEX_PARALLAX_GENERATED_DIR}"
  "${CONTACT_SHADOW_GENERATED_DIR}"
  "${VOLUMETRIC_GENERATED_DIR}"
  "${SKYLIGHTING_GENERATED_DIR}"
  "${SSGI_GENERATED_DIR}"
  "${TERRAIN_SHADOW_GENERATED_DIR}"
  "${INVERSE_SQUARE_GENERATED_DIR}"
  "${WATER_EFFECTS_GENERATED_DIR}"
  "${FILMIC_TONEMAPPING_GENERATED_DIR}"
  "${BLOOM_GLARE_GENERATED_DIR}"
  "${SUBSURFACE_SCATTERING_GENERATED_DIR}"
  "${VANILLA_FIXES_GENERATED_DIR}")
target_compile_definitions(${PROJECT_NAME} PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  NOMMNOSOUND
)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${SOURCE_DIR}"
  "${STREAMLINE_INCLUDE_DIR}"
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
  CSAHCore
  CommonLibF4::CommonLibF4
  minhook::minhook
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  dxguid
  shell32
)

# Keep distribution notices inside the DLL so the install archive only
# exposes game folders. The original notice text is preserved verbatim.
set(CSAH_NOTICES_DIR "${BUILD_DIR}/generated/notices")
file(MAKE_DIRECTORY "${CSAH_NOTICES_DIR}")
set(CSAH_NOTICE_FILES "${ROOT_DIR}/LICENSE" "${ROOT_DIR}/extern/RenderDoc/LICENSE"
  "${COMMON_LIB_F4VR_PATH_SELECTED}/LICENSE" ${STREAMLINE_LICENSE_FILES})
foreach(package IN ITEMS fmt minhook nlohmann-json rapidcsv rsm-mmio spdlog xbyak)
  list(APPEND CSAH_NOTICE_FILES
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${package}/copyright")
endforeach()
set(csahNotices "Community Shaders at Home (CSAH) VR - distribution notices\n")
foreach(notice IN LISTS CSAH_NOTICE_FILES)
  file(READ "${notice}" noticeText)
  string(APPEND csahNotices "\n${noticeText}\n")
endforeach()
file(WRITE "${CSAH_NOTICES_DIR}/notices.txt" "${csahNotices}")
file(WRITE "${CSAH_NOTICES_DIR}/notices.rc"
  "65000 RCDATA \"${CSAH_NOTICES_DIR}/notices.txt\"\n")
target_sources(${PROJECT_NAME} PRIVATE "${CSAH_NOTICES_DIR}/notices.rc")

target_precompile_headers(${PROJECT_NAME} PRIVATE "${SOURCE_DIR}/PCH.h")
target_compile_options(${PROJECT_NAME} PRIVATE
  /sdl
  /utf-8
  /Zi
  /permissive-
  /Zc:__cplusplus
  /Zc:preprocessor
  /W4
  /wd4715
  "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
)
target_link_options(${PROJECT_NAME} PRIVATE
  "/DEF:${SOURCE_DIR}/exports.def"
  "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
  "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE
  "${CMAKE_CXX_FLAGS_RELEASE}")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E rm -f
    "${ROOT_DIR}/package/F4SE/Plugins/fo4vr-community-shaders.dll"
    "${ROOT_DIR}/package/F4SE/Plugins/fo4vr-community-shaders.pdb"
  COMMAND "${CMAKE_COMMAND}" -E remove_directory
    "${ROOT_DIR}/package/DevMenu/Mods/fo4vr-community-shaders"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/Licenses"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy
    "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
    "${ROOT_DIR}/package/F4SE/Plugins/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${STREAMLINE_RUNTIME_FILES}
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${STREAMLINE_LICENSE_FILES}
    "${ROOT_DIR}/package/F4SE/Plugins/Streamline/Licenses/"
  COMMAND "${CMAKE_COMMAND}" -E remove_directory
    "${ROOT_DIR}/package/PrismaUI_F4/views/FO4VR-Community-Shaders"
  COMMAND "${CMAKE_COMMAND}" -E rm -f
    "${ROOT_DIR}/package/DevMenu/Mods/CSAH/menu.json"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${ROOT_DIR}/package/F4SE/Plugins/CSAH"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${PALM_SETTINGS_SOURCE}"
    "${ROOT_DIR}/package/F4SE/Plugins/CSAH/PALMSettings.json"
  COMMENT "Staging ${PROJECT_NAME} in the local package"
)

option(POST_BUILD_COPY_PLUGIN
  "Copy CSAH to a mod root." OFF)
set(COPY_PLUGIN_BASE_PATH "" CACHE STRING
  "Semicolon-separated CSAH mod roots.")
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(FATAL_ERROR
      "COPY_PLUGIN_BASE_PATH is required when POST_BUILD_COPY_PLUGIN is enabled")
  endif()
  foreach(copy_path IN LISTS COPY_PLUGIN_BASE_PATH)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E rm -f
        "${copy_path}/F4SE/Plugins/fo4vr-community-shaders.dll"
        "${copy_path}/F4SE/Plugins/fo4vr-community-shaders.pdb"
      COMMAND "${CMAKE_COMMAND}" -E remove_directory
        "${copy_path}/DevMenu/Mods/fo4vr-community-shaders"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins/Streamline"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins/Streamline/Licenses"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
        "${copy_path}/F4SE/Plugins/"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${STREAMLINE_RUNTIME_FILES}
        "${copy_path}/F4SE/Plugins/Streamline/"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${STREAMLINE_LICENSE_FILES}
        "${copy_path}/F4SE/Plugins/Streamline/Licenses/"
      COMMAND "${CMAKE_COMMAND}" -E remove_directory
        "${copy_path}/PrismaUI_F4/views/FO4VR-Community-Shaders"
      COMMAND "${CMAKE_COMMAND}" -E rm -f
        "${copy_path}/DevMenu/Mods/CSAH/menu.json"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${copy_path}/F4SE/Plugins/CSAH"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PALM_SETTINGS_SOURCE}"
        "${copy_path}/F4SE/Plugins/CSAH/PALMSettings.json"
      COMMENT "Deploying ${PROJECT_NAME} to '${copy_path}'"
    )
  endforeach()
endif()

option(BUILD_CSAH_TESTS
  "Build focused Community Shaders at Home (CSAH) VR tests." OFF)
if(BUILD_CSAH_TESTS)
  enable_testing()
  add_executable(CSAHNativeShadowPropertyTests tests/NativeShadowPropertyTests.cpp)
  target_compile_features(CSAHNativeShadowPropertyTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHNativeShadowPropertyTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHNativeShadowPropertyTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME CSAH.NativeShadowProperty COMMAND CSAHNativeShadowPropertyTests)
  add_executable(CSAHNativeEmissionReadTests tests/NativeEmissionReadTests.cpp)
  target_compile_features(CSAHNativeEmissionReadTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHNativeEmissionReadTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHNativeEmissionReadTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME CSAH.NativeEmissionRead COMMAND CSAHNativeEmissionReadTests)
  find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)

  add_executable(CSAHUpscalingSettingsTests
    "${ROOT_DIR}/tests/UpscalingSettingsTests.cpp"
    "${DLAA_SETTINGS_STORE_SOURCE}"
    "${DLAA_SOURCE_DIR}/DlaaSettingsStore.h"
    "${DLAA_SOURCE_DIR}/DlaaSettings.h"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHUpscalingSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHUpscalingSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHUpscalingSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHUpscalingSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHUpscalingSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHUpscalingSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.UpscalingSettings
    COMMAND CSAHUpscalingSettingsTests)

  add_executable(CSAHLinearLightingSettingsTests
    "${ROOT_DIR}/tests/LinearLightingSettingsTests.cpp")
  target_compile_features(
    CSAHLinearLightingSettingsTests PRIVATE cxx_std_23)
  target_compile_options(
    CSAHLinearLightingSettingsTests PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
  )
  target_link_libraries(
    CSAHLinearLightingSettingsTests PRIVATE
    CSAHCore)
  add_test(NAME CSAH.LinearLightingSettings
    COMMAND CSAHLinearLightingSettingsTests)

  add_executable(CSAHSettingsPathTests
    "${ROOT_DIR}/tests/SettingsPathTests.cpp"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHSettingsPathTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHSettingsPathTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHSettingsPathTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHSettingsPathTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHSettingsPathTests PRIVATE shell32)
  add_test(NAME CSAH.SettingsPath
    COMMAND CSAHSettingsPathTests)

  add_executable(CSAHSharedSettingsRuntimeTests
    "${ROOT_DIR}/tests/SharedSettingsRuntimeTests.cpp"
    "${SHARED_SETTINGS_RUNTIME_HEADER}")
  target_compile_features(
    CSAHSharedSettingsRuntimeTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHSharedSettingsRuntimeTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHSharedSettingsRuntimeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.SharedSettingsRuntime
    COMMAND CSAHSharedSettingsRuntimeTests "${CMAKE_BINARY_DIR}/test-output/settings")

  add_executable(CSAHFixedShaderBindingLookupTests
    "${ROOT_DIR}/tests/FixedShaderBindingLookupTests.cpp")
  target_compile_features(
    CSAHFixedShaderBindingLookupTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHFixedShaderBindingLookupTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHFixedShaderBindingLookupTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.FixedShaderBindingLookup
    COMMAND CSAHFixedShaderBindingLookupTests)

  add_executable(CSAHComplexMaterialProducerModelTests
    "${ROOT_DIR}/tests/ComplexMaterialProducerModelTests.cpp"
    "${COMPLEX_MATERIAL_PRODUCER_MODEL}"
    "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
    "${COMPLEX_ENVIRONMENT_MODEL}"
    "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}")
  target_compile_features(
    CSAHComplexMaterialProducerModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHComplexMaterialProducerModelTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHComplexMaterialProducerModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.ComplexMaterialProducerModel
    COMMAND CSAHComplexMaterialProducerModelTests)

  add_test(NAME CSAH.ComplexMaterialProducerGenerator
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tests/test_generate_complex_material_producer_contracts.py")

  add_executable(CSAHComplexParallaxModelTests
    "${COMPLEX_PARALLAX_TEST_SOURCE}"
    "${COMPLEX_PARALLAX_SETTINGS_HEADER}"
    "${COMPLEX_PARALLAX_MODEL_HEADER}")
  target_compile_features(
    CSAHComplexParallaxModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHComplexParallaxModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHComplexParallaxModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.ComplexParallaxModel
    COMMAND CSAHComplexParallaxModelTests)

  add_test(NAME CSAH.LoaderSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DLOADER_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrLoader.cmake'")
  add_test(NAME CSAH.RendererHookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake'")
  add_test(NAME CSAH.RendererHookValidatorNegative
    COMMAND "${CMAKE_COMMAND}"
      "-DRENDERER_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}"
      "-DRENDERER_VALIDATOR=${ROOT_DIR}/cmake/VerifyFo4VrRendererHook.cmake"
      "-DTEST_OUTPUT_DIR=${BUILD_DIR}/renderer-validator-negative"
      -P "${ROOT_DIR}/cmake/TestVerifyFo4VrRendererHookNegative.cmake")
  add_test(NAME CSAH.PALMSettingsSource
    COMMAND "${CMAKE_COMMAND}"
      "-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
      "-DPROJECT_CMAKE_SOURCE=${ROOT_DIR}/CMakeLists.txt"
      "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
      "-DSHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
      "-DSHARED_SETTINGS_HEADER=${SHARED_SETTINGS_RUNTIME_HEADER}"
      "-DSOURCE_ROOT=${ROOT_DIR}"
      -P "${PALM_SETTINGS_VERIFY_SCRIPT}")

  add_test(NAME CSAH.D3D11HookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyD3D11Hooks.cmake'")

  add_test(NAME CSAH.VanillaFixesSource
    COMMAND "${CMAKE_COMMAND}"
      "-DVANILLA_SHADER_RUNTIME_SOURCE=${VANILLA_SHADER_RUNTIME_SOURCE}"
      "-DVANILLA_FOCUS_RUNTIME_SOURCE=${VANILLA_FOCUS_RUNTIME_SOURCE}"
      "-DVANILLA_SUN_OCCLUSION_RUNTIME_SOURCE=${VANILLA_SUN_OCCLUSION_RUNTIME_SOURCE}"
      "-DVANILLA_RUNTIME_SOURCE=${VANILLA_RUNTIME_SOURCE}"
      "-DVANILLA_SETTINGS_STORE_SOURCE=${VANILLA_SETTINGS_STORE_SOURCE}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
      "-DVANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY=${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY}"
      "-DVANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE=${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
      "-DVANILLA_REFLECTION_PATCH_SOURCE=${VANILLA_REFLECTION_PATCH_SOURCE}"
      "-DVANILLA_REFLECTION_PATCH_TOOL_SOURCE=${VANILLA_REFLECTION_PATCH_TOOL_SOURCE}"
      "-DVANILLA_SSLR_ENVIRONMENT_SOURCE=${VANILLA_SSLR_ENVIRONMENT_SOURCE}"
      "-DVANILLA_IBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}"
      "-DVANILLA_D3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}"
      "-DVANILLA_PALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}"
      "-DVANILLA_SHARED_SETTINGS_SOURCE=${SHARED_SETTINGS_RUNTIME_SOURCE}"
      "-DVANILLA_PLUGIN_SOURCE=${PLUGIN_SOURCE}"
      "-DVANILLA_IBL_GENERATOR_SOURCE=${VANILLA_IBL_GENERATOR_SOURCE}"
      "-DVANILLA_SAO_BLUR_SOURCE=${VANILLA_SAO_BLUR_SOURCE}"
      "-DVANILLA_SAO_RAW_SOURCE=${VANILLA_SAO_RAW_SOURCE}"
      "-DVANILLA_SSLR_BLUR_SOURCE=${VANILLA_SSLR_BLUR_SOURCE}"
      "-DVANILLA_SSLR_PREPASS_SOURCE=${VANILLA_SSLR_PREPASS_SOURCE}"
      "-DVANILLA_SSLR_RAYTRACE_SOURCE=${VANILLA_SSLR_RAYTRACE_SOURCE}"
      "-DVANILLA_FXC_EXECUTABLE=${CSAH_FXC_EXECUTABLE}"
      "-DVANILLA_SAO_BLUR_BINARY=${VANILLA_SAO_BLUR_BINARY}"
      "-DVANILLA_SAO_RAW_BINARY=${VANILLA_SAO_RAW_BINARY}"
      "-DVANILLA_SSLR_BLUR_BINARY=${VANILLA_SSLR_BLUR_BINARY}"
      "-DVANILLA_SSLR_PREPASS_BINARY=${VANILLA_SSLR_PREPASS_BINARY}"
      "-DVANILLA_SSLR_RAYTRACE_BINARY=${VANILLA_SSLR_RAYTRACE_BINARY}"
      -P "${VANILLA_VERIFY_SCRIPT}")

  add_test(NAME CSAH.ContactShadowsSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DCONTACT_SHADOW_RUNTIME_SOURCE=${CONTACT_SHADOW_RUNTIME_SOURCE}' '-DCONTACT_SHADOW_RUNTIME_HEADER=${CONTACT_SHADOW_RUNTIME_HEADER}' '-DCONTACT_SHADOW_SETTINGS_HEADER=${CONTACT_SHADOW_SETTINGS_HEADER}' '-DCONTACT_SHADOW_SETTINGS_STORE_SOURCE=${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}' '-DCONTACT_SHADOW_SETTINGS_STORE_HEADER=${CONTACT_SHADOW_SETTINGS_STORE_HEADER}' '-DCONTACT_SHADOW_SHADER_SOURCE=${CONTACT_SHADOW_SHADER_SOURCE}' '-DCONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE=${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}' '-DCONTACT_SHADOW_MASK_SHADER_SOURCE=${CONTACT_SHADOW_MASK_SHADER_SOURCE}' '-DCONTACT_SHADOW_DISPATCH_SHADER_SOURCE=${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}' '-DCONTACT_SHADOW_RESOLVE_SHADER_SOURCE=${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}' '-DCONTACT_SHADOW_SHADER_GENERATOR=${CONTACT_SHADOW_SHADER_GENERATOR}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}' '-DPLUGIN_SOURCE=${PLUGIN_SOURCE}' '-P' '${CONTACT_SHADOW_VERIFY_SCRIPT}'")

  add_executable(CSAHContactShadowSettingsTests
    "${ROOT_DIR}/tests/ContactShadowSettingsTests.cpp"
    "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHContactShadowSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHContactShadowSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHContactShadowSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHContactShadowSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHContactShadowSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHContactShadowSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.ContactShadowSettings
    COMMAND CSAHContactShadowSettingsTests)

  add_executable(CSAHVolumetricLightingSettingsTests
    "${ROOT_DIR}/tests/VolumetricLightingSettingsTests.cpp"
    "${VOLUMETRIC_LIGHTING_SETTINGS_STORE_SOURCE}"
    "${VOLUMETRIC_LIGHTING_SETTINGS_STORE_HEADER}"
    "${VOLUMETRIC_LIGHTING_SETTINGS_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHVolumetricLightingSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHVolumetricLightingSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHVolumetricLightingSettingsTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHVolumetricLightingSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHVolumetricLightingSettingsTests PRIVATE
    "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHVolumetricLightingSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.VolumetricLightingSettings
    COMMAND CSAHVolumetricLightingSettingsTests)

  add_executable(CSAHVolumetricOutputQualificationTests
    "${ROOT_DIR}/tests/VolumetricOutputQualificationTests.cpp"
    "${VOLUMETRIC_LIGHTING_OUTPUT_QUALIFICATION_HEADER}")
  target_compile_features(
    CSAHVolumetricOutputQualificationTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHVolumetricOutputQualificationTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHVolumetricOutputQualificationTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHVolumetricOutputQualificationTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.VolumetricOutputQualification
    COMMAND CSAHVolumetricOutputQualificationTests)

  add_executable(CSAHVolumetricShaderBytecodeTests
    "${ROOT_DIR}/tests/VolumetricShaderBytecodeTests.cpp")
  target_compile_features(
    CSAHVolumetricShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHVolumetricShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHVolumetricShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHVolumetricShaderBytecodeTests PRIVATE
    d3d11 d3dcompiler)
  add_dependencies(
    CSAHVolumetricShaderBytecodeTests
    VolumetricLightingShaders)
  add_test(NAME CSAH.VolumetricShaderBytecode
    COMMAND CSAHVolumetricShaderBytecodeTests
      "${VOLUMETRIC_QUALIFY_SHADER_BINARY}"
      "${VOLUMETRIC_RESOLVE_SHADER_BINARY}"
      "${VOLUMETRIC_COMPOSITE_SHADER_BINARY}")

  add_executable(CSAHVanillaFixesSettingsTests
    "${ROOT_DIR}/tests/VanillaFixesSettingsTests.cpp"
    "${VANILLA_SETTINGS_STORE_SOURCE}"
    "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettingsStore.h"
    "${VANILLA_FIXES_SOURCE_DIR}/VanillaFixesSettings.h"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHVanillaFixesSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHVanillaFixesSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHVanillaFixesSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHVanillaFixesSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHVanillaFixesSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHVanillaFixesSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.VanillaFixesSettings
    COMMAND CSAHVanillaFixesSettingsTests)

  add_executable(CSAHNativeShadowSettingsTests
    "${ROOT_DIR}/tests/NativeShadowSettingsTests.cpp"
    "${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
    "${NATIVE_SHADOW_SETTINGS_STORE_HEADER}"
    "${NATIVE_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHNativeShadowSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHNativeShadowSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHNativeShadowSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHNativeShadowSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHNativeShadowSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHNativeShadowSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.NativeShadowSettings
    COMMAND CSAHNativeShadowSettingsTests)

  add_test(NAME CSAH.NativeShadowsSource
    COMMAND "${CMAKE_COMMAND}"
      "-DNATIVE_SHADOW_RUNTIME_SOURCE=${NATIVE_SHADOW_RUNTIME_SOURCE}"
      "-DNATIVE_SHADOW_RUNTIME_HEADER=${NATIVE_SHADOW_RUNTIME_HEADER}"
      "-DNATIVE_SHADOW_SETTINGS_HEADER=${NATIVE_SHADOW_SETTINGS_HEADER}"
      "-DNATIVE_SHADOW_SETTINGS_STORE_SOURCE=${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}"
      "-DNATIVE_SHADOW_PATCH_MODEL=${NATIVE_SHADOW_PATCH_MODEL}"
      "-DPLUGIN_SOURCE=${PLUGIN_SOURCE}"
      -P "${NATIVE_SHADOW_VERIFY_SCRIPT}")

  add_executable(CSAHStereoMeasurementTests "${ROOT_DIR}/tests/StereoMeasurementTests.cpp"
    "${SOURCE_DIR}/diagnostics/stereo_measurement/StereoMeasurement.cpp"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}" "${GPU_TIMING_PROFILER_SOURCE}")
  add_dependencies(CSAHStereoMeasurementTests StereoMeasurementShader)
  target_compile_features(CSAHStereoMeasurementTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHStereoMeasurementTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHStereoMeasurementTests PRIVATE "${SOURCE_DIR}" "${STEREO_MEASUREMENT_DIR}")
  target_precompile_headers(CSAHStereoMeasurementTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHStereoMeasurementTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.StereoMeasurement COMMAND CSAHStereoMeasurementTests)

  add_executable(CSAHFeatureComputeOptimizationTests "${ROOT_DIR}/tests/FeatureComputeOptimizationTests.cpp"
    "${BLOOM_GLARE_RUNTIME_SOURCE}" "${IBL_COMPUTE_STATE_SCOPE_SOURCE}")
  add_dependencies(CSAHFeatureComputeOptimizationTests BloomGlareShaders)
  target_include_directories(CSAHFeatureComputeOptimizationTests PRIVATE "${SOURCE_DIR}" "${BLOOM_GLARE_GENERATED_DIR}")
  target_precompile_headers(CSAHFeatureComputeOptimizationTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_compile_features(CSAHFeatureComputeOptimizationTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHFeatureComputeOptimizationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHFeatureComputeOptimizationTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.FeatureComputeOptimization COMMAND CSAHFeatureComputeOptimizationTests "${ROOT_DIR}")
  add_test(NAME CSAH.IblTemplateInputs
    COMMAND "${Python3_EXECUTABLE}" "${ROOT_DIR}/tests/test_ibl_template_inputs.py")

  add_executable(CSAHRenderOverheadTests "${ROOT_DIR}/tests/RenderOverheadTests.cpp"
    "${SOURCE_DIR}/render/GpuTimingProfiler.cpp")
  target_compile_features(CSAHRenderOverheadTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHRenderOverheadTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHRenderOverheadTests PRIVATE "${SOURCE_DIR}")
  target_link_libraries(CSAHRenderOverheadTests PRIVATE d3d11 spdlog::spdlog)
  add_test(NAME CSAH.RenderOverhead COMMAND CSAHRenderOverheadTests)

  add_executable(CSAHCloudGeometryCaptureTests "${ROOT_DIR}/tests/CloudGeometryCaptureTests.cpp"
    "${SOURCE_DIR}/Features/cloud_shadows/CloudGeometryCapture.cpp"
    "${SOURCE_DIR}/Features/cloud_shadows/CloudShadowRuntime.cpp"
    "${SOURCE_DIR}/render/ComputeStateScope.cpp")
  target_compile_features(CSAHCloudGeometryCaptureTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHCloudGeometryCaptureTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHCloudGeometryCaptureTests PRIVATE "${SOURCE_DIR}" "${SOURCE_DIR}/Features/cloud_shadows")
  target_precompile_headers(CSAHCloudGeometryCaptureTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHCloudGeometryCaptureTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.CloudGeometryCapture COMMAND CSAHCloudGeometryCaptureTests "${ROOT_DIR}")

  add_executable(CSAHContactShadowTransmissionTests "${ROOT_DIR}/tests/ContactShadowTransmissionTests.cpp")
  target_compile_features(CSAHContactShadowTransmissionTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHContactShadowTransmissionTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHContactShadowTransmissionTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.ContactShadowTransmission COMMAND CSAHContactShadowTransmissionTests "${ROOT_DIR}")

  add_executable(CSAHContactShadowShaderBytecodeTests
    "${ROOT_DIR}/tests/ContactShadowShaderBytecodeTests.cpp")
  target_compile_features(
    CSAHContactShadowShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHContactShadowShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHContactShadowShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHContactShadowShaderBytecodeTests PRIVATE
    d3d11 d3dcompiler)
  add_dependencies(
    CSAHContactShadowShaderBytecodeTests ContactShadowShader)
  add_test(NAME CSAH.ContactShadowShaderBytecode
    COMMAND CSAHContactShadowShaderBytecodeTests
      "${CONTACT_SHADOW_SHADER_BINARY}"
      "${CONTACT_SHADOW_MASK_BINARY}")

  add_test(NAME CSAH.IblFoundationSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DIBL_RUNTIME_SOURCE=${IBL_RUNTIME_SOURCE}' '-DIBL_RUNTIME_HEADER=${IBL_RUNTIME_HEADER}' '-DIBL_SETTINGS_HEADER=${IBL_SETTINGS_HEADER}' '-DIBL_SETTINGS_STORE_SOURCE=${IBL_SETTINGS_STORE_SOURCE}' '-DIBL_SETTINGS_STORE_HEADER=${IBL_SETTINGS_STORE_HEADER}' '-DIBL_PROVIDER_MODEL=${IBL_PROVIDER_MODEL}' '-DIBL_ENVIRONMENT_PROVIDER_SOURCE=${IBL_ENVIRONMENT_PROVIDER_SOURCE}' '-DIBL_ENVIRONMENT_PROVIDER_HEADER=${IBL_ENVIRONMENT_PROVIDER_HEADER}' '-DIBL_ENVIRONMENT_UPDATER_SOURCE=${IBL_ENVIRONMENT_UPDATER_SOURCE}' '-DIBL_ENVIRONMENT_UPDATER_HEADER=${IBL_ENVIRONMENT_UPDATER_HEADER}' '-DIBL_COMPUTE_STATE_SCOPE_SOURCE=${IBL_COMPUTE_STATE_SCOPE_SOURCE}' '-DIBL_COMPUTE_STATE_SCOPE_HEADER=${IBL_COMPUTE_STATE_SCOPE_HEADER}' '-DIBL_PROJECTION_MODEL=${IBL_PROJECTION_MODEL}' '-DIBL_CAPTURE_PROBE_MODEL=${IBL_CAPTURE_PROBE_MODEL}' '-DIBL_SCENE_RADIANCE_PROBE_MODEL=${IBL_SCENE_RADIANCE_PROBE_MODEL}' '-DIBL_REFLECTION_FREE_CAPTURE_SOURCE=${IBL_REFLECTION_FREE_CAPTURE_SOURCE}' '-DIBL_REFLECTION_FREE_CAPTURE_HEADER=${IBL_REFLECTION_FREE_CAPTURE_HEADER}' '-DIBL_MATERIAL_BINDING_SCOPE_SOURCE=${IBL_MATERIAL_BINDING_SCOPE_SOURCE}' '-DIBL_MATERIAL_BINDING_SCOPE_HEADER=${IBL_MATERIAL_BINDING_SCOPE_HEADER}' '-DIBL_MATERIAL_GENERATED_CONTRACTS=${IBL_MATERIAL_GENERATED_CONTRACTS}' '-DIBL_PROJECTION_SHADER_SOURCE=${IBL_PROJECTION_SHADER_SOURCE}' '-DIBL_PROJECTION_SHADER_ASSET=${IBL_PROJECTION_SHADER_ASSET}' '-DIBL_ENVIRONMENT_UPDATE_SHADER_SOURCE=${IBL_ENVIRONMENT_UPDATE_SHADER_SOURCE}' '-DIBL_ENVIRONMENT_UPDATE_SHADER_ASSET=${IBL_ENVIRONMENT_UPDATE_SHADER_ASSET}' '-DIBL_ENVIRONMENT_FILTER_SHADER_SOURCE=${IBL_ENVIRONMENT_FILTER_SHADER_SOURCE}' '-DIBL_ENVIRONMENT_FILTER_SHADER_ASSET=${IBL_ENVIRONMENT_FILTER_SHADER_ASSET}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DGEOMETRY_HOOK_SOURCE=${RENDERER_HOOK_SOURCE}' '-DPALM_SETTINGS_SOURCE=${PALM_SETTINGS_SOURCE}' '-DPLUGIN_SOURCE=${PLUGIN_SOURCE}' '-DRESOURCE_SOURCE=${RESOURCE_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyIblFoundation.cmake'")

  add_test(NAME CSAH.IblProjectionShaderAsset
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_PROJECTION_SHADER_VERIFIER}"
      --root "${ROOT_DIR}"
      --check)

  add_test(NAME CSAH.IblCaptureProbeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_CAPTURE_PROBE_VERIFIER}"
      --root "${ROOT_DIR}")

  add_test(NAME CSAH.IblMaterialContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${IBL_MATERIAL_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${IBL_MATERIAL_GENERATED_CONTRACTS}"
      --surface-anchor-tool "$<TARGET_FILE:VanillaReflectionPatchTool>"
      --check)

  add_test(NAME CSAH.ComplexMaterialProducerContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${COMPLEX_MATERIAL_PRODUCER_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${COMPLEX_MATERIAL_PRODUCER_GENERATED_CONTRACTS}"
      --check)

  add_test(NAME CSAH.ComplexEnvironmentContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${COMPLEX_ENVIRONMENT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${COMPLEX_ENVIRONMENT_GENERATED_CONTRACTS}"
      --check)

  add_test(NAME CSAH.LinearLightingConstantScopeSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DLINEAR_LIGHTING_RUNTIME_SOURCE=${LINEAR_LIGHTING_RUNTIME_SOURCE}' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyLinearLightingConstantScope.cmake'")

  add_test(NAME CSAH.DFTiledPointLightHookSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DPOINT_LIGHT_HOOK_SOURCE=${POINT_LIGHT_HOOK_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyDFTiledPointLightHook.cmake'")

  add_test(NAME CSAH.LinearLightingQualificationSource
    COMMAND "${PWSH_EXECUTABLE}" -NoProfile -Command
      "& '${CMAKE_COMMAND}' '-DPLUGIN_SOURCE=${SOURCE_DIR}/XSEPlugin.cpp' '-DD3D11_HOOK_SOURCE=${D3D11_HOOK_SOURCE}' '-DQUALIFICATION_SOURCE=${QUALIFICATION_SOURCE}' '-P' '${ROOT_DIR}/cmake/VerifyLinearLightingQualification.cmake'")

  add_executable(CSAHD3D11MethodDetourSmokeTests
    "${ROOT_DIR}/tests/D3D11MethodDetourSmokeTests.cpp")
  target_compile_features(
    CSAHD3D11MethodDetourSmokeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHD3D11MethodDetourSmokeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHD3D11MethodDetourSmokeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHD3D11MethodDetourSmokeTests PRIVATE
    minhook::minhook d3d11)
  add_test(NAME CSAH.D3D11MethodDetourSmoke
    COMMAND CSAHD3D11MethodDetourSmokeTests)

  add_executable(CSAHLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/LinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHLinearLightingShaderParityTests PRIVATE
    d3d11 d3dcompiler)
  add_test(NAME CSAH.LinearLightingShaderParity
    COMMAND CSAHLinearLightingShaderParityTests
      "${ROOT_DIR}")

  add_executable(CSAHIblProjectionTests
    "${ROOT_DIR}/tests/IblProjectionTests.cpp"
    "${IBL_PROJECTION_MODEL}")
  target_compile_features(
    CSAHIblProjectionTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblProjectionTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblProjectionTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblProjectionTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHIblProjectionTests PRIVATE d3d11)
  add_test(NAME CSAH.IblProjection
    COMMAND CSAHIblProjectionTests "${ROOT_DIR}")

  add_executable(CSAHIblCaptureProbeModelTests
    "${ROOT_DIR}/tests/IblCaptureProbeModelTests.cpp"
    "${IBL_CAPTURE_PROBE_MODEL}")
  target_compile_features(
    CSAHIblCaptureProbeModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHIblCaptureProbeModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblCaptureProbeModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.IblCaptureProbeModel
    COMMAND CSAHIblCaptureProbeModelTests)

  add_executable(CSAHIblSceneRadianceProbeModelTests
    "${ROOT_DIR}/tests/IblSceneRadianceProbeModelTests.cpp"
    "${IBL_SCENE_RADIANCE_PROBE_MODEL}")
  target_compile_features(
    CSAHIblSceneRadianceProbeModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHIblSceneRadianceProbeModelTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblSceneRadianceProbeModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.IblSceneRadianceProbeModel
    COMMAND CSAHIblSceneRadianceProbeModelTests)

  add_executable(CSAHIblReflectionFreeCaptureTests
    "${ROOT_DIR}/tests/IblReflectionFreeCaptureTests.cpp"
    "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "${IBL_REFLECTION_FREE_CAPTURE_HEADER}"
    "${GPU_TIMING_PROFILER_SOURCE}"
    "${GPU_TIMING_PROFILER_HEADER}")
  target_compile_features(
    CSAHIblReflectionFreeCaptureTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblReflectionFreeCaptureTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblReflectionFreeCaptureTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblReflectionFreeCaptureTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHIblReflectionFreeCaptureTests PRIVATE
    d3d11 d3dcompiler dxguid spdlog::spdlog)
  add_test(NAME CSAH.IblReflectionFreeCapture
    COMMAND CSAHIblReflectionFreeCaptureTests)

  add_executable(CSAHIblMaterialBindingScopeTests
    "${ROOT_DIR}/tests/IblMaterialBindingScopeTests.cpp"
    "${IBL_MATERIAL_BINDING_SCOPE_SOURCE}"
    "${IBL_MATERIAL_BINDING_SCOPE_HEADER}")
  target_compile_features(
    CSAHIblMaterialBindingScopeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblMaterialBindingScopeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblMaterialBindingScopeTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblMaterialBindingScopeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHIblMaterialBindingScopeTests PRIVATE d3d11)
  add_test(NAME CSAH.IblMaterialBindingScope
    COMMAND CSAHIblMaterialBindingScopeTests)

  add_executable(CSAHIblMaterialShaderBytecodeTests
    "${ROOT_DIR}/tests/IblMaterialShaderBytecodeTests.cpp")
  target_compile_features(
    CSAHIblMaterialShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblMaterialShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHIblMaterialShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHIblMaterialShaderBytecodeTests PRIVATE d3d11)
  add_test(NAME CSAH.IblMaterialShaderBytecode
    COMMAND CSAHIblMaterialShaderBytecodeTests "${ROOT_DIR}")

  add_executable(CSAHIblMaterialStyleTests "${ROOT_DIR}/tests/IblMaterialStyleTests.cpp")
  target_compile_features(CSAHIblMaterialStyleTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHIblMaterialStyleTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHIblMaterialStyleTests PRIVATE d3d11)
  add_test(NAME CSAH.IblMaterialStyle COMMAND CSAHIblMaterialStyleTests "${ROOT_DIR}")

  add_executable(CSAHIblReferenceLoadTests "${ROOT_DIR}/tests/IblReferenceLoadTests.cpp")
  target_compile_features(CSAHIblReferenceLoadTests PRIVATE cxx_std_23)
  target_include_directories(CSAHIblReferenceLoadTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME CSAH.IblReferenceLoad COMMAND CSAHIblReferenceLoadTests)

  add_executable(CSAHIblProviderTests
    "${ROOT_DIR}/tests/IblProviderTests.cpp"
    "${IBL_PROVIDER_MODEL}"
    "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_HEADER}")
  target_compile_features(
    CSAHIblProviderTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblProviderTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblProviderTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblProviderTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHIblProviderTests PRIVATE d3d11 d3dcompiler dxguid)
  add_test(NAME CSAH.IblProvider
    COMMAND CSAHIblProviderTests)

  add_executable(CSAHIblEnvironmentUpdaterTests
    "${ROOT_DIR}/tests/IblEnvironmentUpdaterTests.cpp"
    "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "${IBL_PROVIDER_MODEL}"
    "${IBL_SCENE_RADIANCE_PROBE_MODEL}"
    "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "${IBL_ENVIRONMENT_PROVIDER_HEADER}"
    "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
    "${IBL_ENVIRONMENT_UPDATER_HEADER}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_HEADER}"
    "${GPU_TIMING_PROFILER_SOURCE}"
    "${GPU_TIMING_PROFILER_HEADER}")
  target_compile_features(
    CSAHIblEnvironmentUpdaterTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblEnvironmentUpdaterTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblEnvironmentUpdaterTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblEnvironmentUpdaterTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHIblEnvironmentUpdaterTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHIblEnvironmentUpdaterTests PRIVATE
    CommonLibF4::CommonLibF4 d3d11 dxguid shell32 spdlog::spdlog)
  add_test(NAME CSAH.IblEnvironmentUpdater
    COMMAND CSAHIblEnvironmentUpdaterTests "${ROOT_DIR}")

  add_executable(CSAHInverseSquareTests "${ROOT_DIR}/tests/InverseSquareTests.cpp"
    "${SOURCE_DIR}/Features/inverse_square/InverseSquareRuntime.cpp")
  add_dependencies(CSAHInverseSquareTests InverseSquareShaders)
  target_compile_features(CSAHInverseSquareTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHInverseSquareTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHInverseSquareTests PRIVATE "${SOURCE_DIR}" "${INVERSE_SQUARE_GENERATED_DIR}")
  target_precompile_headers(CSAHInverseSquareTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHInverseSquareTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.InverseSquare COMMAND CSAHInverseSquareTests "${INVERSE_SQUARE_GENERATED_DIR}")
  add_executable(CSAHInteriorScatteringTests "${ROOT_DIR}/tests/InteriorScatteringTests.cpp"
    "${SOURCE_DIR}/Features/surface_classification/SurfaceClassificationRuntime.cpp"
    "${SOURCE_DIR}/Features/subsurface_scattering/SubsurfaceScatteringRuntime.cpp"
    "${SOURCE_DIR}/render/ComputeStateScope.cpp" "${SOURCE_DIR}/render/GpuTimingProfiler.cpp")
  add_dependencies(CSAHInteriorScatteringTests SubsurfaceScatteringShader)
  target_compile_features(CSAHInteriorScatteringTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHInteriorScatteringTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHInteriorScatteringTests PRIVATE "${SOURCE_DIR}" "${SUBSURFACE_SCATTERING_GENERATED_DIR}")
  target_precompile_headers(CSAHInteriorScatteringTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHInteriorScatteringTests PRIVATE CommonLibF4::CommonLibF4 d3d11 spdlog::spdlog)
  add_test(NAME CSAH.InteriorScattering COMMAND CSAHInteriorScatteringTests)
  add_executable(CSAHHeightFogTests "${ROOT_DIR}/tests/HeightFogTests.cpp")
  target_compile_features(CSAHHeightFogTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHHeightFogTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHHeightFogTests PRIVATE "${SOURCE_DIR}")
  target_link_libraries(CSAHHeightFogTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.HeightFog COMMAND CSAHHeightFogTests "${ROOT_DIR}")
  add_executable(CSAHTerrainShadowTests "${ROOT_DIR}/tests/TerrainShadowTests.cpp")
  target_compile_features(CSAHTerrainShadowTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHTerrainShadowTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHTerrainShadowTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.TerrainShadows COMMAND CSAHTerrainShadowTests "${ROOT_DIR}")
  add_test(NAME CSAH.TerrainHeightmaps COMMAND "${Python3_EXECUTABLE}"
    "${ROOT_DIR}/tests/TerrainHeightmapTests.py" "${CMAKE_BINARY_DIR}/test-output/terrain")
  add_executable(CSAHWaterCausticsTests "${ROOT_DIR}/tests/WaterCausticsTests.cpp"
    "${SOURCE_DIR}/Features/water_effects/WaterEffectsRuntime.cpp" "${SOURCE_DIR}/Features/water_effects/WaterWorldData.cpp")
  add_dependencies(CSAHWaterCausticsTests WaterEffectsAssets ContactShadowShader)
  target_compile_features(CSAHWaterCausticsTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHWaterCausticsTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHWaterCausticsTests PRIVATE "${SOURCE_DIR}" "${WATER_EFFECTS_GENERATED_DIR}" "${CONTACT_SHADOW_GENERATED_DIR}")
  target_precompile_headers(CSAHWaterCausticsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHWaterCausticsTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.WaterCaustics COMMAND CSAHWaterCausticsTests "${ROOT_DIR}")

  add_executable(CSAHWaterScreenReflectionTests "${ROOT_DIR}/tests/WaterScreenReflectionTests.cpp"
    "${SOURCE_DIR}/Features/water_effects/WaterEffectsRuntime.cpp" "${SOURCE_DIR}/Features/water_effects/WaterWorldData.cpp")
  add_dependencies(CSAHWaterScreenReflectionTests WaterEffectsAssets)
  target_compile_features(CSAHWaterScreenReflectionTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHWaterScreenReflectionTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHWaterScreenReflectionTests PRIVATE "${SOURCE_DIR}" "${WATER_EFFECTS_GENERATED_DIR}")
  target_precompile_headers(CSAHWaterScreenReflectionTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHWaterScreenReflectionTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.WaterScreenReflections COMMAND CSAHWaterScreenReflectionTests "${ROOT_DIR}")
  add_executable(CSAHWaterParallaxTests "${ROOT_DIR}/tests/WaterParallaxTests.cpp"
    "${SOURCE_DIR}/Features/water_effects/WaterEffectsRuntime.cpp" "${SOURCE_DIR}/Features/water_effects/WaterWorldData.cpp")
  add_dependencies(CSAHWaterParallaxTests WaterEffectsAssets)
  target_compile_features(CSAHWaterParallaxTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHWaterParallaxTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHWaterParallaxTests PRIVATE "${SOURCE_DIR}" "${WATER_EFFECTS_GENERATED_DIR}")
  target_precompile_headers(CSAHWaterParallaxTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHWaterParallaxTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.WaterParallax COMMAND CSAHWaterParallaxTests "${ROOT_DIR}")

  add_executable(CSAHScreenSpaceGITests "${ROOT_DIR}/tests/ScreenSpaceGITests.cpp"
    "${SOURCE_DIR}/Features/screen_space_gi/ScreenSpaceGIRuntime.cpp" "${SOURCE_DIR}/render/ComputeStateScope.cpp")
  add_dependencies(CSAHScreenSpaceGITests ScreenSpaceGIShaders SkylightingShaders)
  target_compile_features(CSAHScreenSpaceGITests PRIVATE cxx_std_23)
  target_compile_options(CSAHScreenSpaceGITests PRIVATE /Zi)
  target_link_options(CSAHScreenSpaceGITests PRIVATE /DEBUG:FULL)
  target_compile_definitions(CSAHScreenSpaceGITests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHScreenSpaceGITests PRIVATE "${SOURCE_DIR}" "${SSGI_GENERATED_DIR}" "${SKYLIGHTING_GENERATED_DIR}")
  target_precompile_headers(CSAHScreenSpaceGITests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHScreenSpaceGITests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.ScreenSpaceGI COMMAND CSAHScreenSpaceGITests "${ROOT_DIR}")

  add_executable(CSAHTerrainVariationTests "${ROOT_DIR}/tests/TerrainVariationTests.cpp"
    "${SOURCE_DIR}/Features/terrain_variation/TerrainVariationRuntime.cpp")
  target_compile_features(CSAHTerrainVariationTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHTerrainVariationTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHTerrainVariationTests PRIVATE "${SOURCE_DIR}")
  target_precompile_headers(CSAHTerrainVariationTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHTerrainVariationTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.TerrainVariation COMMAND CSAHTerrainVariationTests "${ROOT_DIR}")

  add_executable(CSAHLodBlendingTests "${ROOT_DIR}/tests/LodBlendingTests.cpp"
    "${SOURCE_DIR}/Features/lod_blending/LodBlendingRuntime.cpp")
  target_compile_features(CSAHLodBlendingTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHLodBlendingTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHLodBlendingTests PRIVATE "${SOURCE_DIR}")
  target_precompile_headers(CSAHLodBlendingTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHLodBlendingTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler spdlog::spdlog)
  add_test(NAME CSAH.LodBlending COMMAND CSAHLodBlendingTests "${ROOT_DIR}")

  add_executable(CSAHSkylightingResourceTests
    "${ROOT_DIR}/tests/SkylightingResourceTests.cpp"
    "${SKYLIGHTING_RUNTIME_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${GPU_TIMING_PROFILER_SOURCE}")
  add_dependencies(CSAHSkylightingResourceTests SkylightingShaders)
  target_compile_features(CSAHSkylightingResourceTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHSkylightingResourceTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHSkylightingResourceTests PRIVATE "${SOURCE_DIR}" "${SKYLIGHTING_GENERATED_DIR}")
  target_precompile_headers(CSAHSkylightingResourceTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHSkylightingResourceTests PRIVATE CommonLibF4::CommonLibF4 d3d11 dxguid shell32 spdlog::spdlog)
  add_test(NAME CSAH.SkylightingResources COMMAND CSAHSkylightingResourceTests)

  add_test(NAME CSAH.ShaderNestedRemapping COMMAND "${Python3_EXECUTABLE}" "${ROOT_DIR}/tests/test_shader_nested_remapping.py")

  add_executable(CSAHSkylightingSunHistoryTests "${ROOT_DIR}/tests/SkylightingSunHistoryTests.cpp")
  target_compile_features(CSAHSkylightingSunHistoryTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHSkylightingSunHistoryTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHSkylightingSunHistoryTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.SkylightingSunHistory COMMAND CSAHSkylightingSunHistoryTests "${ROOT_DIR}")

  add_executable(CSAHSkylightingSamplingTests "${ROOT_DIR}/tests/SkylightingSamplingTests.cpp")
  target_compile_features(CSAHSkylightingSamplingTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHSkylightingSamplingTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(CSAHSkylightingSamplingTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.SkylightingSampling COMMAND CSAHSkylightingSamplingTests "${ROOT_DIR}")

  add_executable(CSAHUpscalingRecoveryTests
    "${ROOT_DIR}/tests/UpscalingRecoveryTests.cpp"
    "${SOURCE_DIR}/Features/dlaa/DlaaRuntime.cpp"
    "${GPU_TIMING_PROFILER_SOURCE}"
    "${SOURCE_DIR}/Features/volumetric_lighting/VolumetricStateScope.cpp"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}")
  add_dependencies(CSAHUpscalingRecoveryTests UpscalingShaders)
  target_compile_features(CSAHUpscalingRecoveryTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHUpscalingRecoveryTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHUpscalingRecoveryTests PRIVATE "${SOURCE_DIR}" "${DLAA_GENERATED_DIR}")
  target_precompile_headers(CSAHUpscalingRecoveryTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHUpscalingRecoveryTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler dxguid spdlog::spdlog)
  add_test(NAME CSAH.UpscalingRecovery COMMAND CSAHUpscalingRecoveryTests "${ROOT_DIR}")

  add_executable(CSAHFo4VrDepthTests "${ROOT_DIR}/tests/Fo4VrDepthTests.cpp")
  target_compile_features(CSAHFo4VrDepthTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHFo4VrDepthTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHFo4VrDepthTests PRIVATE "${SOURCE_DIR}")
  target_link_libraries(CSAHFo4VrDepthTests PRIVATE d3d11 d3dcompiler)
  add_test(NAME CSAH.Fo4VrDepth COMMAND CSAHFo4VrDepthTests "${ROOT_DIR}")

  add_executable(CSAHRenderDocCaptureTests
    "${ROOT_DIR}/tests/RenderDocCaptureTests.cpp"
    "${SOURCE_DIR}/diagnostics/RenderDocCapture.cpp")
  target_compile_features(CSAHRenderDocCaptureTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHRenderDocCaptureTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHRenderDocCaptureTests PRIVATE "${SOURCE_DIR}" "${ROOT_DIR}/extern/RenderDoc")
  target_precompile_headers(CSAHRenderDocCaptureTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHRenderDocCaptureTests PRIVATE CommonLibF4::CommonLibF4 d3d11 dxguid shell32 spdlog::spdlog)
  if(EXISTS "$ENV{ProgramFiles}/RenderDoc/renderdoc.dll")
    add_test(NAME CSAH.RenderDocCapture COMMAND CSAHRenderDocCaptureTests
      "${CMAKE_BINARY_DIR}/test-output/renderdoc" "$ENV{ProgramFiles}/RenderDoc/renderdoc.dll")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-output")
    set_tests_properties(CSAH.RenderDocCapture PROPERTIES
      ENVIRONMENT "TEMP=${CMAKE_BINARY_DIR}/test-output;TMP=${CMAKE_BINARY_DIR}/test-output"
      TIMEOUT 30)
  endif()

  add_executable(CSAHIblSettingsTests
    "${ROOT_DIR}/tests/IblSettingsTests.cpp"
    "${IBL_SETTINGS_STORE_SOURCE}"
    "${IBL_SETTINGS_STORE_HEADER}"
    "${IBL_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHIblSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHIblSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHIblSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHIblSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHIblSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHIblSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.IblSettings
    COMMAND CSAHIblSettingsTests)

  add_executable(CSAHPbrMaterialModelTests
    "${ROOT_DIR}/tests/PbrMaterialModelTests.cpp"
    "${PBR_MATERIAL_MODEL_HEADER}"
    "${PBR_SETTINGS_HEADER}")
  target_compile_features(
    CSAHPbrMaterialModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHPbrMaterialModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHPbrMaterialModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.PbrMaterialModel
    COMMAND CSAHPbrMaterialModelTests)

  add_executable(CSAHPbrSettingsTests
    "${ROOT_DIR}/tests/PbrSettingsTests.cpp"
    "${PBR_SETTINGS_STORE_SOURCE}"
    "${PBR_SETTINGS_STORE_HEADER}"
    "${PBR_SETTINGS_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}"
    "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}"
    "${CONTACT_SHADOW_SETTINGS_HEADER}"
    "${SETTINGS_PATH_SOURCE}"
    "${SETTINGS_PATH_HEADER}")
  target_compile_features(
    CSAHPbrSettingsTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHPbrSettingsTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHPbrSettingsTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHPbrSettingsTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_precompile_headers(
    CSAHPbrSettingsTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(
    CSAHPbrSettingsTests PRIVATE
    CommonLibF4::CommonLibF4 shell32 spdlog::spdlog)
  add_test(NAME CSAH.PbrSettings
    COMMAND CSAHPbrSettingsTests)

  add_test(NAME CSAH.AuthoredPbrPackBuilder
    COMMAND "${Python3_EXECUTABLE}" "${AUTHORED_PBR_PACK_TEST}")

  add_executable(CSAHLinearLightingQualificationTests
    "${ROOT_DIR}/tests/LinearLightingQualificationTests.cpp"
    "${SOURCE_DIR}/diagnostics/LinearLightingQualificationModel.h")
  target_compile_features(
    CSAHLinearLightingQualificationTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHLinearLightingQualificationTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHLinearLightingQualificationTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.LinearLightingQualification
    COMMAND CSAHLinearLightingQualificationTests)

  add_executable(CSAHDFLightAmbientShaderPatchTests
    "${ROOT_DIR}/tests/DFLightAmbientShaderPatchTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
  target_compile_features(
    CSAHDFLightAmbientShaderPatchTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHDFLightAmbientShaderPatchTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHDFLightAmbientShaderPatchTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  add_test(NAME CSAH.DFLightAmbientShaderPatch
    COMMAND CSAHDFLightAmbientShaderPatchTests)

  add_executable(CSAHDFLightAmbientShaderBytecodeTests
    "${ROOT_DIR}/tests/DFLightAmbientShaderBytecodeTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightAmbientShaderPatch.h")
  target_compile_features(
    CSAHDFLightAmbientShaderBytecodeTests PRIVATE cxx_std_23)
  target_compile_definitions(
    CSAHDFLightAmbientShaderBytecodeTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(
    CSAHDFLightAmbientShaderBytecodeTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_options(
    CSAHDFLightAmbientShaderBytecodeTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHDFLightAmbientShaderBytecodeTests PRIVATE d3d11)
  add_test(NAME CSAH.DFLightAmbientShaderBytecode
    COMMAND CSAHDFLightAmbientShaderBytecodeTests
      "${ROOT_DIR}")

  add_executable(CSAHDFTiledPointLightModelTests
    "${ROOT_DIR}/tests/DFTiledPointLightModelTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFTiledPointLightModel.h")
  target_compile_features(
    CSAHDFTiledPointLightModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHDFTiledPointLightModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHDFTiledPointLightModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHDFTiledPointLightModelTests PRIVATE
    CSAHCore)
  add_test(NAME CSAH.DFTiledPointLightModel
    COMMAND CSAHDFTiledPointLightModelTests)

  add_executable(CSAHDFLightProducerModelTests
    "${ROOT_DIR}/tests/DFLightProducerModelTests.cpp"
    "${SOURCE_DIR}/Features/linear_lighting/DFLightProducerModel.h")
  target_compile_features(
    CSAHDFLightProducerModelTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHDFLightProducerModelTests PRIVATE "${SOURCE_DIR}")
  target_compile_options(
    CSAHDFLightProducerModelTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHDFLightProducerModelTests PRIVATE
    CSAHCore)
  add_test(NAME CSAH.DFLightProducerModel
    COMMAND CSAHDFLightProducerModelTests)

  add_test(NAME CSAH.LinearLightingShader
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/verify_linear_lighting_shader.py"
      --root "${ROOT_DIR}")
  add_test(NAME CSAH.LinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.DFLightAmbientRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${DFLIGHT_AMBIENT_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${DFLIGHT_AMBIENT_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.SkyLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${SKY_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${SKY_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.DistantTreeLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${DISTANT_TREE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${DISTANT_TREE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
      --check)
  add_test(NAME CSAH.ParticleLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${PARTICLE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${PARTICLE_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.WaterLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${WATER_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${WATER_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.EffectLinearLightingRuntimeContracts
    COMMAND "${Python3_EXECUTABLE}"
      "${EFFECT_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${EFFECT_LINEAR_LIGHTING_GENERATED_CONTRACTS}"
      --check)
  add_test(NAME CSAH.VLSCompositeLinearLightingRuntimeContract
    COMMAND "${Python3_EXECUTABLE}"
      "${VLS_COMPOSITE_LINEAR_LIGHTING_CONTRACT_GENERATOR}"
      --root "${ROOT_DIR}"
      --output "${VLS_COMPOSITE_LINEAR_LIGHTING_GENERATED_CONTRACT}"
      --check)
  add_executable(CSAHSkyLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/SkyLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHSkyLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHSkyLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHSkyLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHSkyLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHSkyLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.SkyLinearLightingShaderParity
    COMMAND CSAHSkyLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(CSAHDistantTreeLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/DistantTreeLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHDistantTreeLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    CSAHDistantTreeLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHDistantTreeLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHDistantTreeLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHDistantTreeLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.DistantTreeLinearLightingShaderParity
    COMMAND CSAHDistantTreeLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(CSAHParticleLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/ParticleLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHParticleLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    CSAHParticleLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHParticleLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHParticleLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHParticleLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.ParticleLinearLightingShaderParity
    COMMAND CSAHParticleLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(CSAHWaterLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/WaterLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHWaterLinearLightingShaderParityTests PRIVATE cxx_std_23)
  target_include_directories(
    CSAHWaterLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHWaterLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHWaterLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHWaterLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.WaterLinearLightingShaderParity
    COMMAND CSAHWaterLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(CSAHEffectLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/EffectLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHEffectLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    CSAHEffectLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHEffectLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHEffectLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHEffectLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.EffectLinearLightingShaderParity
    COMMAND CSAHEffectLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_executable(CSAHVLSCompositeLinearLightingShaderParityTests
    "${ROOT_DIR}/tests/VLSCompositeLinearLightingShaderParityTests.cpp")
  target_compile_features(
    CSAHVLSCompositeLinearLightingShaderParityTests PRIVATE
    cxx_std_23)
  target_include_directories(
    CSAHVLSCompositeLinearLightingShaderParityTests PRIVATE
    "${SOURCE_DIR}")
  target_compile_definitions(
    CSAHVLSCompositeLinearLightingShaderParityTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(
    CSAHVLSCompositeLinearLightingShaderParityTests PRIVATE
    /sdl /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /W4)
  target_link_libraries(
    CSAHVLSCompositeLinearLightingShaderParityTests PRIVATE
    CSAHCore d3d11 d3dcompiler)
  add_test(NAME CSAH.VLSCompositeLinearLightingShaderParity
    COMMAND CSAHVLSCompositeLinearLightingShaderParityTests
      "${ROOT_DIR}")
  add_test(NAME CSAH.LinearLightingShaderCensus
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tools/census_linear_lighting_fxp.py"
      --input "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --known-dir "${ROOT_DIR}/package/Shaders/Community/VerifiedLinearLighting"
      --expect-total 288
      --expect-unique 288
      --expect-target-shape 288
      --expect-known 288)
  add_test(NAME CSAH.LinearLightingFxpParser
    COMMAND "${Python3_EXECUTABLE}"
      "${ROOT_DIR}/tests/test_census_linear_lighting_fxp.py")
endif()

target_sources(CSAH PRIVATE
  "${SOURCE_DIR}/Features/ibl/IblCompactCapture.cpp"
  "${SOURCE_DIR}/Features/ibl/IblCompactCapture.h")
add_custom_target(VerifyIblCompactCapture ALL
  COMMAND "${CMAKE_COMMAND}" -E env "PYTHONDONTWRITEBYTECODE=1"
    "${Python3_EXECUTABLE}" "${ROOT_DIR}/tools/generate_ibl_compact_capture.py" --root "${ROOT_DIR}" --check
  COMMENT "Verifying native compact cubemap lighting and coverage shaders"
  VERBATIM)
add_dependencies(CSAH VerifyIblCompactCapture)

if(BUILD_CSAH_TESTS)
  add_executable(CSAHIblCompactCaptureTests
    "${ROOT_DIR}/tests/IblCompactCaptureTests.cpp"
    "${SOURCE_DIR}/Features/ibl/IblCompactCapture.cpp"
    "${IBL_ENVIRONMENT_PROVIDER_SOURCE}"
    "${IBL_ENVIRONMENT_UPDATER_SOURCE}"
    "${IBL_REFLECTION_FREE_CAPTURE_SOURCE}"
    "${IBL_COMPUTE_STATE_SCOPE_SOURCE}"
    "${GPU_TIMING_PROFILER_SOURCE}")
  target_compile_features(CSAHIblCompactCaptureTests PRIVATE cxx_std_23)
  target_compile_definitions(CSAHIblCompactCaptureTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(CSAHIblCompactCaptureTests PRIVATE "${SOURCE_DIR}")
  target_precompile_headers(CSAHIblCompactCaptureTests PRIVATE "${SOURCE_DIR}/PCH.h")
  target_link_libraries(CSAHIblCompactCaptureTests PRIVATE CommonLibF4::CommonLibF4 d3d11 d3dcompiler dxguid shell32 spdlog::spdlog)
  add_test(NAME CSAH.IblCompactCapture COMMAND CSAHIblCompactCaptureTests "${ROOT_DIR}")
endif()
