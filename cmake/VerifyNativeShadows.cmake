foreach(variable IN ITEMS
    NATIVE_SHADOW_RUNTIME_SOURCE
    NATIVE_SHADOW_RUNTIME_HEADER
    NATIVE_SHADOW_SETTINGS_HEADER
    NATIVE_SHADOW_SETTINGS_STORE_SOURCE
    NATIVE_SHADOW_PATCH_MODEL
    PLUGIN_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "Native Shadows verifier input '${variable}' is missing")
  endif()
endforeach()

file(READ "${NATIVE_SHADOW_RUNTIME_SOURCE}" runtime)
file(READ "${NATIVE_SHADOW_RUNTIME_HEADER}" runtimeHeader)
file(READ "${NATIVE_SHADOW_SETTINGS_HEADER}" settings)
file(READ "${NATIVE_SHADOW_SETTINGS_STORE_SOURCE}" store)
file(READ "${NATIVE_SHADOW_PATCH_MODEL}" model)
file(READ "${PLUGIN_SOURCE}" plugin)

foreach(required IN ITEMS
    "kCascadeCountRva = 0x03924818"
    "kCascadeDistanceRva = 0x03924808"
    "kRendererDistanceRva = 0x068788F0"
    "kShadowResolutionRva = 0x039266F0"
    "kShadowRendererStateRva = 0x068787F0"
    "kCachedCascadeBlendOffset = 0x114"
    "kCachedOrthoShadowFilterOffset = 0x190"
    "0x027E929A"
    "0x0290DC03"
    "0x83, 0x3D, 0x0E, 0x6C, 0x01, 0x01, 0x02"
    "0x83, 0x3D, 0x0E, 0x6C, 0x01, 0x01, 0x04"
    "0x028A57A0"
    "0x028A5C3C"
    "0x02889ACF"
    "0x02889B62"
    "0x028A5A86"
    "0x028A5BCC"
    "0x027EE5CC"
    "kZeroInitRva = 0x027A52A0"
    "kNullSafetyRva = 0x0281377F"
    "kNodeAllocatorRva = 0x0278E610"
    "kPointerValidationRva = 0x027A49DA"
    "kVrArrayRva = 0x06878B18"
    "kRenderSceneNodeRva = 0x06879520"
    "kSetupSceneNodeRva = 0x06885D40")
  string(FIND "${model}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Native Shadows FO4VR patch-model regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "validatePatch(base, patch)"
    "buildMovImmediatePatch("
    "installSafetyCaves(base, safetyOwned)"
    "verifiedNodeAllocatorPatchPrefix("
    "verifiedNodeAllocatorPatchLocked("
    "g_state.nodeAllocatorCave"
    "kNodeAllocatorClearNext"
    "relativeJumpTarget("
    "allocateReachablePage("
    "PAGE_EXECUTE_READ"
    "capacity < patch_model::kExtendedCascadeCount"
    "will not replace an engine-owned buffer"
    "safe two-cascade masks remain active"
    "all four cascades render every frame"
    "no FPS controller or adaptive-quality path exists"
    "forceFixedShadowQuality(moduleBase())"
    "cached cascade blend distance"
    "cached orthographic shadow filter"
    "REL::Module::IsVR()"
    "F4SE::RUNTIME_VR_1_2_72")
  string(FIND "${runtime}\n${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Native Shadows runtime-safety regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "ExtendedDirectionalCascades"
    "TiledDeferredLighting"
    "DirectionalShadowDistance"
    "CascadeBlendDistance"
    "OrthographicShadowFilter"
    "std::clamp(result.directionalShadowDistance, 3000.0f, 50000.0f)")
  string(FIND "${settings}${store}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Native Shadows fixed-settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "native_shadows::startRuntime("
    "native_shadows::onGameDataReady()"
    "native_shadows::onWorldReady(\"PostLoadGame\")"
    "native_shadows::onWorldReady(\"NewGame\")")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Native Shadows deterministic-lifecycle regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "CreateTimerQueueTimer"
    "SetTimer"
    "fFpsTarget"
    "bAutoAdjust"
    "targetFps"
    "frameTime"
    "SharedShadow"
    "0x0290D9D0"
    "0x0290D9D9"
    "0x0281BE1C")
  string(FIND "${runtime}${settings}${store}${model}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Native Shadows forbidden adaptive or disproven patch path returned: '${forbidden}'")
  endif()
endforeach()

message(STATUS
  "Verified fixed Native Shadows contracts: four cascades, tiled lighting, fixed distance and close-shadow quality, deterministic lifecycle, no FPS controller, and no disproven stereo patches")
