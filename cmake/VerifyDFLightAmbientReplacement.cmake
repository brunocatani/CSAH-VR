foreach(variable IN ITEMS
    LINEAR_LIGHTING_RUNTIME_SOURCE
    LINEAR_LIGHTING_RUNTIME_HEADER
    DFLIGHT_AMBIENT_PATCH_HEADER
    D3D11_HOOK_SOURCE
    RENDERER_HOOK_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is missing")
  endif()
endforeach()

file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" runtimeSource)
file(READ "${LINEAR_LIGHTING_RUNTIME_HEADER}" runtimeHeader)
file(READ "${DFLIGHT_AMBIENT_PATCH_HEADER}" patchHeader)
file(READ "${D3D11_HOOK_SOURCE}" d3dSource)
file(READ "${RENDERER_HOOK_SOURCE}" rendererSource)

foreach(required IN ITEMS
    "GeneratedDFLightAmbientContracts.inl"
    "patchDFLightAmbientGamma("
    "createPixelShader_("
    "dFLightAmbientOriginalBytecode_"
    "dFLightAmbientReplacementShaders_"
    "readyDFLightAmbientContractMask_"
    "dFLightAmbientDescriptorReady("
    "replacement->AddRef()"
    "ReplacementShaderFamily::dFLightAmbient"
    "ReplacementPixelConstants_None"
    "const auto nextAmbientGamma = next.ambientGamma;"
    "rebuildDFLightAmbientReplacements(nextAmbientGamma)")
  string(FIND "${runtimeSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DFLight ambient replacement regression: runtime is missing '${required}'")
  endif()
endforeach()

string(FIND "${patchHeader}" "recomputeDxbcChecksum(bytecode)" found)
if(found EQUAL -1)
  message(FATAL_ERROR
    "DFLight ambient replacement regression: patched DXBC checksum repair is missing")
endif()

foreach(required IN ITEMS
    "kDFLightAmbientShaderContractCount = 39"
    "kMaximumTrackedOriginalShadersPerContract = 8"
    "bool retainedForBind")
  string(FIND "${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DFLight ambient replacement regression: runtime interface is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "selection.retainedForBind && selection.shader"
    "selection.shader->Release()")
  string(FIND "${d3dSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DFLight ambient replacement regression: bind lifetime is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kLocalAmbientTransformAccessorRva = 0x027ADD90"
    "kFallbackAmbientTransformAccessorRva = 0x027AE6F0"
    "prepareAmbientTransform("
    "dFLightAmbientDescriptorReady("
    "directionalAmbientInputScale("
    "scaleDirectionalAmbientTransform("
    "ambientTransformPrepared.fetch_add(1, std::memory_order_release)")
  string(FIND "${rendererSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DFLight ambient replacement regression: producer is missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "hookAmbientScalarPow"
    "kAmbientPowCallsites")
  string(FIND "${runtimeSource}\n${rendererSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "DFLight ambient replacement regression: stale CPU pow path '${forbidden}'")
  endif()
endforeach()
