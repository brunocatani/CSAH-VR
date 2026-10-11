foreach(variable IN ITEMS
    SKYLIGHTING_RUNTIME_SOURCE
    SKYLIGHTING_RUNTIME_HEADER
    SKYLIGHTING_NATIVE_HOOK_SOURCE
    SKYLIGHTING_SETTINGS_HEADER
    SKYLIGHTING_SETTINGS_STORE_SOURCE
    SKYLIGHTING_AMBIENT_SHADER_SOURCE
    SKYLIGHTING_COMPUTE_SHADER_SOURCE
    SKYLIGHTING_SHADER_GENERATOR
    SKYLIGHTING_AMBIENT_HEADER
    D3D11_HOOK_SOURCE
    LINEAR_LIGHTING_RUNTIME_SOURCE
    SHARED_SETTINGS_SOURCE
    SHARED_SETTINGS_HEADER
    PLUGIN_SOURCE
    PALM_SETTINGS_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "Skylighting verification input is missing: ${variable}")
  endif()
endforeach()

file(READ "${SKYLIGHTING_RUNTIME_SOURCE}" runtime)
file(READ "${SKYLIGHTING_RUNTIME_HEADER}" runtimeHeader)
get_filename_component(skylightingRuntimeDirectory "${SKYLIGHTING_RUNTIME_HEADER}" DIRECTORY)
file(READ "${skylightingRuntimeDirectory}/SkylightingDepthRead.h" depthReadHeader)
string(APPEND runtimeHeader "\n${depthReadHeader}")
file(READ "${SKYLIGHTING_NATIVE_HOOK_SOURCE}" nativeHook)
file(READ "${SKYLIGHTING_SETTINGS_HEADER}" settingsHeader)
file(READ "${SKYLIGHTING_SETTINGS_STORE_SOURCE}" settingsStore)
file(READ "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}" ambientShader)
get_filename_component(skylightingShaderDirectory "${SKYLIGHTING_AMBIENT_SHADER_SOURCE}" DIRECTORY)
file(READ "${skylightingShaderDirectory}/SkylightingSampling.hlsli" samplingShader)
file(READ "${skylightingShaderDirectory}/SkylightingReceiver.hlsli" receiverShader)
string(APPEND ambientShader "\n${samplingShader}\n${receiverShader}")
file(READ "${SKYLIGHTING_COMPUTE_SHADER_SOURCE}" computeShader)
file(READ "${SKYLIGHTING_SHADER_GENERATOR}" generator)
file(READ "${SKYLIGHTING_AMBIENT_HEADER}" ambientHeader)
file(READ "${D3D11_HOOK_SOURCE}" d3dHook)
file(READ "${LINEAR_LIGHTING_RUNTIME_SOURCE}" linearRuntime)
file(READ "${SHARED_SETTINGS_SOURCE}" sharedSettings)
file(READ "${SHARED_SETTINGS_HEADER}" sharedSettingsHeader)
file(READ "${PLUGIN_SOURCE}" plugin)
file(READ "${PALM_SETTINGS_SOURCE}" palmSettings)

