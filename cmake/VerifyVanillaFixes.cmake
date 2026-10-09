function(vanilla_fixes_require_file variable_name)
  if(NOT DEFINED ${variable_name} OR NOT EXISTS "${${variable_name}}")
    message(FATAL_ERROR
      "Vanilla Fixes verification input '${variable_name}' is missing")
  endif()
endfunction()

function(vanilla_fixes_require_text source label)
  foreach(required IN LISTS ARGN)
    string(FIND "${source}" "${required}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR
        "Vanilla Fixes ${label} regression: missing '${required}'")
    endif()
  endforeach()
endfunction()

foreach(input IN ITEMS
    VANILLA_SHADER_RUNTIME_SOURCE
    VANILLA_FOCUS_RUNTIME_SOURCE
    VANILLA_SUN_OCCLUSION_RUNTIME_SOURCE
    VANILLA_RUNTIME_SOURCE
    VANILLA_SETTINGS_STORE_SOURCE
    VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE
    VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE
    VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE
    VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE
    VANILLA_REFLECTION_PATCH_SOURCE
    VANILLA_REFLECTION_PATCH_TOOL_SOURCE
    VANILLA_SSLR_ENVIRONMENT_SOURCE
    VANILLA_IBL_RUNTIME_SOURCE
    VANILLA_D3D11_HOOK_SOURCE
    VANILLA_PALM_SETTINGS_SOURCE
    VANILLA_SHARED_SETTINGS_SOURCE
    VANILLA_PLUGIN_SOURCE
    VANILLA_IBL_GENERATOR_SOURCE
    VANILLA_SAO_BLUR_SOURCE
    VANILLA_SAO_RAW_SOURCE
    VANILLA_SSLR_BLUR_SOURCE
    VANILLA_SSLR_PREPASS_SOURCE
    VANILLA_SSLR_RAYTRACE_SOURCE)
  vanilla_fixes_require_file(${input})
endforeach()

file(READ "${VANILLA_SHADER_RUNTIME_SOURCE}" shader_runtime)
vanilla_fixes_require_text("${shader_runtime}" "shader identity"
  "kSaoRawAo"
  "3892"
  "0x4A8B8CB64AC24499ull"
  "kSaoHorizontalBlur"
  "2324"
  "0x5186B7FAB41E51CEull"
  "kSslrHorizontalBlur"
  "800"
  "0x45D4CB8E6B37F5E7ull"
  "kSslrPrepass"
  "3080"
  "0xBB9FDCC3817DC31Cull"
  "kSslrRaytrace"
  "68624"
  "0xE4DB1ED97A719E55ull"
  "kFocusShadow"
  "19504"
  "0xF4EBA56324D74051ull"
  "CompleteIdentity{ 9348, 0x59FAED17411F08C7ull"
  "CompleteIdentity{ 9564, 0x0903D20AD75EBB0Eull"
  "CompleteIdentity{ 11100, 0x81247323F5EF60D3ull"
  "CompleteIdentity{ 11316, 0x29D9E8483ACB1988ull"
  "identity.hasDxbcHeader"
  "identity.bytecodeSize == expected.size"
  "identity.hash == expected.hash"
  "identity.checksum == expected.checksum"
  "patchStockReflectionCompositeSurfaceAnchoredCubemap"
  "csah_vanilla_sslr_raytrace_ps"
  "publishSslrPixelShaderPair"
  "selectSslrPixelShaderForBinding"
  "buildReflectionCompositeDiagnosticVariants"
  "publishReflectionCompositePixelShaderVariants"
  "selectSurfaceAnchoredCubemapPixelShaderForBinding"
  "selectReflectionCompositeDiagnosticPixelShaderForBinding"
  "isReflectionCompositeDiagnosticPixelShader"
  "setSurfaceAnchoredCubemapFixRequested")

