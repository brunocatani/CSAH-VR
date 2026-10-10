foreach(variable IN ITEMS
    BLOOM_GLARE_RUNTIME_SOURCE
    BLOOM_GLARE_RUNTIME_HEADER
    BLOOM_GLARE_SETTINGS_HEADER
    BLOOM_GLARE_SETTINGS_STORE_SOURCE
    BLOOM_GLARE_SHADER_GENERATOR
    BLOOM_GLARE_SHADER_DIR
    FILMIC_RUNTIME_SOURCE
    FILMIC_SHADER_SOURCE
    D3D11_HOOK_SOURCE
    SHARED_SETTINGS_SOURCE
    SHARED_SETTINGS_HEADER
    PLUGIN_SOURCE
    PALM_SETTINGS_SOURCE)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "Bloom/Glare verification input '${variable}' is missing")
  endif()
endforeach()

file(READ "${BLOOM_GLARE_RUNTIME_SOURCE}" runtime)
file(READ "${BLOOM_GLARE_RUNTIME_HEADER}" runtimeHeader)
file(READ "${BLOOM_GLARE_SETTINGS_HEADER}" settings)
file(READ "${BLOOM_GLARE_SETTINGS_STORE_SOURCE}" settingsStore)
file(READ "${BLOOM_GLARE_SHADER_GENERATOR}" generator)
file(READ "${FILMIC_RUNTIME_SOURCE}" filmicRuntime)
file(READ "${FILMIC_SHADER_SOURCE}" filmicShader)
file(READ "${D3D11_HOOK_SOURCE}" hooks)
file(READ "${SHARED_SETTINGS_SOURCE}" sharedSettings)
file(READ "${SHARED_SETTINGS_HEADER}" sharedSettingsHeader)
file(READ "${PLUGIN_SOURCE}" plugin)
file(READ "${PALM_SETTINGS_SOURCE}" palmSettings)

foreach(required IN ITEMS
    "ScopedComputeState computeState"
    "dispatchBloom("
    "dispatchGlare("
    "generatePsf("
    "dispatchFft("
    "kPrivateResourceSlot = 4"
    "kPrivateConstantSlot = 13"
    "kColorFormat ="
    "DXGI_FORMAT_R16G16B16A16_FLOAT"
    "DXGI_FORMAT_R32G32_FLOAT"
    "sceneDescription.Width & 1u"
    "settings.glare.fftResolution"
    "for (std::uint32_t eye = 0; eye < 2u; ++eye)"
    "bloomReady ? bloomResources_[0].Get() : nullptr"
    "glareReady ? glareOutput_.resource.Get() : nullptr"
    "setOutputFeatureRequested("
    "firstDispatchLogged_"
    "releaseSizeResources()")
  string(FIND "${runtime}${runtimeHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare runtime regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "thresholdEV"
    "intensity"
    "fftResolution"
    "paddingRatio"
    "apertureMode"
    "apertureBlades"
    "fStop"
    "fresnelExponent"
    "sphericalAberration"
    "chromaticSpread"
    "kernelScale"
    "psfSharpness"
    "psfNoiseFloor"
    "sanitizeFftResolution")
  string(FIND "${settings}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kBloomSection = L\"Bloom\""
    "kGlareSection = L\"PhysicalGlare\""
    "L\"iFftResolution\""
    "L\"iApertureMode\""
    "L\"fKernelScale\""
    "settings_path::resolveIniPath()")
  string(FIND "${settingsStore}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare INI regression: missing '${required}'")
  endif()
endforeach()

foreach(shader IN ITEMS
    BloomGlareCommon.hlsli
    BloomCS.hlsl
    GlareThresholdCS.hlsl
    GlareApertureCS.hlsl
    GlarePsfCS.hlsl
    GlareFftCS.hlsl
    GlareCompositeCS.hlsl)
  if(NOT EXISTS "${BLOOM_GLARE_SHADER_DIR}/${shader}")
    message(FATAL_ERROR "Bloom/Glare shader '${shader}' is missing")
  endif()