foreach(required IN ITEMS
    "kWrapperRva = 0x00634300"
    "kRenderRva = 0x006350C0"
    "kProjectionRva = 0x00635530"
    "kSetViewFrustumVrCallsiteRva = 0x00635A76"
    "kSetViewFrustumVrRva = 0x01C2BFA0"
    "kDepthTargetMapperRva = 0x01DB9E40"
    "kRendererStateRva = 0x038AC010"
    "kCubeSizeRva = 0x05A3CFA4"
    "kDirectionRva = 0x05A3CFC8"
    "kGpuCullingEnabledRva = 0x027E0D50"
    "kRenderDepthTargetSetupOffset = 0x1CC"
    "kRenderGpuCullingQueryOffset = 0x212"
    "kRenderGpuCullingReturnOffset = 0x217"
    "kDepthTargetMapperSignatureOffset = 0x1A"
    "kDepthTargetMapOffset = 0x15FC"
    "kWrapperFirstCallTargetRva = 0x0012FB50"
    "kPrecipitationManagerOffset = 0xA0"
    "kPass14ResolverRva = 0x0281CB50"
    "kAccumulatorPassCollectorRva = 0x0281E760"
    "kLightingPrecipitationBuilderRva ="
    "0x027A48B0"
    "kLightingPrecipitationBuilderSlotOffset ="
    "0x170"
    "kSpecialGeometryNiRttiRva = 0x0689B458"
    "kLightingPassListResolverRva = 0x027A51E0"
    "kAccumulatorPassIndexOffset = 0xF6B0"
    "kAccumulatorPassKeyOffset = 0xF6B8"
    "kAccumulatorPassListCount = 4"
    "kPassListClearRva = 0x0278E3E0"
    "kPassListEmplaceRva = 0x0278E610"
    "kUtilityShaderSingletonRva = 0x0689B4F0"
    "kUtilityShaderSecondaryVtableRva"
    "kUtilityShaderSecondaryVtableOffset = 0x10"
    "kUtilityShaderKindOffset = 0x18"
    "kUtilityShaderKind = 1"
    "kUtilityRenderDepthDescriptor = 1u << 13"
    "kUtilityTreeAnimDescriptor = 1u << 26"
    "kUtilityDepthPassCategory = 0x1E"
    "kExcludedBsxFlags = 0x3D54"
    "std::array<std::byte, 6> kWrapperSignature"
    "std::array<std::byte, 27> kRenderDepthTargetSetupSignature"
    "kSetViewFrustumVrSignature"
    "std::array<std::byte, 17> kDepthTargetMapperSignature"
    "kGpuCullingEnabledSignature"
    "kPass14ResolverSignature"
    "kAccumulatorPassCollectorSignature"
    "kLightingPrecipitationBuilderSignature"
    "kLightingPassListResolverSignature"
    "kPassListClearSignature"
    "kPassListEmplaceSignature"
    "Features/native_shadows/NativeShadowRuntime.h"
    "native_shadows::verifiedNodeAllocatorPatchPrefix(passListEmplace)"
    "passListEmplace + nativeShadowPrefix"
    "kPassListEmplaceSignature.data() + nativeShadowPrefix"
    "std::byte{ 0x40 }, std::byte{ 0x53 }"
    "wrapper + kWrapperSignature.size()"
    "ripRelativeTarget(renderDepthTargetSetup + 3)"
    "relativeTarget(renderDepthTargetSetup + 22)"
    "resolveNativePrecipitationManager()"
    "nativeSkySingleton()"
    "MH_CreateHook("
    "ReadPointerAcquire("
    "hookGpuCullingEnabled"
    "hookSetViewFrustumVr"
    "activeCaptureQuadrant"
    "createAbsoluteJumpThunk("
    "captureDirectCallIdentity("
    "installedSetViewFrustumVrCallIdentity"
    "preexistingSetViewFrustumCall.destination"
    "_ReturnAddress()"
    "hookPass14Resolver"
    "usesLightingPrecipitationBuilder("
    "buildOcclusionPasses("
    "collectAccumulatorPass("
    "inspectUtilityShader("
    "std::atomic_bool passProductionActive"
    "passProductionActive.compare_exchange_strong("
    "captureDetourIdentity("
    "Runtime::get().setNativeHookOwned(true)")
  string(FIND "${nativeHook}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting native identity contract is missing '${required}'")
  endif()
endforeach()

string(FIND "${nativeHook}" "thread_local" skylightingThreadLocal)
if(NOT skylightingThreadLocal EQUAL -1)
  message(FATAL_ERROR
    "Skylighting capture scope must be visible to FO4VR render workers")
endif()

