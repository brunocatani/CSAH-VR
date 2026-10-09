foreach(variable IN ITEMS
    CONTACT_SHADOW_RUNTIME_SOURCE
    CONTACT_SHADOW_RUNTIME_HEADER
    CONTACT_SHADOW_SETTINGS_HEADER
    CONTACT_SHADOW_SETTINGS_STORE_SOURCE
    CONTACT_SHADOW_SETTINGS_STORE_HEADER
    CONTACT_SHADOW_SHADER_SOURCE
    CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE
    CONTACT_SHADOW_MASK_SHADER_SOURCE
    CONTACT_SHADOW_DISPATCH_SHADER_SOURCE
    CONTACT_SHADOW_RESOLVE_SHADER_SOURCE
    CONTACT_SHADOW_SHADER_GENERATOR
    D3D11_HOOK_SOURCE
    PALM_SETTINGS_SOURCE
    PLUGIN_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${CONTACT_SHADOW_RUNTIME_SOURCE}" runtimeSource)
file(READ "${CONTACT_SHADOW_RUNTIME_HEADER}" runtimeHeader)
file(READ "${CONTACT_SHADOW_SETTINGS_HEADER}" settingsHeader)
file(READ "${CONTACT_SHADOW_SETTINGS_STORE_SOURCE}" settingsStoreSource)
file(READ "${CONTACT_SHADOW_SETTINGS_STORE_HEADER}" settingsStoreHeader)
file(READ "${CONTACT_SHADOW_SHADER_SOURCE}" shaderSource)
file(READ "${CONTACT_SHADOW_DIAGNOSTIC_SHADER_SOURCE}"
  diagnosticShaderSource)
file(READ "${CONTACT_SHADOW_MASK_SHADER_SOURCE}" maskShaderSource)
file(READ "${CONTACT_SHADOW_DISPATCH_SHADER_SOURCE}" dispatchShaderSource)
file(READ "${CONTACT_SHADOW_RESOLVE_SHADER_SOURCE}" resolveShaderSource)
file(READ "${CONTACT_SHADOW_SHADER_GENERATOR}" generatorSource)
file(READ "${D3D11_HOOK_SOURCE}" hookSource)
file(READ "${PALM_SETTINGS_SOURCE}" palmSettings)
file(READ "${PLUGIN_SOURCE}" pluginSource)

foreach(required IN ITEMS
    "kConstantSlot = 13"
    "kMaskSlot = 46"
    "matchingContractIndex"
    "csah_contact_shadow_dflight_contracts"
    "kMaximumShaderContracts = 32"
    "kMaximumTrackedShaders = 128"
    "resourcesReady_.store(false"
    "resourcesReady_.store(true"
    "PSGetConstantBuffers("
    "PSSetConstantBuffers(kConstantSlot"
    "PSGetShaderResources("
    "PSSetShaderResources(kMaskSlot"
    "dispatchMask"
    "dispatchCompute_"
    "resolveCompute_"
    "dispatchRecords_"
    "dispatchRecordsView_"
    "dispatchRecordsOutput_"
    "dispatchArguments_"
    "dispatchArgumentsOutput_"
    "DispatchIndirect"
    "kDispatchRecordCount = 16"
    "uploadedGpuSettings_"
    "rawMaskTexture_"
    "rawMaskView_"
    "rawMaskOutput_"
    "csah_contact_shadow_resolve"
    "shaderResourceCount = 3"
    "ClearUnorderedAccessViewFloat"
    "const std::array<ID3D11ShaderResourceView*, 3> resolveInputs"
    "ScopedComputeState restore"
    "recordDrawFallback"
    "selectPixelShader"
    "selectDirectionalDiagnosticPixelShader"
    "isDirectionalDiagnosticPixelShader"
    "retainedOriginalDirectionalDiagnosticPixelShader"
    "tracksOriginal"
    "for (auto& replacement : replacements_)"
    "directionalDiagnostics_"
    "diagnosticBytecodeLength"
    "diagnosticMode > 11"
    "shaderMode = diagnosticMode <= 6 ? diagnosticMode : 4"
    "Exclusive directional diagnostic selected live DFLight contract"
    "for (auto& original : originals_)"
    "original.shader.Reset()")
  string(FIND "${runtimeSource}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows runtime regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "TextureCube<float> CloudOcclusion"
    "CloudVisibility(")
  string(FIND "${maskShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows raw mask must not consume cloud visibility '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> SceneDepth : register(t0)"
    "Texture2D<float> RawContactShadow : register(t1)"
    "TextureCube<float> CloudOcclusion : register(t2)"
    "RWTexture2D<unorm float> ResolvedShadowMask : register(u0)"
    "SamplerState CloudSampler : register(s0)"
    "NativeDFLight : register(b2)"
    "NativeStereo : register(b8)"
    "NativeCamera : register(b12)"
    "ContactShadowSettings : register(b13)"
    "const float rawVisibility"
    "const float viewDepth = abs(surface.z)"
    "1.0f - smoothstep(0.0f, fadeDistance, viewDepth)"
    "saturate(ContactParams0.x) * distanceScale"
    "contactVisibility * cloudVisibility"
    "[numthreads(8, 8, 1)]")
  string(FIND "${resolveShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows wavefront resolve regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "LoadSameReceiverShadow"
    "DirectionalClosure"
    "searchIndex")
  string(FIND "${resolveShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows resolve restored obsolete silhouette closure '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS "RE::" "REL::" "GetRendererData")
  string(FIND "${runtimeSource}${runtimeHeader}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows runtime contains unverified engine access '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> ContactShadowMask : register(t46)"
    "ContactShadowSettings : register(b13)"
    "ContactShadowMask.Load"
    "lerp("
    "rawVisibility")
  string(FIND "${shaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows stereo shader regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "NativeDFLight : register(b2)"
    "NativeCamera : register(b12)"
    "uint eye : EYEINDEX"
    "float3 decodedNormal : TEXCOORD0"
    "DIRECTIONAL_DIAGNOSTIC_MODE == 4"
    "DIRECTIONAL_DIAGNOSTIC_MODE == 5"
    "DIRECTIONAL_DIAGNOSTIC_MODE == 6"
    "Camera[0].xyz"
    "Camera[1].xyz"
    "Camera[2].xyz"
    "WorldToViewDirection"
    "correctedNdotL")
  string(FIND "${diagnosticShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Directional-light diagnostic shader regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> SceneDepth : register(t0)"
    "StructuredBuffer<DispatchRecord> DispatchRecords : register(t1)"
    "RWTexture2D<unorm float> ContactShadowMask : register(u0)"
    "ContactShadowSettings : register(b13)"
    "NativeDFLight : register(b2)"
    "NativeStereo : register(b8)"
    "NativeCamera : register(b12)"
    "static const uint kWaveSize = 64u"
    "static const uint kMaximumSampleCount = 256u"
    "static const float kFarDepthValue = 1.0f"
    "static const float kNearDepthValue = 0.0f"
    "groupshared float SharedDepth"
    "groupshared uint SharedDepthDomain"
    "ComputeWavefrontExtents"
    "LoadNativeDepth"
    "ReconstructViewPosition"
    "ProjectViewPosition"
    "rawDepth * 100.0f"
    "rawDepth * 1.01f - 0.01f"
    "neighborDomain != baseDomain"
    "abs(kFarDepthValue - baseDepth)"
    "GroupMemoryBarrierWithGroupSync"
    "const float surfaceThickness = max(ContactParams0.z"
    "activeSampleCount = min"
    "const float2 rayPixelDelta"
    "const uint requiredPixelReach"
    "const uint qualityPixelBudget"
    "clamp(ContactParams0.w, 2.0f, 8.0f)"
    "SharedDepthDomain[sharedIndex] != sampleDomain[0]"
    "shadowValue = saturate(shadowValue * 4.0f - 3.0f)"
    "const float visibility = dot(shadowValue, 0.25f)"
    "record.eye * eyeWidth"
    "[numthreads(64, 1, 1)]")
  string(FIND "${maskShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows Bend mask regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float> SceneDepth : register(t0)"
    "NativeDFLight : register(b2)"
    "NativeCamera : register(b12)"
    "RWStructuredBuffer<DispatchRecord> DispatchRecords : register(u0)"
    "RWByteAddressBuffer DispatchArguments : register(u1)"
    "ProjectViewDirection"
    "BuildEyeDispatches"
    "const uint kWaveSize = 64u"
    "const uint kDispatchesPerEye = 8u"
    "DispatchArguments.Store"
    "BuildEyeDispatches(0u, viewportSize)"
    "BuildEyeDispatches(1u, viewportSize)"
    "[numthreads(1, 1, 1)]")
  string(FIND "${dispatchShaderSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows GPU dispatch regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "length(surface)"
    "-normalize(DFLight[eye + 1u].xyz)"
    "sampleCount = max((uint)scaled"
    "clamp(ContactParams0.w, 2.0f, 16.0f) * distanceScale"
    "1.0f - separation / thickness"
    "hit * sampleWeight"
    "sampleBudget"
    "LoadCompatibleViewDepth"
    "SampleEdgeAwareViewDepth"
    "float4 laneOcclusion"
    "supportConfidence"
    "intervalCoverage"
    "sqrt(intervalCoverage)"
    "StableRayStride"
    "sampleStride"
    "(index * sampleStride) % sampleCount"
    "ContactParams1.x * 0.25f"
    "viewDepth * 5.0e-4f"
    "DirectionalClosure")
  string(FIND "${maskShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows mask shader restored unstable tap movement '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS "PixelNoise" "discard")
  string(FIND "${shaderSource}${maskShaderSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows stereo shader contains forbidden '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "EXPECTED_COMPATIBLE_SHADER_COUNT = 22"
    "EXPECTED_ALIAS_KEYS = {0x01200202, 0x01200282, 0x11200202}"
    "directional_pixel_shaders"
    "structurally compatible directional coverage changed"
    "write_shader_family_header"
    "CONTACT_MASK_SLOT = 46"
    "contact-shadow template contains an early return"
    "contains an injected early return"
    "contact-shadow mask compute assembly changed"
    "compile_resolve_shader"
    "compile_dispatch_shader"
    "csah_contact_shadow_dispatch"
    "contact-shadow resolve assembly changed"
    "csah_contact_shadow_resolve"
    "multiply_rgb(1, visibility_scratch)"
    "multiply_rgb(0, visibility_scratch)"
    "STOCK_DIRECTIONAL_FINAL_OUTPUTS"
    "DIRECTIONAL_DIAGNOSTIC_MODE_COUNT = 6"
    "directional_diagnostic_outputs"
    "compile_directional_diagnostic_templates"
    "directional_diagnostic_template_contract"
    "remap_directional_diagnostic_instruction"
    "patch_directional_diagnostic"
    "diagnosticBytecode"
    "std::array<const unsigned char*, 6> diagnosticBytecode"
    "directional diagnostic candidate {index}/{mode_index} validation")
  string(FIND "${generatorSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows generation regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "contact_shadows::Runtime::get().onDeviceCreated"
    "contact_shadows::Runtime::get().onPixelShaderCreated"
    "selectDirectionalDiagnosticPixelShader("
    "isDirectionalDiagnosticPixelShader(shader)"
    "contactShadowRuntime.selectPixelShader("
    "contactShadowRuntime.tracksOriginal(shader)"
    "firstTrackedContactShaderBindLogged"
    "wrappedGrassFeatureActive"
    "pbr::Runtime::get().onDeviceCreated"
    "pbrFeatureActive"
    "contactShadowRuntime.compositorReady("
    "if (materialCompositorActive && classInstanceCount == 0)"
    "issueDrawWithContactShadows(context"
    "activeContactShadowsEnabled"
    "wrappedRuntime.scopeDraw("
    "pbrRuntime.scopeDraw("
    "activePbrEnabled"
    "activeContactShadowBinding.original")
  string(FIND "${hookSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows D3D ownership regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${hookSource}"
  "dflightCompositorActive && !qualificationActive" qualificationSuppression)
if(NOT qualificationSuppression EQUAL -1)
  message(FATAL_ERROR
    "Contact Shadows exact compositor must not be suppressed during material qualification")
endif()

string(FIND "${hookSource}"
  "void STDMETHODCALLTYPE hookPSSetShader(" psSetShaderStart)
string(FIND "${hookSource}"
  "void STDMETHODCALLTYPE hookDrawIndexed(" psSetShaderEnd)
if(psSetShaderStart EQUAL -1 OR psSetShaderEnd EQUAL -1 OR
   NOT psSetShaderStart LESS psSetShaderEnd)
  message(FATAL_ERROR
    "Contact Shadows PSSetShader ownership boundary is unavailable")
endif()
math(EXPR psSetShaderLength "${psSetShaderEnd} - ${psSetShaderStart}")
string(SUBSTRING "${hookSource}" ${psSetShaderStart}
  ${psSetShaderLength} psSetShaderSource)
string(FIND "${psSetShaderSource}"
  "auto contactShadowSelection =" contactSelectionStart)
string(FIND "${psSetShaderSource}"
  "const auto selection =" linearSelectionStart)
string(FIND "${psSetShaderSource}"
  "!contactShadowSelection.binding &&" contactPriorityGate)
if(contactSelectionStart EQUAL -1 OR linearSelectionStart EQUAL -1 OR
   contactPriorityGate EQUAL -1 OR
   NOT contactSelectionStart LESS linearSelectionStart)
  message(FATAL_ERROR
    "Contact Shadows exact compositor must take priority over generic Linear Lighting selection")
endif()

string(REGEX MATCHALL
  "issueDrawWithContactShadows\\(context" drawScopes
  "${hookSource}")
list(LENGTH drawScopes drawScopeCount)
if(NOT drawScopeCount EQUAL 4)
  message(FATAL_ERROR
    "Contact Shadows must own one fail-closed scope in all four draw paths")
endif()

foreach(required IN ITEMS
    "bEnabled"
    "bFoveated"
    "fStrength"
    "fMaxDistance"
    "fFadeDistance"
    "fThickness"
    "iSampleCount"
    "parseBoolean")
  string(FIND
    "${settingsHeader}${settingsStoreSource}${settingsStoreHeader}"
    "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "\"id\": \"contact-shadows\""
    "\"id\": \"contact-foveated\""
    "\"section\": \"ContactShadows\""
    "\"key\": \"bEnabled\""
    "\"key\": \"bFoveated\""
    "\"key\": \"fStrength\""
    "\"key\": \"fMaxDistance\""
    "\"key\": \"fFadeDistance\""
    "\"key\": \"fThickness\""
    "\"key\": \"iSampleCount\"")
  string(FIND "${palmSettings}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows PALM settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "contact_shadows::loadSettings"
    "contact_shadows::Runtime::get().applySettings")
  string(FIND "${pluginSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Contact Shadows bootstrap regression: missing '${required}'")
  endif()
endforeach()
