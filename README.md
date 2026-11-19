# Community Shaders at Home (CSAH) VR

An experimental rendering mod for **Fallout 4 VR**, bringing Community Shaders lighting, materials, shadows, and image-quality features to the game's native Direct3D 11 renderer.

This is a Fallout 4 VR implementation with stereo rendering support. Feature coverage and visual results vary; it does not include every feature from Skyrim Community Shaders or Open Shaders.

**[Download CSAH VR 0.0.6 Alpha — installable mod ZIP](https://github.com/brunocatani/CSAH-VR/releases/download/v0.0.6-alpha/CSAH-VR-0.0.6-Alpha.zip)**

[All releases](https://github.com/brunocatani/CSAH-VR/releases) · [Report an issue](https://github.com/brunocatani/CSAH-VR/issues) · [GPL-3.0 license](LICENSE)

## Features

The included features are listed individually below. Incomplete or unconfirmed effects are identified alongside their descriptions.

### Lighting

- **Linear Lighting** — Fallout 4 VR already lights in linear space, so at its defaults this feature reproduces the game exactly. Its response and intensity controls for direct light, ambient light, colour, emission, glowmaps, fog, sky, water, effects and volumetric lighting adjust the image from there: a response of 2.2 matches the game, lower brightens dark values, higher darkens them. Enabled in the first-run gameplay preset.
- **Image Based Lighting** — Supplies a shared stereo environment to the reflection and diffuse-lighting features.
- **Dynamic Cubemaps** — Uses the captured scene environment for position-aware reflections, with each material's native cubemap retained when capture confidence is insufficient. Reflection placement and stereo behavior remain experimental.
- **Diffuse IBL** — Adds environment-derived diffuse bounce lighting. Can be expensive in dense scenes.
- **Skylighting** — Uses a shared world-space probe field to occlude sky light beneath roofs, trees, and overhangs. Probe quality requires a restart; visibility and zenith controls apply live.
- **Sky Sync / Moon Lighting** — Aligns directional lighting and shadows with the Sun by day and Fallout's climate-enabled Moon by night.
- **Cloud Shadows — Not working.** Disabled by default in the plugin and the automatically created settings file.

### Materials

- **Physical Materials (PBR)** — Provides a shared physical response for direct lighting, IBL, cubemaps, wetness, and native reflections. Requires Linear Lighting.
- **Authored PBR Materials** — Supports content-mod manifests pairing base-colour textures with roughness, metalness, ambient-occlusion, and specular data. Textures stream when encountered; authored content still requires visual testing. See the material format below.
- **Complex Metal Response** — Enables complex-material environment response when suitable assets and IBL data are available. A visible result remains unconfirmed with ordinary texture replacements.
- **Complex Parallax** — Adds surface depth to assets authored for the complex-material texture contract, with quality, depth, grazing-angle, and distance-fade controls.
- **Wrapped Grass Lighting — Not working.** Disabled by default in the plugin and the automatically created settings file.
- **Hair Specular — Not working.** Disabled by default in the plugin and the automatically created settings file.
- **Subsurface Scattering — Not working.** Disabled by default in the plugin and the automatically created settings file.
- **Basic Wetness** — Applies a manually selected wet appearance through diffuse darkening, specular response, and roughness. Material coverage is experimental; this is not a complete weather-driven wetness system.

### PBR model controls

- **Convert Legacy Materials** — Converts existing Fallout shininess and specular values into physical roughness and reflectance without rewriting the assets.
- **Direct-Light GGX** — Uses a microfacet specular response for Sun and directional lighting.
- **GGX on Grass** — Optionally applies that specular response to grass. Disabled by default.
- **Environment Fresnel** — Makes environment reflections respond to viewing angle and material roughness.
- **Energy Conservation** — Reduces diffuse lighting as reflected energy and metalness increase.
- **Multiscatter Compensation** — Compensates for energy lost by the single-scatter specular model on rough surfaces.
- **Specular Occlusion** — Limits environment-reflection leakage in strongly occluded material response.

### Shadows and atmosphere

- **Contact Shadows** — Adds short-range screen-space directional occlusion, with strength and ray-distance controls. Close-range coverage and stereo artifacts remain areas for testing.
- **Foveated Sampling** — Reduces contact-shadow sampling work outside the center view while retaining center fidelity.
- **Native Shadow Fixes** — Provides independently controlled native-shadow corrections, with fixed shadow distance, cascade blending, and orthographic filtering. Changes require a restart.
- **Extended Directional Cascades** — Extends the native directional-shadow cascade setup. Requires a restart.
- **Tiled Deferred Lighting** — Controls the native tiled deferred-lighting path through the native-shadow settings. Requires a restart.
- **Volumetric Lighting / Godrays — Not working.** Disabled by default in the plugin and the automatically created settings file.

### Tonemapping and optics

- **Filmic Tonemapping** — Applies hue-preserving highlight compression while retaining native bloom, exposure, cinematic state, and fades. Its visible difference still needs isolated testing.
- **Use Native Auto Exposure** — Uses Fallout's adapted luminance and weather/image-space exposure limits, with exposure compensation and white-point controls.
- **Enhanced Bloom** — Adds a separate HDR bloom response for each eye while preserving native bloom. Includes threshold, intensity, and radius controls; visual qualification remains pending.
- **Physical Glare** — Applies an independent optical glare convolution to each eye, with aperture, diffraction, chromatic-spread, and resolution controls. Visual and performance qualification remain pending.

### Image quality

- **DLAA** — Runs NVIDIA neural anti-aliasing at native render resolution through Streamline.
- **DLSS** — Provides Quality, Balanced, Performance, and Ultra Performance upscaling modes through Streamline.
- **Center DLAA + TAA Periphery** — Applies DLAA to an adjustable center region while retaining TAA in the periphery, with adjustable edge feathering.
- **Transformer Model** — Selects the neural model preset used by the DLAA/DLSS subsystem.
- **Motion Vector Repair** — Repairs motion-vector input used by the neural image-quality path.
- **Post Sharpening** — Adds optional sharpening with an adjustable strength.
- **Hard Reset On Load** — Resets image-quality history on loading transitions.

### Vanilla renderer fixes

- **Vanilla Fixes** — Independent master for the integrated native-renderer corrections. Changing this master requires a restart.
- **Precipitation Occlusion** — Enables native precipitation occlusion through the renderer-fix controls.
- **Image Space Modifiers** — Enables native image-space modifier processing through the renderer-fix controls.
- **Stereo SAO** — Provides the stereo-aware screen-space ambient-occlusion path. It runs whenever Screen Space GI, which replaces it, is off.
- **Stable Screen-Space Reflections** — Uses per-eye reflection traversal, geometric reflection rays, shared scene-radiance retention, and corrected cubemap fallback. Water uses separate shaders and is unaffected by this control.
- **Native Screen-Space Material Pipeline** — Controls Bethesda's shared skin-scattering and reflection image-space pipeline. This can affect SSR and dynamic cubemap capture; the separate Subsurface Scattering feature controls isolated skin diffusion.
- **VR Lens Flare** — Enables the native lens-flare path through the VR renderer fixes.
- **Focus Shadows** — Enables the integrated native focus-shadow path.
- **Stable Stereo Sun Occlusion** — Keeps a peripheral Sun visibility query from discarding valid visibility reported by another completed query.

### Configuration and diagnostics

- **Live Settings Reload** — Applies supported settings edits during play. Settings that require a restart are identified in the configuration section below.
- **CSAH Visual Suite** — Switches the visual suite independently of DLAA/DLSS, Vanilla Fixes, and Native Shadow Fixes.
- **GPU Performance Profiling** — Records GPU timing and CPU submission measurements for selected feature groups.
- **Exclusive Lighting Diagnostic** — Isolates lighting and reflection components for investigation while preserving saved feature settings.
- **Visualize Center Region** — Displays the center-DLAA region for adjustment.
- **Verbose Diagnostics** — Adds detailed diagnostics for the DLAA/DLSS subsystem.

## Requirements

- Fallout 4 VR **1.2.72.0** on Windows, launched through **F4SEVR 0.6.21**.
- A supported NVIDIA RTX GPU for the DLAA/DLSS subsystem. Its availability is checked through NVIDIA Streamline at runtime.
- Compatible authored textures for complex parallax or authored PBR materials. Ordinary texture replacements do not automatically provide those material channels.

## Installation

Install the mod ZIP through MO2 and launch the game. CSAH VR creates its settings automatically on first launch using the supplied gameplay preset. No manual config copying is needed.

1. Download a mod archive from [Releases](https://github.com/brunocatani/CSAH-VR/releases). GitHub's automatically generated **Source code** archives do not contain a built plugin.
2. Install the mod archive through Mod Organizer 2, keeping its directory structure. For a manual installation, place the archive's runtime folders under the game's `Data` directory.
3. Confirm that the installed payload includes:

   ```text
   Data/
     F4SE/Plugins/CSAH.dll
     F4SE/Plugins/Streamline/
     Shaders/
   ```

   Keep the bundled Streamline DLLs together and preserve the archive's folder structure.
4. Launch Fallout 4 VR through F4SEVR. The mod creates `CSAH.toml` automatically; edit that file or use RobCo PALM to configure its features as described below.

## Configuration

The active settings file is in your Windows Documents folder:

```text
Documents/My Games/Fallout4VR/Mods_Config/CSAH/CSAH.toml
```

Every start writes each missing option at its default, grouped by feature with its help above it. Booleans are `true` or `false`; your own notes are kept when they start with a single `#` on the lines above an option. A value of the wrong type is reported in `CSAH.log` and that option keeps its last value. RobCo PALM's CSAH page edits the same file. Most feature changes reload during play; **Native Shadows settings, Skylighting probe quality, and the Vanilla Fixes master switch require a game restart**.

The **CSAH Visual Suite** switch controls the lighting, material, and output effects. **DLAA/DLSS, Vanilla Fixes, and Native Shadows have independent switches** and keep their own state when the visual suite is disabled.

The first-run gameplay preset enables **Linear Lighting**, which the PBR pipeline requires. Cloud Shadows, Hair Specular, Wrapped Grass Lighting, Subsurface Scattering, and Volumetric Lighting / Godrays are disabled by default because they are not working. Quality settings are fixed choices: the mod does not automatically lower visual quality to meet an FPS target.

The first start after an update carries every value you changed in `Mods_Config/CSAH/CSAH.ini`, or in the older `Mods_Config/FO4VRCommunityShaders/FO4VRCommunityShaders.ini`, into `CSAH.toml` and deletes the INI. Values equal to their old defaults follow the current defaults. Section names stay the same and keys lose their type letter, for example `[DLAA] iMode` becomes `[DLAA] Mode`.

Linear Lighting's former `*Gamma` keys, `PreserveNativeDarkness`, `GlowmapMultiplier` and `EffectLightingMultiplier` decoded colours a second time on top of the game's own decode and are retired: CSAH turns them into comments, and their replacements (`*Response`, `GlowmapScale`, `EffectLightingScale`) start at values that match the game.

When upgrading from 0.0.3 Alpha, replace the entire old mod installation before installing CSAH. Do not enable both versions together; the old DLL was named `fo4vr-community-shaders.dll`.

## Authored PBR materials

Texture mods can register base-colour and RMAOS texture pairs through their own manifest:

```text
Data/F4SE/Plugins/CSAH/PBRMaterials/<mod-name>.json
```

Existing content packs must move their manifest folder from `F4SE/Plugins/FO4VRCommunityShaders/PBRMaterials` to `F4SE/Plugins/CSAH/PBRMaterials`. Texture paths and manifest contents stay the same.

```json
{
  "materials": [
    {
      "base": "textures/example/metal_d.dds",
      "rmaos": "textures/example/metal_rmaos.dds"
    }
  ]
}
```

RMAOS stores **roughness, metalness, ambient occlusion, and dielectric specular** in its R, G, B, and A channels. The base-colour texture is sRGB; RMAOS is linear data. The manifest and textures belong to the content mod. Restart the game after adding or changing a manifest. Authored transport requires both Linear Lighting and PBR to be enabled; materials without a matching entry retain the existing material path.

## Limitations and troubleshooting

**Runtime compatibility:** Testing reported so far used **OpenComposite**. **SteamVR is untested and has not been proven to work.**

**Reported performance on an NVIDIA GeForce RTX 4090:** With **Skylighting and Contact Shadows enabled**, performance dropped from **90 FPS to 70 FPS during regular gameplay**, and to **59 FPS in downtown Boston**. These observations are not a performance guarantee for other hardware or settings.

This mod is experimental. Lighting and reflection coverage, stereo artifacts, and compatibility with other rendering modifications still need testing across scenes and hardware. Skylighting, diffuse IBL, contact shadows, and glare can be expensive; choose settings for your headset resolution and GPU. Complex-material effects depend on the supplied assets, and authored PBR content still needs visual qualification.

When reporting a problem, include the mod version, GPU, headset, render resolution, enabled features, relevant settings, reproduction steps, and a screenshot or short video where useful. Attach the log from the affected run:

```text
Documents/My Games/Fallout4VR/F4SE/CSAH.log
```

For an F4SE loading problem, also include `f4sevr.log` from the same directory and game session.

## Building from source

The build requires **Visual Studio 2022 with the v143 C++ toolset**, a Windows SDK containing `fxc.exe`, **CMake 4.2 or newer**, Python 3, vcpkg, CommonLibF4VR, **RPS Framework** (built, so its staged CMake package exists under `build-fast/stage/lib/cmake/RPSFramework`; it reads and writes `CSAH.toml`), and the **NVIDIA Streamline 2.12.0 SDK** with its runtime DLLs and license files. Dependencies from vcpkg use the `x64-windows-static` triplet.

Clone this repository with its CommonLibF4VR submodule:

```powershell
git clone --recurse-submodules https://github.com/brunocatani/CSAH-VR.git
cd CSAH-VR
```

The checked-in presets expect vcpkg at `C:/vcpkg`. Adjust the toolchain and `VCPKG_ROOT` through a local preset if your installation differs. Point CMake at the initialized CommonLibF4VR checkout, RPS Framework's staged package and your Streamline SDK:

```powershell
$env:VCPKG_ROOT = "C:/vcpkg"
cmake --preset fast -DCOMMON_LIB_F4VR_PATH="$PWD/extern/CommonLibF4VR" -DSTREAMLINE_SDK_ROOT="C:/SDKs/streamline-sdk-v2.12.0" -DRPSFramework_DIR="C:/src/RPS_Framework/build-fast/stage/lib/cmake/RPSFramework"
cmake --build build-fast --config Release -- /m:1 /p:CL_MPCount=2
```

The build stages its runtime files under `package/`. The `fast` preset does not deploy to an installed game. Local deployment can be configured in the ignored `CMakeUserPresets.json` using `POST_BUILD_COPY_PLUGIN` and `COPY_PLUGIN_BASE_PATH`.

To build and run the existing tests:

```powershell
$env:VCPKG_ROOT = "C:/vcpkg"
cmake --preset tests -DCOMMON_LIB_F4VR_PATH="$PWD/extern/CommonLibF4VR" -DSTREAMLINE_SDK_ROOT="C:/SDKs/streamline-sdk-v2.12.0" -DRPSFramework_DIR="C:/src/RPS_Framework/build-fast/stage/lib/cmake/RPSFramework"
cmake --build build-tests --config Release -- /m:1 /p:CL_MPCount=2
ctest --test-dir build-tests -C Release -j 4 --output-on-failure
```

## Credits and license

Thanks to the Community Shaders and Open Shaders contributors, the F4SEVR and CommonLibF4VR maintainers, and the Fallout 4 VR modding community. NVIDIA Streamline supplies the DLAA/DLSS integration.

The project's original source and modifications are licensed under the **GNU General Public License, version 3 only (GPL-3.0-only)**. See [LICENSE](LICENSE) for the complete terms. Third-party code, assets, and NVIDIA runtime components retain their respective licenses and notices; the project license does not replace those terms.
