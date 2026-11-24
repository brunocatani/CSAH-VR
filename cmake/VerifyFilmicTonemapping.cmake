cmake_minimum_required(VERSION 4.2)

foreach(required IN ITEMS
    FILMIC_RUNTIME_SOURCE
    FILMIC_RUNTIME_HEADER
    FILMIC_SETTINGS_HEADER
    FILMIC_SETTINGS_STORE_SOURCE
    FILMIC_SHADER_SOURCE
    FILMIC_SHADER_GENERATOR
    D3D11_HOOK_SOURCE
    SHARED_SETTINGS_SOURCE
    SHARED_SETTINGS_HEADER
    PLUGIN_SOURCE)
  if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
    message(FATAL_ERROR
      "Filmic Tonemapping verification input is missing: ${required}")
  endif()
endforeach()

function(require_token path token description)
  file(READ "${path}" contents)
  string(FIND "${contents}" "${token}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "Filmic Tonemapping contract lost ${description}: '${token}' in '${path}'")
  endif()
endfunction()

require_token("${FILMIC_SHADER_GENERATOR}"
  "ImageSpace[026]" "the executing base packed FXP family")
require_token("${FILMIC_SHADER_GENERATOR}"
  "83b514dd63eea60d84769343d576d4fa"
  "the executing base native DXBC identity")
require_token("${FILMIC_SHADER_GENERATOR}"
  "ImageSpace[027]" "the fade packed FXP family")
require_token("${FILMIC_SHADER_GENERATOR}"
  "f1bfa042d52062c4aedfd612bbf4799d"
  "the fade native DXBC identity")
require_token("${FILMIC_SHADER_SOURCE}"
  "Texture2D<float4> BypassMaskTexture : register(t3)"
  "the native UI/image-space bypass mask")
require_token("${FILMIC_SHADER_SOURCE}"
  "cbuffer NativeHdrBlend : register(b2)"
  "the native HDR blend constants")
require_token("${FILMIC_SHADER_SOURCE}"
  "cbuffer FilmicSettings : register(b12)"
  "the private scoped filmic constants")
require_token("${FILMIC_SHADER_SOURCE}"
  "FO4VR_FILMIC_FADE" "the exact base/fade semantic split")
require_token("${FILMIC_SHADER_SOURCE}"
  "NativeExposure.z / (adaptedLuminance + 0.001)"
  "the engine adaptation contract")
require_token("${FILMIC_SHADER_SOURCE}"
  "NativeHableUnnormalized" "the verified native filmic curve")
require_token("${FILMIC_SHADER_SOURCE}"
  "huePreserving" "hue-preserving highlight compression")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "kNativeBaseBytecodeSize = 1772"
  "the exact executing base bytecode length")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "kNativeFadeBytecodeSize = 1848"
  "the exact fade bytecode length")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "generated::kBasePixelShader" "the generated base replacement asset")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "generated::kFadePixelShader" "the generated fade replacement asset")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "PSGetConstantBuffers" "constant-buffer preservation")
require_token("${FILMIC_RUNTIME_SOURCE}"
  "PSSetConstantBuffers" "constant-buffer bind and restoration")
require_token("${FILMIC_SETTINGS_HEADER}"
  "useNativeAutoExposure" "the explicit native-adaptation policy")
require_token("${FILMIC_SETTINGS_STORE_SOURCE}"
  "FilmicTonemapping" "the persistent settings table")
require_token("${D3D11_HOOK_SOURCE}"
  "filmic_tonemapping::Runtime::get().onPixelShaderCreated"
  "shader identity registration")
require_token("${D3D11_HOOK_SOURCE}"
  "activeFilmicTonemappingBinding" "draw-lifetime output ownership")
require_token("${SHARED_SETTINGS_HEADER}"
  "filmicTonemapping" "shared INI change detection")
require_token("${SHARED_SETTINGS_SOURCE}"
  "filmic_tonemapping::Runtime::get().applySettings"
  "live settings publication")
require_token("${PLUGIN_SOURCE}"
  "filmic_tonemapping::loadSettings" "startup settings load")

message(STATUS
  "Filmic Tonemapping contract verified: executing FO4VR base/fade HDR output family, native adaptation and scoped CB12")