endforeach()

file(READ "${BLOOM_GLARE_SHADER_DIR}/BloomCS.hlsl" bloomShader)
foreach(required IN ITEMS
    "ClampEyeUv"
    "eye * 0.5"
    "CS_Threshold"
    "CS_Downsample"
    "CS_Upsample"
    "BloomParams")
  string(FIND "${bloomShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Stereo bloom shader regression: missing '${required}'")
  endif()
endforeach()

file(READ "${BLOOM_GLARE_SHADER_DIR}/GlareThresholdCS.hlsl" glareThreshold)
file(READ "${BLOOM_GLARE_SHADER_DIR}/GlareCompositeCS.hlsl" glareComposite)
foreach(required IN ITEMS
    "EyeIndex() * eyeWidth"
    "FftRed"
    "FftGreen"
    "FftBlue")
  string(FIND "${glareThreshold}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Physical Glare threshold regression: missing '${required}'")
  endif()
endforeach()
foreach(required IN ITEMS
    "EyeIndex() * eyeWidth + tid.x"
    "GlareRed"
    "GlareGreen"
    "GlareBlue"
    "glare - bright")
  string(FIND "${glareComposite}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Physical Glare composite regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "kBloomThreshold"
    "kBloomDownsample"
    "kBloomUpsample"
    "kGlareThreshold"
    "kGlareAperture"
    "kGlarePsfRed"
    "kGlareFftRowForward"
    "kGlareConvolveRowInverse"
    "MULTIPLY_PSF=1"
    "kGlareFftColumnInverse"
    "kGlareComposite")
  string(FIND "${generator}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare generator regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "BloomGlareSampler : register(s4)"
    "EnhancedBloomTexture : register(t4)"
    "PhysicalGlareTexture : register(t5)"
    "BloomGlareSettings : register(b13)"
    "BloomGlareComposite.x > 0.5"
    "BloomGlareComposite.y > 0.5")
  string(FIND "${filmicShader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare HDR composite regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "setOutputFeatureRequested(bool requested)"
    "outputFeatureRequested_"
    "filmicStrength = 0.0f")
  string(FIND "${filmicRuntime}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare output ownership regression: missing '${required}'")
  endif()
endforeach()

string(REGEX MATCHALL "bloom_glare::Runtime::get[(][)].scopeDraw[(]" drawScopes "${hooks}")
list(LENGTH drawScopes drawScopeCount)
if(NOT drawScopeCount EQUAL 4)
  message(FATAL_ERROR "Bloom/Glare must own all four D3D11 draw boundaries")
endif()
foreach(required IN ITEMS
    "bloom_glare::Runtime::get().onDeviceCreated("
    "activeFilmicTonemappingBinding")
  string(FIND "${hooks}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare D3D11 ownership regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "bloom_glare::Settings bloomGlare"
    "bool bloomGlare"
    "bloom_glare::loadSettings(path)"
    "bloom_glare::Runtime::get().applySettings(next.bloomGlare)")
  string(FIND "${sharedSettings}${sharedSettingsHeader}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare live-settings regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "bloom_glare::loadSettings()"
    "bloom_glare::Runtime::get().applySettings(")
  string(FIND "${plugin}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare bootstrap regression: missing '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "\"id\": \"bloom\""
    "\"section\": \"Bloom\""
    "\"id\": \"physical-glare\""
    "\"section\": \"PhysicalGlare\""
    "\"key\": \"iFftResolution\""
    "\"key\": \"fChromaticSpread\"")
  string(FIND "${palmSettings}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare PALM settings regression: missing '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "GetProcessTimes"
    "targetFps"
    "autoQuality"
    "HistogramAutoExposure"
    "LocalExposure")
  string(FIND "${runtime}${settings}${generator}${bloomShader}${glareComposite}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "Bloom/Glare contains forbidden '${forbidden}'")
  endif()
endforeach()

message(STATUS "Verified stereo HDR Bloom and per-eye FFT Physical Glare contracts")
