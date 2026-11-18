foreach(variable IN ITEMS PBR_RUNTIME_SOURCE D3D11_HOOK_SOURCE DFPREPASS_HOOK_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "${variable} is unavailable")
  endif()
endforeach()

file(READ "${PBR_RUNTIME_SOURCE}" pbr_source)
file(READ "${D3D11_HOOK_SOURCE}" d3d_source)
file(READ "${DFPREPASS_HOOK_SOURCE}" dfprepass_source)

foreach(required IN ITEMS
    "kMaximumAuthoredMaterials = 65536"
    "kMaterialLoadQueueCapacity = 4096"
    "kMaterialLoadsPerMainThreadTask = 4"
    "baseTexture->GetName()"
    "std::lower_bound("
    "queueLoad(std::uint32_t recordIndex)"
    "F4SE::GetTaskInterface()"
    "processMaterialLoadsOnMainThread()"
    "authoredTransportEnabled_.load(std::memory_order_acquire)"
    "cancelQueuedLoads()"
    "setDFPrePassAuthoredPbrEnabled(authoredTransport)"
    "without preloading texture assets")
  string(FIND "${pbr_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Authored PBR lazy-runtime contract lost '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "kMaximumAuthoredMaterials = 1024"
    "pendingRecords"
    "PSGetShaderResources(0, 1"
    "baseTexture = loadTexture"
    ".baseTexture = std::move(baseTexture)")
  string(FIND "${pbr_source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "Authored PBR restored eager or guessed lookup '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "activeAuthoredPbrBaseTexture"
    "scopeAuthoredMaterialDraw("
    "baseTexture)")
  string(FIND "${d3d_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Authored PBR draw handoff lost '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kPassGeometryOwnerOffset = 0x18"
    "kGeometryShaderPropertyOffset = 0x178"
    "kShaderPropertyMaterialOffset = 0x58"
    "kLightingMaterialBaseTextureOffset = 0x38"
    "publishDFPrePassGeometry(pixelShaderKey(descriptor), baseTexture)")
  string(FIND "${dfprepass_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Authored PBR verified pointer walk lost '${required}'")
  endif()
endforeach()

message(STATUS "Verified lazy authored-PBR material runtime")
