foreach(variable IN ITEMS
    DLAA_ENGINE_SOURCE
    DLAA_D3D_SOURCE
    DLAA_RUNTIME_SOURCE
    DLAA_FRAME_HEADER
    DLAA_STREAMLINE_SOURCE
    DLAA_SETTINGS_SOURCE
    UPSCALING_COMPOSE_SHADER_SOURCE
    UPSCALING_MOTION_SHADER_SOURCE
    UPSCALING_REACTIVE_SHADER_SOURCE
    PLUGIN_SOURCE
    RENDERER_D3D_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "DLAA verifier input '${variable}' is missing")
  endif()
endforeach()

file(READ "${DLAA_ENGINE_SOURCE}" engine)
file(READ "${DLAA_D3D_SOURCE}" d3d)
file(READ "${DLAA_RUNTIME_SOURCE}" runtime)
file(READ "${DLAA_FRAME_HEADER}" frame)
file(READ "${DLAA_STREAMLINE_SOURCE}" streamline)
file(READ "${DLAA_SETTINGS_SOURCE}" settings)
file(READ "${UPSCALING_COMPOSE_SHADER_SOURCE}" compositor)
file(READ "${UPSCALING_MOTION_SHADER_SOURCE}" motionRepair)
file(READ "${UPSCALING_REACTIVE_SHADER_SOURCE}" reactiveMask)
file(READ "${PLUGIN_SOURCE}" plugin)
file(READ "${RENDERER_D3D_SOURCE}" renderer)

