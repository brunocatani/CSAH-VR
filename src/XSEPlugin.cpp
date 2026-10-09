#include "Features/inverse_square/InverseSquareRuntime.h"
#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "Features/water_effects/WaterEffectsRuntime.h"
#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "PCH.h"

#include "Features/basic_wetness/BasicWetnessRuntime.h"
#include "Features/lod_blending/LodBlendingRuntime.h"
#include "Features/terrain_variation/TerrainVariationRuntime.h"
#include "Features/lod_blending/LodBlendingSettingsStore.h"
#include "Features/basic_wetness/BasicWetnessSettingsStore.h"
#include "Features/bloom_glare/BloomGlareRuntime.h"
#include "Features/bloom_glare/BloomGlareSettingsStore.h"
#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "Features/cloud_shadows/CloudShadowSettingsStore.h"
#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "Features/dlaa/DlaaD3D11Hooks.h"
#include "Features/dlaa/DlaaEngineHooks.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/dlaa/DlaaSettingsStore.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettingsStore.h"
#include "Features/hair_specular/HairSpecularRuntime.h"
#include "Features/hair_specular/HairSpecularSettingsStore.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/ibl/IblSettingsStore.h"
#include "Features/complex_materials/ComplexParallaxSettingsStore.h"
#include "Features/linear_lighting/DFTiledPointLightHook.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/linear_lighting/LinearLightingSettingsStore.h"
#include "Features/native_shadows/NativeShadowRuntime.h"
#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "Features/pbr/PbrRuntime.h"
#include "Features/pbr/PbrSettingsStore.h"
#include "Features/skylighting/SkylightingNativeHooks.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/skylighting/SkylightingSettingsStore.h"
#include "Features/sky_sync/SkySyncRuntime.h"
#include "Features/sky_sync/SkySyncSettingsStore.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringRuntime.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettingsStore.h"
#include "Features/vanilla_fixes/VanillaFixesRuntime.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/volumetric_lighting/VolumetricLightingRuntime.h"
#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"
#include "Features/wrapped_grass/WrappedGrassRuntime.h"
#include "Features/wrapped_grass/WrappedGrassSettingsStore.h"
#include "diagnostics/HdrOutputProbe.h"
#include "diagnostics/LinearLightingQualification.h"
#include "render/BSDFPrePassShaderHook.h"
#include "render/BSLightingGeometryHook.h"
#include "render/D3D11Hooks.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"
#include "settings/SharedSettingsRuntime.h"

#include <MinHook.h>

extern "C" __declspec(dllexport) constinit F4SE::PluginVersionData F4SEPlugin_Version = []() noexcept {
    F4SE::PluginVersionData version{};
    version.PluginName("Community Shaders at Home (CSAH) VR");
    version.PluginVersion(REL::Version(0, 0, 6));
    version.AuthorName("CSAH contributors");
    return version;
}();

namespace
{
    [[nodiscard]] bool ensureSkylightingNativeHooks(
        const char* trigger) noexcept
    {
        if (csah::skylighting::validateNativeHooks(trigger)) {
            return true;
        }
        if (!csah::skylighting::installNativeHooks()) {
            return false;
        }
        return csah::skylighting::validateNativeHooks(trigger);
    }

    void reportPluginBoundaryFailure(
        const char* boundary,
        const char* detail) noexcept
    {
        char message[1024]{};
        if (detail) {
            std::snprintf(
                message,
                sizeof(message),
                "Community Shaders at Home (CSAH) VR: %s failed: %s\n",
                boundary,
                detail);
        } else {
            std::snprintf(
                message,
                sizeof(message),
                "Community Shaders at Home (CSAH) VR: %s failed with an unknown exception.\n",
                boundary);
        }
        OutputDebugStringA(message);
        csah::logging::critical("{}", message);
    }