file(READ "${VANILLA_FOCUS_RUNTIME_SOURCE}" focus_runtime)
vanilla_fixes_require_text("${focus_runtime}" "focus-shadow native/resource"
  "kProducerRva = 0x29126C0"
  "kTaskPrepareRva = 0x28AAC40"
  "kMapPublishRva = 0x2912500"
  "kMapTaskExecuteRva = 0x28CB6C0"
  "exactEntry(targets[0], kProducerEntry)"
  "exactEntry(targets[1], kTaskPrepareEntry)"
  "exactEntry(targets[2], kMapPublishEntry)"
  "exactEntry(targets[3], kMapTaskExecuteEntry)"
  "kShadowMapSlot = 5"
  "kShadowMapWidth = 8192"
  "kShadowMapHeight = 8192"
  "kShadowMapArraySize = 4"
  "DXGI_FORMAT_D16_UNORM"
  "DXGI_FORMAT_R16_TYPELESS"
  "DXGI_FORMAT_R16_UNORM"
  "D3D11_DSV_DIMENSION_TEXTURE2DARRAY"
  "D3D11_SRV_DIMENSION_TEXTURE2DARRAY"
  "CreateShaderResourceView"
  "PSGetShaderResources"
  "PSSetShaderResources"
  "ScopedFocusShadowBinding::~ScopedFocusShadowBinding")

file(READ "${VANILLA_SUN_OCCLUSION_RUNTIME_SOURCE}" sun_occlusion_runtime)
vanilla_fixes_require_text("${sun_occlusion_runtime}"
  "stereo Sun occlusion"
  "kAggregateSunOcclusionRva = 0x0281DD70"
  "kMinimumAcceptedCoveragePixels = 10"
  "kStoredSunOcclusionOffset = 0xF618"
  "kSunTestsOffset = 0xF620"
  "sizeof(SunOcclusionTest) == 0x18"
  "aggregateStereoSunOcclusion("
  "installSunOcclusionNativeHook()"
  "setSunOcclusionFixEnabled("
  "MH_CreateHook("
  "MH_EnableHook(")

file(READ "${VANILLA_RUNTIME_SOURCE}" runtime)
vanilla_fixes_require_text("${runtime}" "engine-gate"
  "0x03740E38"
  "0x037C69F8"
  "0x037C6A10"
  "0x03924D50"
  "0x03924D68"
  "0x03924EB8"
  "0x03924ED0"
  "0x039255F0"
  "0x03925608"
  "kRendererConfigRva = 0x068787F0"
  "kImageSpaceManagerPointerRva = 0x068789E8"
  "kSceneRootRegistryPointerRva = 0x06879520"
  "kSaoEffectVtableRva = 0x030B8FD8"
  "kShaderPropertyRefreshRva = 0x027F5BC0"
  "kRefreshSceneRootRva = 0x02804860"
  "saoEffectIndex = 0x47"
  "validateRipTarget"
  "writableRange"
  "InterlockedExchange8"
  "preserveStartupCapability"
  "effectivePolicy(activeSettings_)"
  "DirectionalLightDiagnosticMode::screenSpaceReflectionOnly"
  "policy.screenSpaceReflections = screenSpaceReflectionOnly"
  "policy.nativeScreenSpaceMaterialPipeline ="
  "lightingOwnershipDiagnostic"
  "settings.directionalLightDiagnosticMode =="
  "setSslrSuiteRequested("
  "F4SE::GetTaskInterface()"
  "applyScreenSpacePolicyOnMainThread()"
  "synchronizeSaoAndSslrEffect("
  "effect + 0x120"
  "sao || screenSpaceReflections"
  "refreshNativeShaderProperties()"
  "draw-boundary shader reconciliation is armed"
  "kPolicyMaintenanceInterval ="
  "std::chrono::milliseconds(250)"
  "policyMaintenanceMain("
  "shared settings exclusively own INI reloads")
foreach(forbidden IN ITEMS
    "REL::Relocation"
    "REL::ID"
    "Data/F4SE/Plugins"
    "reloadIfChanged()"
    "Vanilla Fixes reloaded the shared INI in-game.")
  string(FIND "${runtime}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Vanilla Fixes engine-gate contains forbidden '${forbidden}'")
  endif()
endforeach()