foreach(required IN ITEMS
    "kPreRenderCallsiteRva = 0x0284EBC4"
    "kPreRenderTargetRva = 0x01DBA040"
    "kPostImageSpaceCallsiteRva = 0x0284E4A5"
    "kPostImageSpaceTargetRva = 0x01DBA030"
    "kDynamicResolutionManagerRva = 0x038AC010"
    "kGraphicsStateRva = 0x065A2AB0"
    "kPreRenderSignature"
    "kPostImageSpaceSignature"
    "resolveRelativeCall(preCall) != image + kPreRenderTargetRva"
    "resolveRelativeCall(postCall) !="
    "allocateReachablePage"
    "rollbackTransaction"
    "vanilla TAA remains active as the required upstream frame-preparation path"
    "reinterpret_cast<float*>(graphics + kJitterXOffset)"
    "validateEngineHooks(\"install\")")
  string(FIND "${engine}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA FO4VR engine ownership regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "hookTaaActivity"
    "shouldSuppressVanillaTaa"
    "kTaaVtableCellRva"
    "kTaaActivityTargetRva")
  string(FIND "${engine}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "DLAA FO4VR integration regression: forbidden vanilla-TAA suppression path '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS "hashMatches" "BCryptHash" "GeneratedStreamlinePayloadHashes.h")
  string(FIND "${streamline}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "DLAA input-hash gate reintroduced: '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kMapVtableIndex = 14"
    "kUnmapVtableIndex = 15"
    "addressBelongsToD3D11"
    "capturePatch(mapTarget, mapPatch)"
    "capturePatch(unmapTarget, unmapPatch)"
    "Runtime::get().onMapSucceeded"
    "Runtime::get().onBeforeUnmap"
    "resident hooks remain strict pass-through"
    "validateD3D11Hooks")
  string(FIND "${d3d}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA D3D11 ownership regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "sizeof(Fo4VrFrameBuffer)"
    "description.BindFlags & D3D11_BIND_CONSTANT_BUFFER"
    "description.ByteWidth < sizeof(Fo4VrFrameBuffer)"
    "D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT"
    "context_->PSGetConstantBuffers(12, 1, &pixelBuffer)"
    "cameraBufferIdentity_.store(candidate"
    "resource != boundCamera"
    "cameraIdentityMatches_.fetch_add"
    "inspectCamera(frame).worldValid()"
    "validation.firstPersonAvailable(0)"
    "D3D11_MAP_FLAG_DO_NOT_WAIT"
    "DLAA qualification motion readback"
    "DLAA qualification depth readback"
    "DXGI_FORMAT_R11G11B10_FLOAT"
    "DXGI_FORMAT_R8G8B8A8_UNORM"
    "DXGI_FORMAT_R16G16_FLOAT"
    "DXGI_FORMAT_R24G8_TYPELESS"
    "lastValidCameraPostCall_"
    "kDynamicResolutionWidthOffset"
    "kDynamicResolutionHeightOffset"
    "kDynamicResolutionActiveOffset"
    "kDynamicResolutionRegionOffset = 0x1683"
    "ensureEyeResources("
    "updateEvaluationGeometry()"
    "validateActiveViewport()"
    "isEvaluationViewportValid("
    "renderScaleOwned"
    "dynamicRegionClosed"
    "hardResetTemporalState("
    "applyPendingSettings()"
    "composeStereo("
    "compositionSurface_"
    "description.BindFlags = D3D11_BIND_UNORDERED_ACCESS"
    "qualifiedResources_.outputColor.Get(),"
    "compositionSurface_.Get(),"
    "GeneratedUpscalingComposeShader.h"
    "ClearUnorderedAccessViewFloat"
    "D3D11_ASYNC_GETDATA_DONOTFLUSH"
    "Upscaling performance timing over"
    "evaluateStereoFrame(postCall)"
    "ScopedImageSpaceBindings"
    "render::ScopedComputeState"
    "if (evaluated && wasOperational)"
    "releaseStreamlineDlaaResources()"
    "restoreDynamicResolutionIfOwned()"
    "void Runtime::setEnabled(bool enabled)"
    "DLAA render-thread transition applied"
    "both eye histories will reset through one private proof"
    "if (!isDlssMode(settings_.mode))"
    "computeJitterPhaseCount("
    "sampleIndex = jitterPhase_ % phaseCount + 1u"
    "prepareEyeInputs("
    "sourceLeft = settings_.mode == Mode::centerDlaa"
    ".inputLeft = 0"
    ".inputTop = 0"
    "motionOutput.Get()"
    "GeneratedUpscalingMotionRepairShader.h"
    "GeneratedUpscalingReactiveMaskShader.h"
    "imageState.renderTargetTexture(0, currentTaaMask)"
    "resolveTaaMaskView("
    "DXGI_FORMAT_R8_UNORM"
    "biasCurrentColorUav"
    "kQualificationProbeFrameCadence = 60"
    "Qualification is passive resource discovery"
    "Upscaling re-armed passive TAA resource qualification"
    "DLAA.Compare.InputPacked"
    "DLAA.Compare.VanillaPacked"
    "DLAA.Compare.OutputLeft"
    "DLAA.Compare.OutputRight"
    "DLAA.Active.InputPacked"
    "DLAA.Active.CommittedPacked"
    "DLAA.Active.OutputLeft"
    "DLAA.Active.OutputRight"
    "ownedActive = savedDynamicResolutionActive_"
    "ownedDynamicResolutionWidth_ = evaluationGeometry_.widthScale"
    "queryStreamlineOptimalSettings("
    "settings_.mode == Mode::centerDlaa"
    "cameraBufferQualified_.store(true"
    "renderResourcesQualified_.store(false"
    "operational_.store(false"
    "Vanilla TAA remains active")
  string(FIND "${runtime}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA rendering integration regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${runtime}"
  "const auto requestedNow = requested_.load" requested_state_pos)
string(FIND "${runtime}"
  "consumeQualificationProbes();" qualification_consume_pos)
string(FIND "${runtime}"
  "captureQualificationResources();" qualification_capture_pos)
string(FIND "${runtime}"
  "if (!requestedNow) {\n            return;" disabled_return_pos)
if(requested_state_pos EQUAL -1 OR qualification_consume_pos EQUAL -1 OR
   qualification_capture_pos EQUAL -1 OR disabled_return_pos EQUAL -1 OR
   NOT requested_state_pos LESS qualification_consume_pos OR
   NOT qualification_consume_pos LESS qualification_capture_pos OR
   NOT qualification_capture_pos LESS disabled_return_pos)
  message(FATAL_ERROR
    "DLAA disabled-start regression: passive qualification must complete before the disabled evaluation return")
endif()

string(FIND "${runtime}"
  "CreateUnorderedAccessView(\n                qualifiedResources_.outputColor.Get()"
  direct_output_uav)
if(NOT direct_output_uav EQUAL -1)
  message(FATAL_ERROR
    "Upscaling compositor regression: FO4VR's verified TAA output is not UAV-bindable; composition must use the private stereo surface")
endif()

foreach(required IN ITEMS
    "static_assert(sizeof(Fo4VrFrameBuffer) == 0x4D0)"
    "static_assert(offsetof(Fo4VrFrameBuffer, projection) == 0x40)"
    "static_assert(offsetof(Fo4VrFrameBuffer, inverseView) == 0x140)"
    "static_assert(offsetof(Fo4VrFrameBuffer, shaderParameters48) == 0x300)"
    "previousViewProjectionUnjittered) == 0x330)"
    "static_assert(offsetof(Fo4VrFrameBuffer, viewProjectionUnjittered) == 0x3F0)")
  string(FIND "${frame}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA FO4VR stereo frame-layout regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "verifyRuntimeFiles"
    "std::filesystem::file_size"
    "resolveCoreFunctions"
    "kProjectId"
    "615adbde-c997-4380-a9b0-73dfb99fb0b1"
    "preferences.projectId = kProjectId"
    "eUseManualHooking"
    "eUseFrameBasedResourceTagging"
    "initializationAttempted"
    "preferences.logMessageCallback = streamlineLogCallback"
    "EngineType::eCustom"
    "FO4VR-1.2.72-CS-0.2.0"
    "RenderAPI::eD3D11"
    "slUpgradeInterface"
    "slSetD3DDevice"
    "slIsFeatureSupported"
    "slGetFeatureRequirements"
    "bindDlssFunctions")
  string(FIND "${streamline}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA Streamline bootstrap regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "evaluateStreamlineDlaa("
    "queryStreamlineOptimalSettings("
    "slDLSSGetOptimalSettings"
    "state.getNewFrameToken"
    "sl::ViewportHandle viewport(eye)"
    "sl::DLSSMode::eDLAA"
    "sl::DLSSMode::eMaxQuality"
    "sl::DLSSMode::eBalanced"
    "sl::DLSSMode::eMaxPerformance"
    "sl::DLSSMode::eUltraPerformance"
    "sl::DLSSPreset::ePresetJ"
    "sl::DLSSPreset::ePresetK"
    "sl::DLSSPreset::ePresetL"
    "sl::DLSSPreset::ePresetM"
    "options.colorBuffersHDR = sl::Boolean::eFalse"
    "options.useAutoExposure = isDlssMode(frame.mode) ?"
    "options.alphaUpscalingEnabled = sl::Boolean::eFalse"
    "constants.depthInverted = source.depthInverted ?"
    "const auto optionsChanged = !optionsKey.valid"
    "source.inputLeft"
    "state.setConstants"
    "state.setTagForFrame"
    "sl::ResourceLifecycle::eValidUntilEvaluate"
    "state.evaluateFeature"
    "sl::kBufferTypeScalingInputColor"
    "sl::kBufferTypeScalingOutputColor"
    "sl::kBufferTypeDepth"
    "sl::kBufferTypeMotionVectors"
    "sl::kBufferTypeBiasCurrentColorHint"
    "releaseStreamlineDlaaResources"
    "state.freeResources")
  string(FIND "${streamline}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA Streamline stereo-evaluation regression: missing '${required}'")
  endif()
endforeach()

string(FIND "${streamline}"
  "options.useAutoExposure = sl::Boolean::eFalse;" disabled_auto_exposure)
if(NOT disabled_auto_exposure EQUAL -1)
  message(FATAL_ERROR
    "DLSS exposure regression: auto exposure was disabled without a tagged exposure texture")
endif()



foreach(required IN ITEMS
    "bEnabled"
    "iMode"
    "iModelPreset"
    "bPostSharpening"
    "bMotionVectorRepair"
    "fSharpness"
    "fCenterWidth"
    "fCenterHeight"
    "fCenterFeatherPixels"
    "bVisualizeCenter"
    "bHardResetOnLoad"
    "bVerboseDiagnostics")
  string(FIND "${settings}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA persisted-settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float4> TaaMask"
    "RWTexture2D<unorm float> BiasCurrentColor"
    "destination + SourceOrigin"
    "taaHistoryMask * 0.1")
  string(FIND "${reactiveMask}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Upscaling reactive-mask regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float2> MotionInput"
    "Texture2D<float> DepthInput"
    "Texture2D<float2> MotionHistory"
    "RWTexture2D<float2> MotionOutput"
    "LinearDepth(float depth)"
    "centerLinearDepth > FarDepthStart"
    "neighborLinearDepth >= centerLinearDepth"
    "MotionHistory.Load"
    "ResetHistory == 0")
  string(FIND "${motionRepair}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Upscaling motion-vector repair regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "Texture2D<float4> NeuralLeft"
    "Texture2D<float4> NeuralRight"
    "RWTexture2D<unorm float4> CompositionSurface"
    "RegionDimensions.x * 2"
    "eye * EyeStride + DestinationLeft"
    "smoothstep(0.0, FeatherPixels, edgeDistance)"
    "const float peak = -rcp(lerp(8.0, 5.0, saturate(Sharpness)))"
    "amplitude = sqrt(amplitude)"
    "lerp(vanilla.rgb, neural, blend)"
    "vanilla.a")
  string(FIND "${compositor}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Upscaling stereo compositor regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "csah::dlaa::loadSettings()"
    "csah::dlaa::Runtime::get().applySettings"
    "csah::dlaa::installEngineHooks()"
    "csah::dlaa::validateEngineHooks("
    "csah::dlaa::validateD3D11Hooks("
    "beginQualificationSession(")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA loader/lifecycle regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "dlaa::initializeStreamlineBeforeDevice()"
    "dlaa::bindStreamlineDeviceAndSwapChain("
    "dlaa::installD3D11Hooks(context)"
    "dlaa::Runtime::get().onDeviceCreated")
  string(FIND "${renderer}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "DLAA renderer integration regression: missing '${required}'")
  endif()
endforeach()


string(FIND "${renderer}" "HRESULT WINAPI hookCreateDeviceAndSwapChain(" hook_start)
string(FIND "${renderer}" "dlaa::initializeStreamlineBeforeDevice()" init_pos)
string(FIND "${renderer}" "const auto result = originalCreateDeviceAndSwapChain(" create_pos)
string(FIND "${renderer}" "bool installEarlyD3D11Hooks()" early_start)
if(hook_start EQUAL -1 OR init_pos EQUAL -1 OR create_pos EQUAL -1 OR
   early_start EQUAL -1 OR NOT init_pos GREATER hook_start OR
   NOT init_pos LESS create_pos OR NOT create_pos LESS early_start)
  message(FATAL_ERROR
    "DLAA Streamline initialization must run inside the renderer interception boundary before native D3D11 creation, never inside F4SEPlugin_Load")
endif()

string(FIND "${renderer}" "dlaa::bindStreamlineDeviceAndSwapChain(" bind_pos)
string(FIND "${renderer}" "installDeviceMethodDetours(*device" methods_pos)
if(bind_pos EQUAL -1 OR methods_pos EQUAL -1 OR
   NOT create_pos LESS bind_pos OR NOT bind_pos LESS methods_pos)
  message(FATAL_ERROR
    "DLAA Streamline device/swapchain binding must precede renderer feature initialization")
endif()

set(all_dlaa "${engine}\n${d3d}\n${runtime}\n${frame}\n${streamline}\n${settings}")
foreach(forbidden IN ITEMS
    "REL::"
    "RE::BSGraphics"
    "RenderTargetManager"
    "0xF88"
    "0xF8C"
    "0xFA8"
    "RelocationID(524768"
    "411384"
    "eDisableCLStateTracking")
  string(FIND "${all_dlaa}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "DLAA regression: forbidden flat/Open-Shaders contract '${forbidden}'")
  endif()
endforeach()