    void F4SEAPI onF4SEMessage(
        F4SE::MessagingInterface::Message* message) noexcept
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case F4SE::MessagingInterface::kPostPostLoad:
        {
            const auto d3d = csah::render::d3d11HookSnapshot();
            const auto dlaa = csah::dlaa::Runtime::get().snapshot();
            csah::logging::info(
                "F4SE PostPostLoad: D3D importHook={}, deviceCaptured={}, deviceHooks={}, createCalls={}; DLAA requested={}, Streamline initialized={}.",
                d3d.deviceCreationImportInstalled,
                d3d.deviceCaptured,
                d3d.deviceHooksInstalled,
                d3d.deviceCreationCalls,
                dlaa.settings.enabled,
                dlaa.streamline.initialized);
            break;
        }
        case F4SE::MessagingInterface::kGameDataReady:
        {
            csah::native_shadows::onGameDataReady();
            csah::vanilla_fixes::onGameDataReady();
            csah::volumetric_lighting::Runtime::get()
                .onGameDataReady();
            csah::pbr::Runtime::get().onGameDataReady();
            csah::ibl::Runtime::get().loadMaterialReference();
            (void)csah::render::
                validateD3D11ShaderHooks("GameDataReady");
            (void)csah::render::
                validateBSLightingGeometryHook("GameDataReady");
            (void)csah::render::
                validateBSDFPrePassShaderHook("GameDataReady");
            (void)csah::linear_lighting::
                validateDFTiledPointLightHook("GameDataReady");
            (void)ensureSkylightingNativeHooks(
                "GameDataReady");
            (void)csah::dlaa::validateEngineHooks(
                "GameDataReady");
            (void)csah::dlaa::validateD3D11Hooks(
                "GameDataReady");
            const auto linearLighting =
                csah::linear_lighting::Runtime::get().snapshot();
            const auto geometry =
                csah::render::geometryHookSnapshot();
            const auto d3d =
                csah::render::d3d11HookSnapshot();
            const auto pointLight = csah::linear_lighting::
                dFTiledPointLightHookSnapshot();
            const auto dlaa =
                csah::dlaa::Runtime::get().snapshot();
            const auto dlaaEngine =
                csah::dlaa::engineHookSnapshot();
            const auto dlaaD3d =
                csah::dlaa::d3d11HookSnapshot();
            const auto skylighting =
                csah::skylighting::Runtime::get().snapshot();
            csah::logging::info(
                "F4SE GameDataReady: Linear Lighting enabled={}, gpuReady={}, geometryReady={}, matchingShaders={}, trackedShaders={}, grassVsMask=0x{:02X}, grassVsCreated={}, grassVsTracked={}, grassClassSelections={}, ambientContractMask=0x{:010X}, ambientReadyMask=0x{:010X}, ambientShaders={}, ambientTracked={}, ambientBuilds={}, ambientBuildFailures={}, psBindCalls={}, shaderSelections={}, replacementBinds={}, ambientReplacementBinds={}, d3dBindDetourEnabled={}, techniqueCellOwned={}, geometryCellOwned={}, dFLightProducerOwned={}, techniqueCalls={}, geometryCalls={}, geometryUpdates={}, geometrySourceRejects={}, deepestGeometrySourceStage={}, ambientDescriptors={}, directionalDescriptors={}, ambientTransformPrepared={}, directionalPowModified={}, pointDetourOwned={}, pointGammaLoadsOwned={}, pointCalls={}, pointModified={}, pointGamma={}, pointMultiplier={}.",
                linearLighting.enabled,
                linearLighting.gpuResourcesReady,
                linearLighting.geometryProviderReady,
                linearLighting.matchingShadersCreated,
                linearLighting.trackedOriginalShaders,
                linearLighting.matchingGrassVertexShaderIdentityMask,
                linearLighting.matchingGrassVertexShadersCreated,
                linearLighting.trackedGrassVertexShaders,
                linearLighting.grassVertexClassSelections,
                linearLighting.matchingDFLightAmbientContractMask,
                linearLighting.readyDFLightAmbientContractMask,
                linearLighting.matchingDFLightAmbientShaders,
                linearLighting.trackedDFLightAmbientShaders,
                linearLighting.dFLightAmbientReplacementBuilds,
                linearLighting.dFLightAmbientReplacementFailures,
                d3d.pixelShaderBindCalls,
                linearLighting.shaderSelectionCalls,
                linearLighting.replacementBinds,
                linearLighting.dFLightAmbientReplacementBinds,
                d3d.pixelShaderBindDetourEnabled,
                geometry.techniqueVtableCellOwned,
                geometry.geometryVtableCellOwned,
                geometry.dFLightProducerCallsitesOwned,
                geometry.techniqueCalls,
                geometry.calls,
                geometry.acceptedUpdates,
                geometry.rejectedSources,
                static_cast<std::uint32_t>(geometry.deepestStage),
                geometry.ambientDescriptors,
                geometry.directionalDescriptors,
                geometry.ambientTransformPrepared,
                geometry.directionalPowModified,
                pointLight.detourOwned,
                pointLight.gammaLoadsOwned,
                pointLight.completedCalls,
                pointLight.modifiedCalls,
                pointLight.activeGamma,
                pointLight.activeColorMultiplier);
            csah::logging::info(
                "F4SE GameDataReady: DLAA requested={}, operational={}, engineHooksOwned={}, mapUnmapOwned={}, cameraQualified={}, cameraBinding={}, renderResourcesQualified={}, Streamline initialized={}, deviceBound={}, swapchainUpgraded={}, DLSS loaded={}, supported={}, functionsBound={}, cameraFrames={}, containingMaps={}, identityMatches={}, validationFailures={}, preCalls={}, postCalls={}, stereoEvaluations={}, evaluationFailures={}, committedFrames={}.",
                dlaa.settings.enabled,
                dlaa.operational,
                dlaaEngine.owned,
                dlaaD3d.owned,
                dlaa.cameraBufferQualified,
                dlaa.cameraBindingObserved,
                dlaa.renderResourcesQualified,
                dlaa.streamline.initialized,
                dlaa.streamline.deviceBound,
                dlaa.streamline.swapChainUpgraded,
                dlaa.streamline.featureLoaded,
                dlaa.streamline.featureSupported,
                dlaa.streamline.featureFunctionsBound,
                dlaa.mappedCameraFrames,
                dlaa.cameraMapCandidates,
                dlaa.cameraIdentityMatches,
                dlaa.cameraValidationFailures,
                dlaa.preRenderCalls,
                dlaa.postRenderCalls,
                dlaa.stereoEvaluations,
                dlaa.stereoEvaluationFailures,
                dlaa.committedFrames);
            csah::logging::info(
                "F4SE GameDataReady: Skylighting requested={}, gpuReady={}, nativeHookOwned={}, exteriorActive={}, privateDepthReady={}, probeValid={}, quality={} ({}x{}x{}), captures={}, depthBinds={}, dispatches={}, rejectedCaptures={}, ambientBinds={}.",
                skylighting.requested,
                skylighting.gpuResourcesReady,
                skylighting.nativeHookOwned,
                skylighting.exteriorActive,
                skylighting.privateDepthReady,
                skylighting.probeDataValid,
                csah::skylighting::qualityName(
                    skylighting.activeQuality),
                skylighting.probeWidth,
                skylighting.probeHeight,
                skylighting.probeDepth,
                skylighting.captureCalls,
                skylighting.privateDepthBinds,
                skylighting.probeDispatches,
                skylighting.rejectedCaptures,
                skylighting.ambientBinds);
            const auto volumetric = csah::volumetric_lighting::
                Runtime::get().snapshot();
            csah::logging::info(
                "F4SE GameDataReady: Volumetric Lighting enabled={}, nativeContract={}, engineData={}, gpuReady={}, diagnosticSuppressed={}, hostObserved={}, outputQualified={}, directionalCaptures={}, renderedFrames={}, rejectedFrames={}, qualification=[passes={},failures={}].",
                volumetric.settings.enabled,
                volumetric.nativeContractValid,
                volumetric.engineDataValid,
                volumetric.gpuReady,
                volumetric.diagnosticSuppressed,
                volumetric.hostShaderObserved,
                volumetric.outputQualified,
                volumetric.directionalCaptures,
                volumetric.renderedFrames,
                volumetric.rejectedFrames,
                volumetric.qualificationPasses,
                volumetric.qualificationFailures);
            break;
        }
        case F4SE::MessagingInterface::kPreLoadGame:
            csah::sky_sync::Runtime::get().onWorldEnding();
            csah::screen_space_gi::Runtime::get().resetHistory();
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
            csah::native_shadows::onWorldReady("PostLoadGame");
            csah::sky_sync::Runtime::get().onWorldReady(
                "PostLoadGame");
            (void)ensureSkylightingNativeHooks("PostLoadGame");
            (void)csah::render::
                validateD3D11ShaderHooks("GameSessionReady");
            csah::ibl::Runtime::get()
                .beginWorldCaptureProbeSession();
            csah::skylighting::Runtime::get()
                .beginWorldSession();
            csah::diagnostics::
                beginLinearLightingQualificationSession("PostLoadGame");
            csah::dlaa::Runtime::get().beginQualificationSession(
                "PostLoadGame");
            break;
        case F4SE::MessagingInterface::kNewGame:
            csah::native_shadows::onWorldReady("NewGame");
            csah::sky_sync::Runtime::get().onWorldReady(
                "NewGame");
            (void)ensureSkylightingNativeHooks("NewGame");
            (void)csah::render::
                validateD3D11ShaderHooks("GameSessionReady");
            csah::ibl::Runtime::get()
                .beginWorldCaptureProbeSession();
            csah::skylighting::Runtime::get()
                .beginWorldSession();
            csah::diagnostics::
                beginLinearLightingQualificationSession("NewGame");
            csah::dlaa::Runtime::get().beginQualificationSession(
                "NewGame");
            break;
        default:
            break;
        }
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
    const F4SE::QueryInterface* a_f4se,
    F4SE::PluginInfo* a_info) noexcept
{
    try {
        if (!a_f4se || !a_info) {
            return false;
        }

        csah::logging::init();
        csah::logging::info(
            "=== Community Shaders at Home (CSAH) VR v0.0.6 query ===");

        a_info->infoVersion = F4SE::PluginInfo::kVersion;
        a_info->name = "Community Shaders at Home (CSAH) VR";
        a_info->version = 6;

        if (a_f4se->IsEditor()) {
            csah::logging::critical(
                "Editor runtime is unsupported.");
            return false;
        }
        if (!REL::Module::IsVR()) {
            csah::logging::critical(
                "Fallout 4 VR runtime is required.");
            return false;
        }

        // F4SEVR exposes its flat-compatible loader API version here. This is
        // a different version domain from Fallout4VR.exe itself.
        const auto requiredRuntime = F4SE::RUNTIME_1_10_138;
        if (a_f4se->RuntimeVersion() < requiredRuntime) {
            csah::logging::critical(
                "Unsupported F4SE compatibility runtime {} (need >= {}).",
                a_f4se->RuntimeVersion().string(),
                requiredRuntime.string());
            return false;
        }

        const auto executableVersion = REL::Module::get().version();
        if (executableVersion != F4SE::RUNTIME_VR_1_2_72) {
            csah::logging::critical(
                "Only Fallout4VR.exe 1.2.72 is supported; executable={}, F4SE compatibility runtime={}.",
                executableVersion.string(),
                a_f4se->RuntimeVersion().string());
            return false;
        }

        csah::logging::info(
            "Runtime gate passed: Fallout4VR.exe {}, F4SE compatibility runtime {}.",
            executableVersion.string(),
            a_f4se->RuntimeVersion().string());
        return true;
    } catch (const std::exception& error) {
        reportPluginBoundaryFailure("F4SEPlugin_Query", error.what());
        return false;
    } catch (...) {
        reportPluginBoundaryFailure("F4SEPlugin_Query", nullptr);
        return false;
    }
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(
    const F4SE::LoadInterface* a_f4se) noexcept
{
    try {
        if (!a_f4se) {
            return false;
        }
        F4SE::Init(a_f4se, false);

        const auto iniPath = csah::settings_path::resolveIniPath();
        std::error_code migrationError;
        const auto migration = csah::settings_path::initializeIni(
            iniPath, migrationError);
        if (migration == csah::settings_path::SetupResult::failed) {
            csah::logging::critical(
                "CSAH VR settings setup failed for '{}': {}. Existing settings were not replaced; plugin remains unloaded.",
                iniPath.string(), migrationError.message());
            return false;
        }
        if (migration == csah::settings_path::SetupResult::migrated) {
            csah::logging::info(
                "Migrated existing settings to '{}'; values and formatting preserved.",
                iniPath.string());
        }
        if (migration == csah::settings_path::SetupResult::created) {
            csah::logging::info(
                "Created first-run CSAH VR settings at '{}'.", iniPath.string());
        }

        const auto minHookStatus = MH_Initialize();
        if (minHookStatus != MH_OK &&
            minHookStatus != MH_ERROR_ALREADY_INITIALIZED) {
            csah::logging::critical(
                "Shared native-hook runtime initialization failed (MinHook={}); plugin remains unloaded.",
                static_cast<int>(minHookStatus));
            return false;
        }
        csah::logging::info(
            "Shared native-hook runtime initialized before feature ownership begins.");

        const auto settings =
            csah::linear_lighting::loadSettings();
        const auto dlaaSettings = csah::dlaa::loadSettings();
        const auto filmicTonemappingSettings =
            csah::filmic_tonemapping::loadSettings();
        const auto bloomGlareSettings =
            csah::bloom_glare::loadSettings();
        const auto iblSettings = csah::ibl::loadSettings();
        const auto contactShadowSettings =
            csah::contact_shadows::loadSettings();
        const auto complexMaterialSettings =
            csah::complex_materials::loadSettings();
        const auto wrappedGrassSettings =
            csah::wrapped_grass::loadSettings();
        const auto hairSpecularSettings =
            csah::hair_specular::loadSettings();
        const auto subsurfaceScatteringSettings =
            csah::subsurface_scattering::loadSettings();
        const auto terrainVariationSettings=csah::terrain_variation::loadSettings();
        const auto screenSpaceGISettings=csah::screen_space_gi::loadSettings();
        csah::terrain_shadows::Runtime::get().loadMaps("Data/F4SE/Plugins/CSAH/HeightMaps");
        csah::terrain_shadows::Runtime::get().applySettings(csah::terrain_shadows::loadSettings());
        csah::inverse_square::Runtime::get().applySettings(csah::inverse_square::loadSettings());
        const auto waterEffectsSettings=csah::water_effects::loadSettings();
        const auto lodBlendingSettings = csah::lod_blending::loadSettings();
        const auto basicWetnessSettings =
            csah::basic_wetness::loadSettings();
        const auto cloudShadowSettings =
            csah::cloud_shadows::loadSettings();
        const auto volumetricLightingSettings =
            csah::volumetric_lighting::loadSettings();
        const auto vanillaFixesSettings =
            csah::vanilla_fixes::loadSettings();
        const auto nativeShadowSettings =
            csah::native_shadows::loadSettings();
        const auto pbrSettings = csah::pbr::loadSettings();
        const auto skylightingSettings =
            csah::skylighting::loadSettings();
        const auto skySyncSettings =
            csah::sky_sync::loadSettings();
        csah::linear_lighting::Runtime::get().applySettings(
            settings);
        csah::pbr::Runtime::get().setLinearLightingEnabled(
            settings.enabled);
        csah::dlaa::Runtime::get().applySettings(dlaaSettings);
        csah::filmic_tonemapping::Runtime::get().applySettings(
            filmicTonemappingSettings);
        csah::bloom_glare::Runtime::get().applySettings(
            bloomGlareSettings);
        csah::linear_lighting::Runtime::get().
            applyComplexParallaxSettings(complexMaterialSettings);
        csah::ibl::Runtime::get().applySettings(iblSettings);
        csah::contact_shadows::Runtime::get().applySettings(
            contactShadowSettings);
        csah::cloud_shadows::Runtime::get().applySettings(
            cloudShadowSettings);
        if (!csah::volumetric_lighting::Runtime::get().start(
                volumetricLightingSettings,
                vanillaFixesSettings.enabled &&
                    vanillaFixesSettings.directionalLightDiagnosticMode !=
                        csah::vanilla_fixes::
                            DirectionalLightDiagnosticMode::off)) {
            csah::logging::warn(
                "Volumetric Lighting rejected an exact FO4VR host, camera, or weather contract; the feature remains fail-closed.");
        }
        csah::wrapped_grass::Runtime::get().applySettings(
            wrappedGrassSettings);
        csah::hair_specular::Runtime::get().applySettings(
            hairSpecularSettings);
        csah::subsurface_scattering::Runtime::get().applySettings(
            subsurfaceScatteringSettings);
        csah::lod_blending::Runtime::get().applySettings(lodBlendingSettings);
        csah::terrain_variation::Runtime::get().applySettings(terrainVariationSettings);
        csah::screen_space_gi::Runtime::get().applySettings(screenSpaceGISettings);
        csah::water_effects::Runtime::get().applySettings(waterEffectsSettings);
        csah::basic_wetness::Runtime::get().applySettings(
            basicWetnessSettings);
        csah::pbr::Runtime::get().applySettings(pbrSettings);
        csah::skylighting::Runtime::get().applySettings(
            skylightingSettings);
        csah::sky_sync::Runtime::get().applySettings(
            skySyncSettings);
        if (!csah::native_shadows::startRuntime(
                nativeShadowSettings)) {
            csah::logging::warn(
                "Native Shadows rejected one or more FO4VR engine contracts; unknown boundaries remain vanilla and extended-cascade masks remain fail-closed.");
        }
        if (!csah::vanilla_fixes::startRuntime(
                vanillaFixesSettings)) {
            csah::logging::warn(
                "Vanilla Fixes engine-gate contract was rejected; shader fixes remain available, but engine-gate ownership stays fail-closed.");
        }
        if (!csah::render::installEarlyD3D11Hooks()) {
            csah::logging::warn(
                "Verified D3D11 bootstrap was not installed; plugin remains loaded but all rendering stays vanilla.");
        }
        if (!csah::diagnostics::hdr_output_probe::install()) {
            csah::logging::warn(
                "Verified FO4VR HDR output ownership probe was not installed; Filmic/Bloom/Glare diagnosis remains fail-closed.");
        }
        if (!csah::dlaa::installEngineHooks()) {
            csah::logging::warn(
                "Verified FO4VR DLAA engine boundaries were not installed; vanilla TAA remains active.");
        }
        if (!csah::render::installBSLightingGeometryHook()) {
            csah::logging::warn(
                "Verified BSDF lighting geometry hook was not installed; Linear Lighting replacement remains fail-closed.");
        }
        if (!csah::render::installBSDFPrePassShaderHook()) {
            csah::logging::warn(
                "Verified BSDFPrePass descriptor hook was not installed; complex environment materials remain fail-closed.");
        }

        const auto* messaging = F4SE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(onF4SEMessage)) {
            csah::logging::critical(
                "F4SE messaging registration failed.");
            return false;
        }
        csah::diagnostics::
            startLinearLightingQualificationReporter();
        if (!csah::shared_settings::startMonitor()) {
            csah::logging::warn(
                "Shared CSAH INI monitor did not start; startup settings remain active, but RobCo PALM changes require the next launch.");
        }

        csah::logging::info(
            "Community Shaders at Home (CSAH) VR loaded; persisted upscaling enabled={}, mode={}, modelPreset={}, sharpening={}, sharpness={}; Linear Lighting enabled={}, Image Based Lighting enabled={}, Dynamic Cubemaps enabled={}, diffuse IBL enabled={}, diffuse level={}, PBR enabled={}, direct GGX={}, environment Fresnel={}, Skylighting enabled={}, quality={}, Contact Shadows enabled={}, samples={}, Wrapped Grass Lighting enabled={}, wrap amount={}, Hair Specular enabled={}, multiplier={}, Subsurface Scattering enabled={}, strength={}, Basic Wetness enabled={}, wetness={}, Cloud Shadows enabled={}, opacity={}, Volumetric Lighting enabled={}, quality={}, base={}, shafts={}, complex parallax enabled={}, parallax quality={}, Native Shadows enabled={}, four cascades={}, tiled deferred lighting={}, fixed shadow distance={}, Vanilla Fixes enabled={}, focus shadows={}, and replacements remain fail-closed until their verified render providers are ready.",
            dlaaSettings.enabled,
            csah::dlaa::modeName(dlaaSettings.mode),
            static_cast<std::uint32_t>(dlaaSettings.modelPreset),
            dlaaSettings.sharpening,
            dlaaSettings.sharpness,
            settings.enabled,
            iblSettings.enabled,
            iblSettings.dynamicCubemapsEnabled,
            iblSettings.diffuseEnabled,
            iblSettings.diffuseLevel,
            pbrSettings.enabled,
            pbrSettings.directGgx,
            pbrSettings.environmentFresnel,
            skylightingSettings.enabled,
            csah::skylighting::qualityName(
                skylightingSettings.quality),
            contactShadowSettings.enabled,
            contactShadowSettings.sampleCount,
            wrappedGrassSettings.enabled,
            wrappedGrassSettings.wrapAmount,
            hairSpecularSettings.enabled,
            hairSpecularSettings.specularMultiplier,
            subsurfaceScatteringSettings.enabled,
            subsurfaceScatteringSettings.strength,
            basicWetnessSettings.enabled,
            basicWetnessSettings.wetness,
            cloudShadowSettings.enabled,
            cloudShadowSettings.opacity,
            volumetricLightingSettings.enabled,
            volumetricLightingSettings.quality,
            volumetricLightingSettings.baseScattering,
            volumetricLightingSettings.shaftIntensity,
            complexMaterialSettings.parallaxEnabled,
            complexMaterialSettings.parallaxQuality,
            nativeShadowSettings.enabled,
            nativeShadowSettings.extendedDirectionalCascades,
            nativeShadowSettings.tiledDeferredLighting,
            nativeShadowSettings.directionalShadowDistance,
            vanillaFixesSettings.enabled,
            vanillaFixesSettings.focusShadows);
        return true;
    } catch (const std::exception& error) {
        reportPluginBoundaryFailure("F4SEPlugin_Load", error.what());
        return false;
    } catch (...) {
        reportPluginBoundaryFailure("F4SEPlugin_Load", nullptr);
        return false;
    }
}