file(READ "${VANILLA_SETTINGS_STORE_SOURCE}" settings_store)
vanilla_fixes_require_text("${settings_store}" "settings"
  "kVanillaFixesSection = L\"VanillaFixes\""
  "kDiagnosticsSection = L\"Diagnostics\""
  "L\"bEnabled\""
  "L\"bPrecipitationOcclusion\""
  "L\"bAllowImageSpaceModifiers\""
  "L\"bVrAllowSAO\""
  "L\"bVrAllowScreenSpaceReflections\""
  "L\"bNativeScreenSpaceMaterialPipeline\""
  "L\"bLensFlareVr\""
  "L\"bVrAllowFocusShadows\""
  "L\"bVrSunOcclusion\""
  "L\"iDirectionalLightingMode\""
  "DirectionalLightDiagnosticMode::cubemapLookupOnly"
  "readDiagnosticMode("
  "writeDiagnosticMode("
  "settings_path::resolveIniPath()")

file(READ "${VANILLA_REFLECTION_PATCH_SOURCE}" reflection_patch)
vanilla_fixes_require_text("${reflection_patch}" "reflection transform"
  "patchStockReflectionCompositeSurfaceAnchoredCubemap"
  "patchStockReflectionCompositeRawSslr"
  "patchStockReflectionCompositeRawStockCubemap"
  "kRawSslrColor"
  "kConditionalRawSslrFinalOutput"
  "kRawCubemapFinalOutput"
  "kConditionalRawCubemapFinalOutput"
  "recomputeDxbcChecksum"
  "patchedBytecode.swap(candidate)")
file(READ "${VANILLA_REFLECTION_PATCH_TOOL_SOURCE}" reflection_patch_tool)
vanilla_fixes_require_text("${reflection_patch_tool}"
  "reflection diagnostic patch tool"
  "raw-sslr"
  "raw-stock-cubemap"
  "patchStockReflectionCompositeRawSslr"
  "patchStockReflectionCompositeRawStockCubemap")
file(READ "${VANILLA_DIRECTIONAL_DIAGNOSTIC_SURFACE_SOURCE}"
  directional_diagnostic_surface)
vanilla_fixes_require_text("${directional_diagnostic_surface}"
  "exclusive directional surface"
  "DXGI_FORMAT_R16G16B16A16_FLOAT"
  "D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE"
  "CreateTexture2D"
  "CreateRenderTargetView"
  "CreateShaderResourceView"
  "coverageDepthStencilState()"
  "coverageRasterizerState("
  "lightingOwnershipPixelShader("
  "isLightingOwnershipPixelShader("
  "csah_vanilla_lighting_ownership_diffuse_ps"
  "csah_vanilla_lighting_ownership_specular_ps"
  "csah_vanilla_lighting_ownership_black_ps"
  "DepthEnable = FALSE"
  "ScissorEnable = FALSE"
  "CullMode = D3D11_CULL_NONE"
  "ClearRenderTargetView"
  "csah_vanilla_directional_diagnostic_composite_ps"
  "compositePixelShader()"
  "isCompositePixelShader("
  "SampleDesc.Count != 1"
  "ArraySize != 1")
file(READ "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_SOURCE}"
  directional_diagnostic_composite)
vanilla_fixes_require_text("${directional_diagnostic_composite}"
  "directional diagnostic composite shader"
  "DirectionalDiagnostic : register(t5)"
  "SV_Position"
  "DirectionalDiagnostic.Load"
  "SV_Target0")
file(READ "${VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_SOURCE}"
  directional_diagnostic_coverage)
vanilla_fixes_require_text("${directional_diagnostic_coverage}"
  "directional diagnostic coverage shader"
  "SV_VertexID"
  "SV_InstanceID"
  "EYEINDEX"
  "kRectangle[6]"
  "localPosition.x * 0.5f + eyeCenter"
  "SV_Position")
file(READ "${VANILLA_LIGHTING_OWNERSHIP_DIAGNOSTIC_SOURCE}"
  lighting_ownership_diagnostic)
vanilla_fixes_require_text("${lighting_ownership_diagnostic}"
  "exclusive lighting ownership shader"
  "LIGHTING_OWNERSHIP_MODE"
  "DirectSpecular : register(t4)"
  "DirectDiffuse : register(t5)"
  "DirectDiffuse.SampleLevel("
  "DirectSpecular.SampleLevel("
  "const float3 value = 0.0f")
