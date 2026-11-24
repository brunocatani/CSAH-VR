function(cs_require_text FILE_PATH REQUIRED_TEXT DESCRIPTION)
  if(NOT EXISTS "${FILE_PATH}")
    message(FATAL_ERROR "Missing ${DESCRIPTION}: ${FILE_PATH}")
  endif()
  file(READ "${FILE_PATH}" contents)
  string(FIND "${contents}" "${REQUIRED_TEXT}" match)
  if(match EQUAL -1)
    message(FATAL_ERROR
      "${DESCRIPTION} does not contain required contract '${REQUIRED_TEXT}'")
  endif()
endfunction()

function(cs_verify_compiled_parallax SHADER_BINARY DESCRIPTION INSTANCED)
  if(NOT EXISTS "${SHADER_BINARY}")
    message(FATAL_ERROR "Missing compiled ${DESCRIPTION}: ${SHADER_BINARY}")
  endif()
  execute_process(
    COMMAND "${FXC_EXECUTABLE}" /dumpbin "${SHADER_BINARY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE dump
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not inspect ${DESCRIPTION}: ${error}")
  endif()
  foreach(required IN ITEMS
      "dcl_constantbuffer CB5[9]"
      "dcl_constantbuffer CB8[1]"
      "dcl_constantbuffer CB12[71]"
      "dcl_output o0.xyzw"
      "dcl_output o1.xy"
      "dcl_output o2.xyzw"
      "dcl_output o3.xyzw"
      "dcl_output o4.xyz"
      "dcl_output o5.xy"
      "EYEINDEX")
    string(FIND "${dump}" "${required}" match)
    if(match EQUAL -1)
      message(FATAL_ERROR "Compiled ${DESCRIPTION} lost '${required}'")
    endif()
  endforeach()
  if(INSTANCED)
    foreach(required IN ITEMS
        "dcl_resource_texture2darray (float,float,float,float) t0"
        "dcl_resource_texture2darray (float,float,float,float) t1"
        "dcl_resource_texture2darray (float,float,float,float) t2"
        "dcl_resource_structured t4, 76")
      string(FIND "${dump}" "${required}" match)
      if(match EQUAL -1)
        message(FATAL_ERROR "Compiled ${DESCRIPTION} lost '${required}'")
      endif()
    endforeach()
  else()
    foreach(slot RANGE 0 11)
      string(FIND "${dump}"
        "dcl_resource_texture2d (float,float,float,float) t${slot}" match)
      if(match EQUAL -1)
        message(FATAL_ERROR "Compiled ${DESCRIPTION} lost texture t${slot}")
      endif()
    endforeach()
  endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  foreach(required IN ITEMS
      "weightedHeight / max(activeWeight, 1.0e-5)"
      "if ((Weight) > 0.0)"
      "const float distanceDetail = sqrt(saturate(fade));"
      "previousDepth = sampledDepth;"
      "dot(cb12[0].xyz, input.currentPosition.xyz)"
      "input.tangent.x,"
      "input.bitangent.x,"
      "input.normal.x));"
      "NormalizeLandscapeWeights(input.layerWeights)"
      "LinearLightingDecodedDiffuse(diffuse)"
      "LinearLightingEmitColor(cb2[1].xyz)")
    cs_require_text("${HLSL_SOURCE}" "${required}"
      "combined Linear Lighting complex-parallax shader")
  endforeach()
  cs_require_text("${LINEAR_LIGHTING_INCLUDE}"
    "uint enableComplexParallax"
    "shared b5 ABI")
  cs_require_text("${RUNTIME_SOURCE}"
    "complex_materials::landscapeParallaxSlot"
    "exact parallax material mapping")
  cs_require_text("${RUNTIME_SOURCE}"
    "complexParallaxReplacementShaders_"
    "combined replacement selection")
  cs_require_text("${RUNTIME_SOURCE}"
    "queueComplexParallaxSettings"
    "render-boundary parallax settings")
  cs_verify_compiled_parallax(
    "${BASE_BINARY}" "base landscape complex parallax" FALSE)
  cs_verify_compiled_parallax(
    "${LOD_BINARY}" "LOD landscape complex parallax" FALSE)
  cs_verify_compiled_parallax(
    "${INSTANCED_BINARY}" "instanced LOD landscape complex parallax" TRUE)
endif()
