if(NOT DEFINED DFPREPASS_HOOK_SOURCE OR
   NOT EXISTS "${DFPREPASS_HOOK_SOURCE}")
  message(FATAL_ERROR "BSDFPrePass hook source is unavailable")
endif()

file(READ "${DFPREPASS_HOOK_SOURCE}" hook_source)
foreach(required_token IN ITEMS
    "kVtableRva = 0x030B8C68"
    "kTechniqueSetupSlot = 4"
    "kTechniqueSetupFunctionRva = 0x0287B720"
    "kTechniqueRestoreSlot = 5"
    "kTechniqueRestoreFunctionRva = 0x028789B0"
    "kGeometrySetupSlot = 9"
    "kGeometrySetupFunctionRva = 0x0287CF60"
    "kGeometryDescriptorLoadRva = 0x0287D1AB"
    "kGeometryStateDescriptorOffset = 0x40"
    "kPassGeometryOwnerOffset = 0x18"
    "kGeometryShaderPropertyOffset = 0x178"
    "kShaderPropertyMaterialOffset = 0x58"
    "kLightingMaterialBaseTextureOffset = 0x38"
    "plausibleEnginePointer("
    "std::array<std::byte, 26> kTechniqueSetupSignature"
    "std::byte{ 0x40 }, std::byte{ 0x53 }"
    "std::array<std::byte, 16> kTechniqueRestoreSignature"
    "std::array<std::byte, 55> kGeometrySetupSignature"
    "std::array<std::byte, 3> kGeometryDescriptorLoadSignature"
    "std::byte{ 0x8B }, std::byte{ 0x57 }, std::byte{ 0x40 }"
    "void* geometryState"
    "std::memcmp("
    "#include \"render/D3D11Hooks.h\""
    "beginDFPrePassTechnique(pixelShaderKey(descriptor))"
    "endDFPrePassTechnique(pixelShaderKey(descriptor))"
    "publishDFPrePassGeometry(pixelShaderKey(descriptor), baseTexture)"
    "kSetShadersRva = 0x01D92C00"
    "kRendererCurrentPixelShaderOffset = 0x1EE0 + 0x6A0"
    "kPixelShaderKeyMask = 0xFFFFEFE7"
    "publishDFPrePassShaderKey(key)"
    "std::atomic_uint32_t descriptorConsumerMask"
    "setDFPrePassLinearLightingEnabled(bool enabled) noexcept"
    "setDFPrePassComplexEnvironmentEnabled(bool enabled) noexcept"
    "setDFPrePassIblEnabled(bool enabled) noexcept"
    "setDFPrePassSurfaceClassificationEnabled(bool enabled) noexcept"
    "setDFPrePassAuthoredPbrEnabled(bool enabled) noexcept"
    "descriptorConsumerMask.load(std::memory_order_acquire) == 0"
    "static_cast<const std::byte*>(geometryState) +"
    "kGeometryStateDescriptorOffset"
    "geometryDescriptorLoadMatches"
    "rollbackComplete"
    "installed.store(false, std::memory_order_release)")
  string(FIND "${hook_source}" "${required_token}" token_offset)
  if(token_offset EQUAL -1)
    message(FATAL_ERROR
      "BSDFPrePass hook lost required FO4VR identity/scope token: ${required_token}")
  endif()
endforeach()

string(FIND "${hook_source}" "bool __fastcall hookSetupTechnique(" hook_start)
string(FIND "${hook_source}" "    bool installBSDFPrePassShaderHook()" hook_end)
if(hook_start EQUAL -1 OR hook_end EQUAL -1 OR hook_end LESS_EQUAL hook_start)
  message(FATAL_ERROR "BSDFPrePass runtime-hook boundary is unavailable")
endif()
math(EXPR hook_length "${hook_end} - ${hook_start}")
string(SUBSTRING "${hook_source}" ${hook_start} ${hook_length} hook_body)

if(hook_body MATCHES "readable\\(|VirtualQuery\\(")
  message(FATAL_ERROR
    "BSDFPrePass runtime hooks must not query virtual memory in hot paths")
endif()

string(FIND "${hook_body}"
  "descriptorConsumerMask.load(std::memory_order_acquire) == 0" gate_offset)
string(FIND "${hook_body}"
  "static_cast<const std::byte*>(geometryState) +" read_offset)
if(gate_offset EQUAL -1 OR read_offset EQUAL -1 OR
   gate_offset GREATER_EQUAL read_offset)
  message(FATAL_ERROR
    "BSDFPrePass descriptor read must remain behind the any-consumer gate")
endif()

foreach(forbidden_read IN ITEMS
    "kDescriptorOffset = 0x48")
  string(FIND "${hook_source}" "${forbidden_read}" forbidden_read_offset)
  if(NOT forbidden_read_offset EQUAL -1)
    message(FATAL_ERROR
      "BSDFPrePass hook restored stale pass-owned descriptor read: ${forbidden_read}")
  endif()
endforeach()

string(FIND "${hook_body}"
  "beginDFPrePassTechnique(pixelShaderKey(descriptor));" technique_begin)
string(FIND "${hook_body}"
  "const auto accepted = original(" technique_original)
string(FIND "${hook_body}"
  "endDFPrePassTechnique(pixelShaderKey(descriptor));" technique_end)
if(technique_begin EQUAL -1 OR technique_original EQUAL -1 OR
   technique_end EQUAL -1 OR
   NOT technique_begin LESS technique_original OR
   NOT technique_original LESS technique_end)
  message(FATAL_ERROR
    "BSDFPrePass technique descriptor must bracket native setup failure")
endif()

string(FIND "${hook_body}"
  "publishDFPrePassGeometry(pixelShaderKey(descriptor), baseTexture);\n            original(receiver, pass, geometryState)"
  ordered_handoff)
if(ordered_handoff EQUAL -1)
  message(FATAL_ERROR
    "BSDFPrePass descriptor publication must precede native SetupGeometry")
endif()

foreach(forbidden IN ITEMS
    "DescriptorScope"
    "activeDFPrePassDescriptorScope"
    "compiledProgram"
    "publishDFPrePassDescriptor")
  string(FIND "${hook_source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "BSDFPrePass hook restored stale temporary descriptor scope '${forbidden}'")
  endif()
endforeach()

if(hook_body MATCHES "kAllDescriptorConsumers|!=[ \t\r\n]*kAllDescriptorConsumers")
  message(FATAL_ERROR
    "BSDFPrePass descriptor capture must not require unrelated consumers")
endif()

if(hook_source MATCHES "REL::|CommonLib|RuntimeVersion")
  message(FATAL_ERROR
    "BSDFPrePass hook must remain independent of unverified CommonLib addresses")
endif()

message(STATUS "Verified FO4VR BSDFPrePass descriptor hook source contract")