file(READ "${VANILLA_SSLR_ENVIRONMENT_SOURCE}" sslr_environment)
vanilla_fixes_require_text("${sslr_environment}" "SSLR environment binding"
  "kFirstResourceSlot = 4"
  "kResourceCount = 6"
  "kSamplerSlot = 4"
  "kConstantSlot = 11"
  "kCurrentHitMinimumConfidence = 0.65f"
  "tryGetSslrEnvironment"
  "PSGetShaderResources"
  "PSSetShaderResources"
  "PSGetConstantBuffers"
  "PSSetConstantBuffers"
  "appliedMatches"
  "(void)restore()"
  "setSslrConsumerEnabled")
file(READ "${VANILLA_IBL_RUNTIME_SOURCE}" ibl_runtime)
vanilla_fixes_require_text("${ibl_runtime}" "shared IBL SSLR consumer"
  "sslrConsumerEnabled_"
  "setSslrConsumerEnabled"
  "tryGetSslrEnvironment"
  "materialEnvironmentTransitionWeight_"
  "publishedEnvironmentSessionId_ == requestedSession")

file(READ "${VANILLA_D3D11_HOOK_SOURCE}" d3d11)
vanilla_fixes_require_text("${d3d11}" "D3D11 ownership"
  "hookCreateComputeShader"
  "vanilla_fixes::selectVertexShader("
  "vanilla_fixes::selectPixelShader("
  "vanilla_fixes::selectComputeShader("
  "vanilla_fixes::selectSslrPixelShaderForBinding(shader)"
  "selectSurfaceAnchoredCubemapPixelShaderForBinding(shader)"
  "selectDirectionalDiagnosticPixelShader("
  "isDirectionalDiagnosticPixelShader(shader)"
  "captureProbeBindingForShader(shader)"
  ".compositePixelShader()"
  ".isCompositePixelShader(shader)"
  "ScopedDirectionalDiagnosticOutput"
  "coverageNoDepthStencil"
  "coverageFullRaster"
  "coverageSyntheticProducer"
  "coverageSyntheticComposite"
  "finalOutputPresentation"
  "tracksOriginal(shader)"
  "ScopedDirectionalDiagnosticSyntheticGeometry"
  "originalDrawInstanced(context, 6, 2, 0, 0)"
  "RSSetViewports("
  "OMSetDepthStencilState("
  "ScopedDirectionalDiagnosticInput"
  "bound its private packed-stereo payload at t5"
  "issueDrawWithDirectionalDiagnostic("
  "reconcileDirectionalDiagnosticModeAtDraw(context)"
  "activeEnginePixelShader = shader"
  "exclusiveDirectionalDiagnostic"
  "vanilla_fixes::isSslrRaytracePixelShader(shader)"
  "vanilla_fixes::retainedStockSslrPixelShader(shader)"
  "reconcileSslrDrawShader(context, sslrEnvironment)"
  "ScopedSslrEnvironmentBinding"
  "observeFocusShadowRenderTargets(depthStencil)"
  "vanilla_fixes::isFocusShadowPixelShader(shader)"
  "ScopedFocusShadowBinding focusShadowBinding"
  "installFocusShadowNativeHooks()")
vanilla_fixes_require_text("${d3d11}" "lighting ownership D3D selection"
  "isLightingOwnershipDiagnostic("
  "lightingOwnershipPixelShader("
  "isLightingOwnershipPixelShader(shader)"
  "activeLightingOwnershipDiagnosticPixel"
  "activeLightingOwnershipReflectionDiagnosticPixel"
  "selectReflectionCompositeDiagnosticPixelShaderForBinding("
  "isReflectionCompositeDiagnosticPixelShader(shader)"
  "logLightingOwnershipDiagnosticDraw("
  "engineCaptureBinding.isDFComposite")
string(REGEX MATCHALL
  "ScopedFocusShadowBinding focusShadowBinding" focus_draw_bindings "${d3d11}")
list(LENGTH focus_draw_bindings focus_draw_binding_count)
if(NOT focus_draw_binding_count EQUAL 4)
  message(FATAL_ERROR
    "Vanilla Fixes focus resource must bind at all four draw boundaries")
endif()
string(REGEX MATCHALL
  "sslrEnvironment\\(" sslr_draw_bindings "${d3d11}")
