if(NOT DEFINED D3D11_HOOK_SOURCE OR
   NOT EXISTS "${D3D11_HOOK_SOURCE}")
  message(FATAL_ERROR "D3D11_HOOK_SOURCE is missing")
endif()

file(READ "${D3D11_HOOK_SOURCE}" source)

foreach(required IN ITEMS
    "#include <MinHook.h>"
    "addressBelongsToModule"
    "GetModuleHandleW(L\"d3d11.dll\")"
    "MH_QueueEnableHook"
    "MH_ApplyQueued"
    "captureMinHookPatchIdentity"
    "detourPatchOwned"
    "shaderInterceptionActive.store(false"
    "shaderInterceptionActive.store(true"
    "resident hooks remain strict pass-through"
    "complete native COM vtables remain untouched"
    "validateD3D11ShaderHooks"
    "hookCreateVertexShader"
    "kCreateVertexShaderVtableIndex = 12"
    "hookInverseDispatch"
    "hookInverseDispatchIndirect"
    "inverseDispatchTargets={table[41],table[42]}"
    "detourPatchOwned(inverseDispatchTargets[0],inverseDispatchPatches[0])"
    "detourPatchOwned(inverseDispatchTargets[1],inverseDispatchPatches[1])"
    "inverse_square::Runtime::get().scope(context,false,activeInverseSquarePixel,localMaterials.active())"
    "hookCreateComputeShader"
    "kCreateComputeShaderVtableIndex = 18"
    "selectComputeShader"
    "hookVSSetShader"
    "kVSSetShaderVtableIndex = 11"
    "onVertexShaderCreated("
    "isGrassVertexShader(shader)"
    "reconcileGrassVertexClassAtDraw(context)"
    "selectPixelShaderForGrassVertex("
    "original(context, shader, classInstances, classInstanceCount)"
    "hookOMSetRenderTargets"
    "kOMSetRenderTargetsVtableIndex = 33"
    "hookOMSetRenderTargetsAndUnorderedAccessViews"
    "kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex = 34"
    "surfaceRuntime.prepareGBufferBinding"
    "void beginDFPrePassTechnique("
    "observeDFPrePassDescriptor("
    "void endDFPrePassTechnique("
    "activeDFPrePassDescriptor() noexcept"
    "kMaxDFPrePassTechniqueDepth = 8"
    "void publishDFPrePassGeometry("
    "pendingDFPrePassDescriptor = { descriptor, nullptr, true }"
    "activeAuthoredPbrBaseTexture"
    "consumePendingDFPrePassDescriptorAtDraw(context)"
    "selectPixelShaderForDFPrePassDescriptor("
    "activeReplacementOriginal"
    "activeReplacementContext"
    "activeSurfaceClassCode = selection.surfaceClassCode"
    "no hook repair was attempted")
  string(FIND "${source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour regression: missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL
  "markWorldFrameConsumed\(\)"
  surfaceFrameConsumes "${source}")
list(LENGTH surfaceFrameConsumes surfaceFrameConsumeCount)
if(NOT surfaceFrameConsumeCount EQUAL 0)
  message(FATAL_ERROR
    "Surface-classification lifetime must follow the native frame boundary, not a directional-light draw")
endif()

string(FIND "${source}"
  "void beginDFPrePassTechnique(" descriptorRebindStart)
string(FIND "${source}"
  "    bool installEarlyD3D11Hooks()" descriptorRebindEnd)
if(descriptorRebindStart EQUAL -1 OR descriptorRebindEnd EQUAL -1 OR
   NOT descriptorRebindStart LESS descriptorRebindEnd)
  message(FATAL_ERROR
    "D3D11 detour regression: descriptor transaction boundary is missing")
endif()
math(EXPR descriptorRebindLength
  "${descriptorRebindEnd} - ${descriptorRebindStart}")
string(SUBSTRING "${source}" ${descriptorRebindStart}
  ${descriptorRebindLength} descriptorRebindSource)
foreach(forbidden IN ITEMS
    "PSGetShader"
    "VirtualQuery"
    "new "
    "std::vector"
    "std::ifstream"
    "std::ofstream"
    "CreateFile")
  string(FIND "${descriptorRebindSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 descriptor rebind hot-path regression: contains '${forbidden}'")
  endif()
endforeach()

string(REGEX MATCHALL
  "consumePendingDFPrePassDescriptorAtDraw\\(context\\)"
  descriptorDrawConsumes "${source}")
list(LENGTH descriptorDrawConsumes descriptorDrawConsumeCount)
if(NOT descriptorDrawConsumeCount EQUAL 4)
  message(FATAL_ERROR
    "D3D11 descriptor transaction must expire at all four intercepted draw boundaries")
endif()

string(REGEX MATCHALL
  "reconcileGrassVertexClassAtDraw\\(context\\)"
  grassDrawReconciles "${source}")
list(LENGTH grassDrawReconciles grassDrawReconcileCount)
if(NOT grassDrawReconcileCount EQUAL 4)
  message(FATAL_ERROR
    "D3D11 exact grass classification must reconcile at all four intercepted draw boundaries")
endif()

string(REGEX MATCHALL "MH_CreateHook\\(" createHookCalls "${source}")
list(LENGTH createHookCalls createHookCallCount)
if(NOT createHookCallCount EQUAL 9)
  message(FATAL_ERROR
    "D3D11 detour regression: expected seven core calls, one bounded draw helper, and one bounded Dispatch helper")
endif()

string(FIND "${source}" "Runtime::get().onDeviceCreated" deviceReady)
string(FIND "${source}" "shaderInterceptionActive.store(true" activate)
if(deviceReady EQUAL -1 OR activate EQUAL -1 OR
   NOT deviceReady LESS activate)
  message(FATAL_ERROR
    "D3D11 detour regression: interception activated before GPU initialization")
endif()

string(FIND "${source}" "HookSnapshot d3d11HookSnapshot" snapshotStart)
if(snapshotStart EQUAL -1)
  message(FATAL_ERROR
    "D3D11 detour regression: hook snapshot boundary is missing")
endif()
string(SUBSTRING "${source}" ${snapshotStart} -1 snapshotSource)
foreach(forbidden IN ITEMS
    "VirtualQuery"
    "detourPatchOwned(")
  string(FIND "${snapshotSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour hot-path regression: snapshot contains '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "D3D11HookRepairGate"
    "d3d11_hook_repair"
    "immediateContextVtableShadow"
    "kDeviceContextVtableEntryCount"
    "initialContextVtable"
    "immediateContextVtableCell"
    "downstreamPSSetShader"
    "Restored D3D11 immediate-context vtable shadow"
    "Installed isolated D3D11 immediate-context vtable shadow")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "D3D11 detour regression: forbidden stale path '${forbidden}'")
  endif()
endforeach()