foreach(forbidden IN ITEMS
    "kOcclusionPassListOffset"
    "kLightingPropertyVtableRva"
    "kLightingPassBuilderRva"
    "kLightingPassBuilderSlot"
    "lightingPassBuilderCell"
    "patchPointerCell("
    "hookLightingPassBuilder"
    "setViewFrustumVrTrampoline"
    "installedSetViewFrustumVrIdentity"
    "kLightingPassBuilderRva = 0x027A3250"
    "kLightingPassBuilderSlot = 0x2D")
  string(FIND "${nativeHook}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting retained disproven pass-dispatch contract '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "return { 64, 64, 32 }"
    "return { 128, 128, 64 }"
    "return { 256, 256, 128 }"
    "Runtime::Dimensions Runtime::farDimensionsFor() noexcept"
    "return { 32, 32, 60 }"
    "kFarCaptureDistance = 32768.0f"
    "kFarCaptureInterval = 8"
    "farProbes_"
    "farLevel"
    "updateSliceCursor"
    "captureQuadrant"
    "ScopedOcclusionPassProduction passProduction("
    "(targetLevel.captureQuadrant + 1u) % 4u"
    "DXGI_FORMAT_R16G16B16A16_FLOAT"
    "DXGI_FORMAT_R8_UINT"
    "ScopedComputeState"
    "kNativePrecipitationDepthTarget = 9"
    "kRendererStateRva = 0x038AC010"
    "kDepthTargetMapOffset = 0x15FC"
    "kFo4VrDepthStencilTargetsOffset = 0x2588"
    "kFo4VrDepthStencilTargetCount = 18"
    "sizeof(Fo4VrDepthStencilTarget) == 0x98"
    "ScopedNativeDepthTarget"
    "ScopedOcclusionPassProduction"
    "D3D11_COMPARISON_LESS_EQUAL"
    "samplerDescription.ComparisonFunc = kOcclusionComparison"
    "ScopedDepthRead depthRead"
    "D3D11_KEEP_UNORDERED_ACCESS_VIEWS"
    "boundDepth.Get()!=resource"
    "kPrivateDepthClearValue = 1.0f"
    "D3D11_CLEAR_DEPTH,"
    "kPrivateDepthClearValue,"
    "without hot-path diagnostics"
    "RendererData::GetSingleton()"
    "InterlockedIncrement("
    "submitAmbientDiagnostic()"
    "consumeAmbientDiagnosticReadback()"
    "Skylighting ambient-consumer trace:"
    "observePrivateCaptureDraw("
    "Skylighting first private draw state:"
    "nativeOutput_.data() + kNativeProjectionOffset"
    "previousProbes_.size()"
    "PSSetConstantBuffers(13"
    "probeDataValid_.store("
    "playerCell->IsExterior()"
    "firstActiveAmbientBindLogged_.exchange(")
  string(FIND "${runtime}\n${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting runtime contract is missing '${required}'")
  endif()
endforeach()

if(runtime MATCHES "farDimensionsFor\\(activeQuality_\\)")
  message(FATAL_ERROR
    "Skylighting far-grid dimensions must remain independent of the near quality tier")
endif()

string(FIND "${d3dHook}" "observePrivateCaptureDraw(context)" privateDrawObservation)
if(privateDrawObservation EQUAL -1)
  message(FATAL_ERROR
    "Skylighting private draw-state observation is not connected to D3D11 draws")
endif()

foreach(required IN ITEMS
    "Quality quality{ Quality::high }"
    "bEnabled"
    "iQuality"
    "fMinDiffuseVisibility"
    "fMinSpecularVisibility"
    "fMaxZenithDegrees")
  string(FIND "${settingsHeader}\n${settingsStore}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting settings contract is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "register(t50)"
    "register(t51)"
    "register(u7)"
    "register(b13)"
    "Texture3D<float4> NearSkylightingProbeArray"
    "Texture3D<float4> FarSkylightingProbeArray"
    "ProbeLevelSettings NearLevel"
    "ProbeLevelSettings FarLevel"
    "LevelExtent("
    "smoothstep("
    "lerp(farVisibility, nearVisibility, nearWeight)"
    "RWByteAddressBuffer SkylightingAmbientDiagnostic"
    "ReconstructRelativeWorldPosition"
    "const float2 screenUv = input.Position.xy /"
    "NativeDepth.SampleLevel("
    "const float depth = loadedDepth.x"
    "NativeDepth.Load("
    "Texture2D<float4> NativeDepth"
    "NativeNormal.Load("
    "NativeMaterial.Load("
    "register(s3)"
    "insideVolume"
    "FauxSpecularLobe"
    "output.DiffuseVisibility"
    "output.SpecularVisibility")
  string(FIND "${ambientShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting ambient shader is missing '${required}'")
  endif()