list(LENGTH sslr_draw_bindings sslr_draw_binding_count)
if(NOT sslr_draw_binding_count EQUAL 4)
  message(FATAL_ERROR
    "Vanilla Fixes SSLR environment must bind at all four draw boundaries")
endif()
string(REGEX MATCHALL
  "issueDrawWithDirectionalDiagnostic\\(" directional_diagnostic_draws
  "${d3d11}")
list(LENGTH directional_diagnostic_draws directional_diagnostic_draw_count)
if(NOT directional_diagnostic_draw_count EQUAL 5)
  message(FATAL_ERROR
    "Exclusive directional diagnostic must own its helper and all four draw boundaries")
endif()
string(REGEX MATCHALL
  "reconcileDirectionalDiagnosticModeAtDraw\\(context\\)"
  directional_diagnostic_reconciles "${d3d11}")
list(LENGTH directional_diagnostic_reconciles
  directional_diagnostic_reconcile_count)
if(NOT directional_diagnostic_reconcile_count EQUAL 4)
  message(FATAL_ERROR
    "Exclusive directional diagnostic must reconcile retained binds at all four draw boundaries")
endif()

file(READ "${VANILLA_PALM_SETTINGS_SOURCE}" palmSettings)
vanilla_fixes_require_text("${palmSettings}" "PALM settings controls"
  "\"id\": \"vanilla-fixes\""
  "\"section\": \"VanillaFixes\""
  "\"key\": \"bEnabled\""
  "\"key\": \"bPrecipitationOcclusion\""
  "\"key\": \"bAllowImageSpaceModifiers\""
  "\"key\": \"bVrAllowSAO\""
  "\"key\": \"bVrAllowScreenSpaceReflections\""
  "\"key\": \"bNativeScreenSpaceMaterialPipeline\""
  "\"label\": \"Native Screen-Space Material Pipeline\""
  "\"key\": \"bLensFlareVr\""
  "\"key\": \"bVrAllowFocusShadows\""
  "\"key\": \"bVrSunOcclusion\""
  "\"id\": \"directional-diagnostic\""
  "\"section\": \"Diagnostics\""
  "\"key\": \"iDirectionalLightingMode\""
  "\"label\": \"Shadow Visibility (Blue)\""
  "\"label\": \"Supplied Light Vector (RGB)\""
  "\"label\": \"View Light Vector (RGB)\""
  "\"label\": \"Corrected N dot L (Red)\""
  "\"label\": \"Coverage Without Depth (RGB)\""
  "\"label\": \"Coverage Full Raster (RGB)\""
  "\"label\": \"Synthetic DFLight Coverage (RGB)\""
  "\"label\": \"Synthetic Composite Coverage (RGB)\""
  "\"label\": \"Final HDR Output (RGB)\"")
vanilla_fixes_require_text("${palmSettings}" "lighting ownership controls"
  "\"label\": \"Direct Diffuse Only\""
  "\"label\": \"Direct Specular Only\""
  "\"label\": \"Native SSR Only\""
  "\"label\": \"Vanilla Cubemap Only\"")
file(READ "${VANILLA_SHARED_SETTINGS_SOURCE}" shared_settings)
vanilla_fixes_require_text("${shared_settings}" "live settings publication"
  "vanilla_fixes::applySettings(next.vanillaFixes)")

file(READ "${VANILLA_PLUGIN_SOURCE}" plugin)
vanilla_fixes_require_text("${plugin}" "startup"
  "MH_Initialize()"
  "MH_ERROR_ALREADY_INITIALIZED"
  "Shared native-hook runtime initialized before feature ownership begins."
  "vanilla_fixes::loadSettings()"
  "vanilla_fixes::startRuntime("
  "vanilla_fixes::onGameDataReady()"
  "render::installEarlyD3D11Hooks()")
string(FIND "${plugin}" "vanilla_fixes::startRuntime(" runtime_start)
string(FIND "${plugin}" "render::installEarlyD3D11Hooks()" d3d_start)
if(runtime_start EQUAL -1 OR d3d_start EQUAL -1 OR
   NOT runtime_start LESS d3d_start)
  message(FATAL_ERROR
    "Vanilla Fixes engine-gate runtime must start before D3D interception")
