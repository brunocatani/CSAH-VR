foreach(variable IN ITEMS
    PALM_SETTINGS_SOURCE
    PROJECT_CMAKE_SOURCE
    PLUGIN_SOURCE
    SHARED_SETTINGS_SOURCE
    SHARED_SETTINGS_HEADER
    SOURCE_ROOT)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "PALM settings verification input is missing: ${variable}")
  endif()
endforeach()

file(READ "${PALM_SETTINGS_SOURCE}" manifest)
file(READ "${PROJECT_CMAKE_SOURCE}" projectCmake)
file(READ "${PLUGIN_SOURCE}" pluginSource)
file(READ "${SHARED_SETTINGS_SOURCE}" sharedSettingsSource)
file(READ "${SHARED_SETTINGS_HEADER}" sharedSettingsHeader)

string(JSON schemaVersion ERROR_VARIABLE jsonError
  GET "${manifest}" schemaVersion)
if(NOT jsonError STREQUAL "NOTFOUND" OR NOT schemaVersion EQUAL 1)
  message(FATAL_ERROR
    "CSAH PALM settings manifest is invalid: ${jsonError}")
endif()
string(JSON packageId GET "${manifest}" id)
if(NOT packageId STREQUAL "csah")
  message(FATAL_ERROR
    "CSAH PALM settings package id changed: '${packageId}'")
endif()

set(controlIds)
set(controlCount 0)
string(JSON tabCount LENGTH "${manifest}" tabs)
math(EXPR lastTab "${tabCount} - 1")
foreach(tabIndex RANGE 0 ${lastTab})
  string(JSON pageCount LENGTH "${manifest}" tabs ${tabIndex} pages)
  math(EXPR lastPage "${pageCount} - 1")
  foreach(pageIndex RANGE 0 ${lastPage})
    string(JSON groupCount LENGTH "${manifest}"
      tabs ${tabIndex} pages ${pageIndex} groups)
    math(EXPR lastGroup "${groupCount} - 1")
    foreach(groupIndex RANGE 0 ${lastGroup})
      string(JSON groupControlCount LENGTH "${manifest}"
        tabs ${tabIndex} pages ${pageIndex} groups ${groupIndex} controls)
      math(EXPR lastControl "${groupControlCount} - 1")
      foreach(controlIndex RANGE 0 ${lastControl})
        set(controlPath
          tabs ${tabIndex} pages ${pageIndex} groups ${groupIndex}
          controls ${controlIndex})
        string(JSON controlId GET "${manifest}" ${controlPath} id)
        list(FIND controlIds "${controlId}" duplicateIndex)
        if(NOT duplicateIndex EQUAL -1)
          message(FATAL_ERROR
            "CSAH PALM settings control id is duplicated: '${controlId}'")
        endif()
        list(APPEND controlIds "${controlId}")
        math(EXPR controlCount "${controlCount} + 1")

        string(JSON bindingType GET "${manifest}" ${controlPath} binding type)
        string(JSON bindingRoot GET "${manifest}" ${controlPath} binding root)
        string(JSON bindingPath GET "${manifest}" ${controlPath} binding path)
        string(JSON bindingSection GET "${manifest}" ${controlPath} binding section)
        string(JSON bindingKey GET "${manifest}" ${controlPath} binding key)
        if(NOT bindingType STREQUAL "ini" OR
           NOT bindingRoot STREQUAL "documents" OR
           NOT bindingPath STREQUAL
             "Mods_Config/CSAH/CSAH.ini" OR
           bindingSection STREQUAL "" OR bindingKey STREQUAL "")
          message(FATAL_ERROR
            "PALM settings control '${controlId}' lost its shared-INI ownership contract")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()

if(NOT controlCount EQUAL 181)
  message(FATAL_ERROR
    "CSAH PALM settings coverage changed: expected 181 controls, found ${controlCount}")
endif()