endforeach()

foreach(stale IN ITEMS
    "DFLight[0]"
    "DFLight[45]"
    "NativeNormal.Sample("
    "NativeMaterial.Sample(")
  string(FIND "${ambientShader}" "${stale}" staleAmbientCoordinate)
  if(NOT staleAmbientCoordinate EQUAL -1)
    message(FATAL_ERROR
      "Skylighting ambient shader retained disproven native addressing '${stale}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "register(t0)"
    "register(u0)"
    "register(u1)"
    "register(u2)"
    "RWByteAddressBuffer DiagnosticStats"
    "ProbeLevelSettings NearLevel"
    "ProbeLevelSettings FarLevel"
    "UpdateControl"
    "probeSlice"
    "sampledDepth < 1.0f - 1.0e-5f"
    "register(s0)"
    "register(b13)"
    "[numthreads(8, 8, 1)]"
    "min(previousFrames + 1u, 255u)")
  string(FIND "${computeShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting compute shader is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "flatten_operands("
    "Skylighting template retained an unmapped temporary operand")
  string(FIND "${generator}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting DXBC nested-operand gate is missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL
  "Fo4vrCsSkylightingAmbientContract\\{"
  ambientContracts "${ambientHeader}")
list(LENGTH ambientContracts ambientContractCount)
if(NOT ambientContractCount EQUAL 39)
  message(FATAL_ERROR
    "Skylighting ambient coverage changed: expected 39 contracts, found ${ambientContractCount}")
endif()

foreach(required IN ITEMS
    "SkylightingAmbient.h"
    "csah_skylighting_ambient_contracts"
    "skylightingContract.gammaOffsets"
    "selectDFLightAmbientPixelShader")
  string(FIND "${linearRuntime}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting ambient owner integration is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylighting::Runtime::get().scopeAmbientDraw"
    "skylighting::installNativeHooks()"
    "D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL")
  string(FIND "${d3dHook}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting D3D11 integration is missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "substituteDepthStencil"
    "privateCaptureActive"
    "rendererData->depthStencilTargets")
  string(FIND "${runtime}\n${runtimeHeader}\n${d3dHook}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting retained disproven D3D output-merger path '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylighting::Runtime::get().applySettings"
    ".skylighting = skylighting::loadSettings(path)")
  string(FIND "${sharedSettings}\n${sharedSettingsHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting shared settings publication is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "skylighting::loadSettings()"
    "ensureSkylightingNativeHooks("
    "ensureSkylightingNativeHooks(\"PostLoadGame\")"
    "ensureSkylightingNativeHooks(\"NewGame\")"
    "skylighting::validateNativeHooks("
    ".beginWorldSession()")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting plugin lifecycle is missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "\"id\": \"skylighting\""
    "\"id\": \"skylighting-quality\""
    "\"id\": \"skylighting-diffuse-min\""
    "\"id\": \"skylighting-specular-min\""
    "\"id\": \"skylighting-zenith\"")
  string(FIND "${palmSettings}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Skylighting PALM settings contract is missing '${required}'")
  endif()
endforeach()

message(STATUS
  "Verified Skylighting: exact FO4VR native capture, shared stereo near/far clipmap, 39 ambient contracts, and PALM settings ownership")