endif()

vanilla_fixes_require_text("${d3d11}" "shared hook ownership"
  "status != MH_ERROR_ALREADY_INITIALIZED")
string(FIND "${d3d11}" "MH_Uninitialize()" minhook_uninitialize)
if(NOT minhook_uninitialize EQUAL -1)
  message(FATAL_ERROR
    "D3D11 rollback must not uninitialize the process-wide native-hook runtime")
endif()

file(READ "${VANILLA_IBL_GENERATOR_SOURCE}" ibl_generator)
vanilla_fixes_require_text("${ibl_generator}" "IBL composition"
  "--surface-anchor-tool"
  "raw-sslr"
  "raw-stock-cubemap"
  "validate_reflection_diagnostic_candidate"
  "len(SURFACE_ANCHORED_IDENTITIES)"
  "anchored_count"
  "9348"
  "9564"
  "11100"
  "11316")

file(READ "${VANILLA_SAO_BLUR_SOURCE}" sao_blur)
vanilla_fixes_require_text("${sao_blur}" "SAO blur source"
  "kTileOutputWidth = 960"
  "kFilterRadius = 6"
  "width >> 1u"
  "sampleX = eyeBoundary - 1"
  "sampleX = eyeBoundary")
file(READ "${VANILLA_SAO_RAW_SOURCE}" sao_raw)
vanilla_fixes_require_text("${sao_raw}" "raw SAO source"
  "eyeLocalX * 2 + 1"
  "clampInternalEyeSampleX"
  "eyeLocalUvX"
  "sampleIndex < 5"
  "motionDirection.x * 0.5f"
  "motion.x * 0.5f"
  "clampInternalEyeUv"
  "depthAgreement * motionAgreement * 0.99f")
file(READ "${VANILLA_SSLR_BLUR_SOURCE}" sslr_blur)
vanilla_fixes_require_text("${sslr_blur}" "SSLR blur source"
  "centerX >= 0.5f"
  "0.5f + halfTexel"
  "0.5f - halfTexel"
  "-3.294215f"
  "3.294215f")
file(READ "${VANILLA_SSLR_PREPASS_SOURCE}" sslr_prepass)
vanilla_fixes_require_text("${sslr_prepass}" "SSLR prepass source"
  "depth <= 0.01f"
  "input.uv.x >= 0.5f"
  "eyeMatrixOffset = rightEye ? 4u : 0u"
  "eyeLocalX ="
  "rayClipPosition"
  "geometricCandidate = cross("
  "ddx(viewPosition)"
  "ddy(viewPosition)"
  "worldNormal = transformRows(20u, normal)"
  "reflectedWorld = normalize(reflect("
  "encodeDirection(reflectedWorld)")
foreach(forbidden IN ITEMS
    "reflected.z >"
    "dot(normal, viewDirection) >= 0.0f"
    "eyeLocalEndpoint"
    "reflected * 1000.0f")
  string(FIND "${sslr_prepass}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Vanilla Fixes SSLR prepass retained stale '${forbidden}'")
  endif()
endforeach()

file(READ "${VANILLA_SSLR_RAYTRACE_SOURCE}" sslr_raytrace)
vanilla_fixes_require_text("${sslr_raytrace}" "SSLR raytrace source"
  "CameraData[77]"
  "PublishedEnvironment : register(t4)"
  "PreviousPosition : register(t9)"
  "SslrEnvironmentParameters : register(b11)"
  "DirectionTexture.Load"
  "ViewDepthTexture.Load"
  "reconstructLinearPosition"
  "viewDepth / eyeRay.z"
  "receiverWorldPosition"
  "correctProbeDirection"
  "loadWorldRadianceForReceiver"
  "maximumSteps = 32u"
  "refinementSteps = 5u"
  "maximumTravel = 1000.0f"
  "return rightEye ? uv.x > 0.5f : uv.x < 0.5f"
  "confidence >= 1.0f - 1.0e-5f"
  "confidence - EnvironmentControl.z"
  "return float4(worldRadiance, worldValidity)")