foreach(required IN ITEMS
    water-fx-screen-reflections water-reflection-flatness water-reflection-blur
    inverse-square-bEnabled inverse-square-fSourceSize inverse-square-fIntensity
    height-fog-bHeightFogEnabled height-fog-fHeightFogDensity height-fog-fHeightFogHeight height-fog-fHeightFogFalloff
    height-fog-fHeightFogStart height-fog-fHeightFogOpacity height-fog-fHeightFogWeather height-fog-fHeightFogVolume
    terrain-shadows-bEnabled terrain-shadows-fStrength
    community-shaders linear-lighting native-darkness ibl dynamic-cubemaps diffuse-ibl skylighting sky-sync
    bloom physical-glare bloom-threshold bloom-intensity bloom-radius
    glare-threshold glare-intensity glare-fft glare-padding
    glare-aperture-mode glare-blades glare-rotation glare-fstop glare-fresnel
    glare-aberration glare-chromatic glare-kernel glare-psf-sharpness
    glare-noise-floor
    skylighting-quality skylighting-diffuse-min skylighting-specular-min
    skylighting-zenith filmic-tonemapping
    cloud-shadows complex-environment complex-parallax wrapped-grass
    pbr pbr-legacy pbr-direct-ggx pbr-grass-ggx
    pbr-environment-fresnel pbr-energy-conservation pbr-multiscatter
    pbr-specular-occlusion pbr-roughness pbr-roughness-blend pbr-base-f0
    pbr-min-f0 pbr-cubemap-f0 pbr-complex-f0 pbr-direct-scale
    hair-specular subsurface-scattering basic-wetness contact-shadows
    contact-foveated dlaa vanilla-fixes native-auto-exposure exposure-ev
    filmic-strength white-point-scale diffuse-ibl-level grass-wrap
    volumetric-lighting volumetric-quality volumetric-intensity
    volumetric-base volumetric-shafts volumetric-density-mix
    volumetric-density-scale volumetric-distance
    volumetric-temporal volumetric-wind
    hair-highlight skin-strength wetness-amount cloud-opacity strength
    quality precipitation native-shadows-enabled shadow-distance mode
    center-width stereo-sun-occlusion directional-diagnostic
    gpu-profiling-mode lod-matching lod-terrain-brightness lod-terrain-gamma
    lod-object-brightness lod-object-gamma terrain-variation terrain-variation-lod)
  list(FIND controlIds "${required}" controlIndex)
  if(controlIndex EQUAL -1)
    message(FATAL_ERROR
      "CSAH PALM settings lost required control '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "PrismaPanel"
    "Prisma"
    "legacy-panel"
    "wrist panel"
    "wrist-panel")
  string(FIND "${manifest}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "CSAH PALM settings retains obsolete wrist token '${forbidden}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "validateD3D11ShaderHooks(\"GameDataReady\")"
    "validateD3D11ShaderHooks(\"GameSessionReady\")"
    "wrapped_grass::Runtime::get().applySettings"
    "hair_specular::Runtime::get().applySettings"
    "subsurface_scattering::Runtime::get().applySettings"
    "basic_wetness::Runtime::get().applySettings"
    "lod_blending::Runtime::get().applySettings"
    "terrain_variation::Runtime::get().applySettings"
    "screen_space_gi::Runtime::get().applySettings"
    "water_effects::Runtime::get().applySettings"
    "terrain_shadows::Runtime::get().applySettings"
    "inverse_square::Runtime::get().applySettings"
    "pbr::Runtime::get().applySettings"
    "volumetric_lighting::Runtime::get().start"
    "bloom_glare::Runtime::get().applySettings"
    "skylighting::Runtime::get().applySettings"
    "sky_sync::Runtime::get().applySettings")
  string(FIND "${pluginSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "PALM settings bootstrap lost runtime ownership '${required}'")
  endif()
endforeach()

foreach(required IN ITEMS
    "linear_lighting::Runtime::get().queueSettings"
    "dlaa::Runtime::get().applySettings"
    "filmic_tonemapping::Runtime::get().applySettings"
    "bloom_glare::Runtime::get().applySettings"
    "ibl::Runtime::get().applySettings"
    "queueComplexParallaxSettings"
    "contact_shadows::Runtime::get().applySettings"
    "wrapped_grass::Runtime::get().applySettings"
    "hair_specular::Runtime::get().applySettings"
    "subsurface_scattering::Runtime::get().applySettings"
    "basic_wetness::Runtime::get().applySettings"
    "lod_blending::Runtime::get().applySettings"
    "terrain_variation::Runtime::get().applySettings"
    "screen_space_gi::Runtime::get().applySettings"
    "water_effects::Runtime::get().applySettings"
    "terrain_shadows::Runtime::get().applySettings"
    "inverse_square::Runtime::get().applySettings"
    "pbr::Runtime::get().applySettings"
    "cloud_shadows::Runtime::get().applySettings"
    "volumetric_lighting::Runtime::get().applySettings"
    "skylighting::Runtime::get().applySettings"
    "sky_sync::Runtime::get().applySettings"
    "vanilla_fixes::applySettings"
    "GpuTimingProfiler::setOnDemandGroups")
  string(FIND "${sharedSettingsSource}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "PALM settings shared-INI publication lost '${required}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "ui/WristPanelRuntime.h"
    "csah::ui::")
  string(FIND "${pluginSource}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Plugin bootstrap retains obsolete wrist ownership '${forbidden}'")
  endif()
endforeach()
foreach(forbidden IN ITEMS "ui::" "prismaPanel" "Prisma panel")
  string(FIND "${sharedSettingsSource}${sharedSettingsHeader}"
    "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Shared settings retain obsolete wrist ownership '${forbidden}'")
  endif()
endforeach()

foreach(forbidden IN ITEMS
    "PRISMA_F4VR_API_PATH_SELECTED"
    "ROCK_SDK_PATH_SELECTED"
    "WristPanelRuntime"
    "VerifyWristPanel"
    "WRIST_PANEL_"
    "\${ROOT_DIR}/view")
  string(FIND "${projectCmake}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
      "Build graph retains obsolete wrist dependency '${forbidden}'")
  endif()
endforeach()
foreach(required IN ITEMS
    "remove_directory"
    "package/PrismaUI_F4/views/FO4VR-Community-Shaders"
    "F4SE/Plugins/CSAH/PALMSettings.json")
  string(FIND "${projectCmake}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Build graph lost PALM settings staging/legacy cleanup '${required}'")
  endif()
endforeach()

foreach(relativePath IN ITEMS
    "src/ui/LinearLightingTelemetryGate.h"
    "src/ui/PointerClickGate.h"
    "src/ui/WristPanelPose.h"
    "src/ui/WristPanelRuntime.cpp"
    "src/ui/WristPanelRuntime.h"
    "src/ui/WristPanelSettings.cpp"
    "src/ui/WristPanelSettings.h"
    "src/ui/WristProviderRetry.h"
    "view/index.html"
    "cmake/VerifyWristPanel.cmake")
  if(EXISTS "${SOURCE_ROOT}/${relativePath}")
    message(FATAL_ERROR
      "Obsolete CSAH wrist artifact remains: ${relativePath}")
  endif()
endforeach()

message(STATUS
  "Verified PALM settings ownership: 165 shared-INI controls, direct runtime publication, no wrist provider or assets")