if(DEFINED VANILLA_FXC_EXECUTABLE)
  vanilla_fixes_require_file(VANILLA_FXC_EXECUTABLE)
  foreach(shader IN ITEMS
      SAO_BLUR SAO_RAW SSLR_BLUR SSLR_PREPASS SSLR_RAYTRACE
      DIRECTIONAL_DIAGNOSTIC_COMPOSITE
      DIRECTIONAL_DIAGNOSTIC_COVERAGE)
    vanilla_fixes_require_file(VANILLA_${shader}_BINARY)
  endforeach()

  set(binary_specs
    "VANILLA_SAO_BLUR_BINARY|cs_5_0|dcl_thread_group 972, 1, 1"
    "VANILLA_SAO_RAW_BINARY|cs_5_0|dcl_thread_group 16, 16, 1"
    "VANILLA_SSLR_BLUR_BINARY|vs_5_0|dcl_output o5.xy"
    "VANILLA_SSLR_PREPASS_BINARY|ps_5_0|dynamicIndexed"
    "VANILLA_SSLR_RAYTRACE_BINARY|ps_5_0|dcl_resource_texturecube"
    "VANILLA_DIRECTIONAL_DIAGNOSTIC_COMPOSITE_BINARY|ps_5_0|ld_indexable"
    "VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY|vs_5_0|dcl_input_sgv v1.x, instance_id")
  foreach(spec IN LISTS binary_specs)
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 path_variable)
    # Stage and resource bindings define this contract; no input hash gates.
    list(GET fields 1 profile_pattern)
    list(GET fields 2 contract_pattern)
    execute_process(
      COMMAND "${VANILLA_FXC_EXECUTABLE}" /dumpbin /nologo "${${path_variable}}"
      RESULT_VARIABLE dump_result
      OUTPUT_VARIABLE assembly
      ERROR_VARIABLE dump_error)
    if(NOT dump_result EQUAL 0 OR
       NOT assembly MATCHES "${profile_pattern}" OR
       NOT assembly MATCHES "${contract_pattern}")
      message(FATAL_ERROR
        "Vanilla Fixes shader contract failed for ${path_variable}: ${dump_error}")
    endif()
    if(path_variable STREQUAL "VANILLA_SSLR_RAYTRACE_BINARY")
      foreach(required_pattern IN ITEMS
          "dcl_constantbuffer CB11\\[3\\]"
          "dcl_constantbuffer CB12\\[61\\], dynamicIndexed"
          "dcl_sampler s3"
          "dcl_sampler s4"
          "dcl_resource_texture2d.* t0"
          "dcl_resource_texture2d.* t1"
          "dcl_resource_texture2d.* t2"
          "dcl_resource_texture2d.* t3"
          "dcl_resource_texturecube.* t4"
          "dcl_resource_texturecube.* t5"
          "dcl_resource_texturecube.* t6"
          "dcl_resource_texturecube.* t7"
          "dcl_resource_texturecube.* t8"
          "dcl_resource_texturecube.* t9"
          "dcl_output o0.xyzw"
          "loop")
        if(NOT assembly MATCHES "${required_pattern}")
          message(FATAL_ERROR
            "Vanilla Fixes SSLR raytrace bytecode is missing '${required_pattern}'")
        endif()
      endforeach()
      foreach(forbidden_pattern IN ITEMS
          "dcl_sampler s[0-2]"
          "dcl_sampler s[5-9]"
          "dcl_resource_[^\n]* t1[0-9]"
          "dcl_uav")
        if(assembly MATCHES "${forbidden_pattern}")
          message(FATAL_ERROR
            "Vanilla Fixes SSLR raytrace bytecode contains forbidden '${forbidden_pattern}'")
        endif()
      endforeach()
    endif()
    if(path_variable STREQUAL
        "VANILLA_DIRECTIONAL_DIAGNOSTIC_COVERAGE_BINARY")
      foreach(forbidden_pattern IN ITEMS
          "cull_distance"
          "clip_distance")
        if(assembly MATCHES "${forbidden_pattern}")
          message(FATAL_ERROR
            "Directional coverage shader retained forbidden '${forbidden_pattern}'")
        endif()
      endforeach()
    endif()
  endforeach()
endif()

message(STATUS "Verified Vanilla Fixes native, shader, settings, and PALM settings contracts")
