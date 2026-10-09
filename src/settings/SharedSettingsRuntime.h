#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"
#include "Features/lod_blending/LodBlendingSettings.h"
#include "Features/terrain_variation/TerrainVariationSettings.h"
#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "Features/water_effects/WaterEffectsSettings.h"
#include "Features/bloom_glare/BloomGlareSettings.h"
#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "Features/complex_materials/ComplexParallaxSettings.h"
#include "Features/contact_shadows/ContactShadowSettings.h"
#include "Features/dlaa/DlaaSettings.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"
#include "Features/hair_specular/HairSpecularSettings.h"
#include "Features/ibl/IblSettings.h"
#include "Features/linear_lighting/LinearLightingSettings.h"
#include "Features/native_shadows/NativeShadowSettings.h"
#include "Features/pbr/PbrSettings.h"
#include "Features/skylighting/SkylightingSettings.h"
#include "Features/sky_sync/SkySyncSettings.h"
#include "Features/subsurface_scattering/SubsurfaceScatteringSettings.h"
#include "Features/vanilla_fixes/VanillaFixesSettings.h"
#include "Features/volumetric_lighting/VolumetricLightingSettings.h"
#include "Features/wrapped_grass/WrappedGrassSettings.h"
#include "settings/DiagnosticsSettings.h"

#include <cstddef>

namespace csah::shared_settings
{
    struct Snapshot final
    {
        bool masterEnabled{ true };
        diagnostics_settings::Settings diagnostics{};
        linear_lighting::Settings linearLighting{};
        dlaa::Settings dlaa{};
        filmic_tonemapping::Settings filmicTonemapping{};
        bloom_glare::Settings bloomGlare{};
        ibl::Settings ibl{};
        complex_materials::Settings complexMaterials{};
        contact_shadows::Settings contactShadows{};
        wrapped_grass::Settings wrappedGrass{};
        hair_specular::Settings hairSpecular{};
        subsurface_scattering::Settings subsurfaceScattering{};
        basic_wetness::Settings basicWetness{};
        lod_blending::Settings lodBlending{};
        terrain_variation::Settings terrainVariation{};
        screen_space_gi::Settings screenSpaceGI{};
        water_effects::Settings waterEffects{};
        cloud_shadows::Settings cloudShadows{};
        volumetric_lighting::Settings volumetricLighting{};
        vanilla_fixes::Settings vanillaFixes{};
        native_shadows::Settings nativeShadows{};
        pbr::Settings pbr{};
        skylighting::Settings skylighting{};
        sky_sync::Settings skySync{};

        [[nodiscard]] bool operator==(const Snapshot&) const noexcept = default;
    };

    struct ChangeSet final
    {
        bool masterGate{};
        bool diagnostics{};
        bool linearLighting{};
        bool dlaa{};
        bool filmicTonemapping{};
        bool bloomGlare{};
        bool ibl{};
        bool complexMaterials{};
        bool contactShadows{};
        bool wrappedGrass{};
        bool hairSpecular{};
        bool subsurfaceScattering{};
        bool basicWetness{};
        bool lodBlending{};
        bool terrainVariation{};
        bool screenSpaceGI{};
        bool waterEffects{};
        bool cloudShadows{};
        bool volumetricLighting{};
        bool vanillaFixesGate{};
        bool vanillaFixes{};
        bool nativeShadows{};
        bool pbr{};
        bool skylighting{};
        bool skySync{};

        [[nodiscard]] bool any() const noexcept
        {
            return masterGate || diagnostics || vanillaFixesGate ||
                liveFeatureCount() != 0 || nativeShadows;
        }

        [[nodiscard]] std::size_t liveFeatureCount() const noexcept
        {
            return static_cast<std::size_t>(linearLighting) +
                static_cast<std::size_t>(diagnostics) +
                static_cast<std::size_t>(dlaa) +
                static_cast<std::size_t>(filmicTonemapping) +
                static_cast<std::size_t>(bloomGlare) +
                static_cast<std::size_t>(ibl) +
                static_cast<std::size_t>(complexMaterials) +
                static_cast<std::size_t>(contactShadows) +
                static_cast<std::size_t>(wrappedGrass) +
                static_cast<std::size_t>(hairSpecular) +
                static_cast<std::size_t>(subsurfaceScattering) +
                static_cast<std::size_t>(basicWetness) +
                static_cast<std::size_t>(lodBlending) +
                static_cast<std::size_t>(terrainVariation) +
                static_cast<std::size_t>(screenSpaceGI) +
                static_cast<std::size_t>(waterEffects) +
                static_cast<std::size_t>(cloudShadows) +
                static_cast<std::size_t>(volumetricLighting) +
                static_cast<std::size_t>(vanillaFixes) +
                static_cast<std::size_t>(pbr) +
                static_cast<std::size_t>(skylighting) +
                static_cast<std::size_t>(skySync);
        }
    };

    [[nodiscard]] inline ChangeSet diff(
        const Snapshot& previous,
        const Snapshot& next) noexcept
    {
        return {
            .masterGate = previous.masterEnabled != next.masterEnabled,
            .diagnostics = previous.diagnostics != next.diagnostics,
            .linearLighting =
                previous.linearLighting != next.linearLighting,
            .dlaa = previous.dlaa != next.dlaa,
            .filmicTonemapping = previous.filmicTonemapping !=
                next.filmicTonemapping,
            .bloomGlare = previous.bloomGlare != next.bloomGlare,
            .ibl = previous.ibl != next.ibl,
            .complexMaterials =
                previous.complexMaterials != next.complexMaterials,
            .contactShadows =
                previous.contactShadows != next.contactShadows,
            .wrappedGrass = previous.wrappedGrass != next.wrappedGrass,
            .hairSpecular = previous.hairSpecular != next.hairSpecular,
            .subsurfaceScattering = previous.subsurfaceScattering !=
                next.subsurfaceScattering,
            .basicWetness = previous.basicWetness != next.basicWetness,
            .lodBlending = previous.lodBlending != next.lodBlending,
            .terrainVariation = previous.terrainVariation != next.terrainVariation,
            .screenSpaceGI = previous.screenSpaceGI != next.screenSpaceGI,
            .waterEffects = previous.waterEffects != next.waterEffects,
            .cloudShadows = previous.cloudShadows != next.cloudShadows,
            .volumetricLighting = previous.volumetricLighting !=
                next.volumetricLighting,
            .vanillaFixesGate = previous.vanillaFixes.enabled !=
                next.vanillaFixes.enabled,
            // Vanilla Fixes is an independent startup-owned suite. Its
            // individual policy can change live while its own master remains
            // active, regardless of the CSAH visual-suite gate.
            // Never dismantle the active SAO/SSLR renderer graph mid-session.
            .vanillaFixes = previous.vanillaFixes.enabled &&
                next.vanillaFixes.enabled &&
                previous.vanillaFixes != next.vanillaFixes,
            .nativeShadows = previous.nativeShadows != next.nativeShadows,
            .pbr = previous.pbr != next.pbr,
            .skylighting = previous.skylighting != next.skylighting,
            .skySync = previous.skySync != next.skySync,
        };
    }

    // Starts one bounded monitor for the shared CSAH INI. The
    // controller is process-lifetime owned, performs no work on render hooks,
    // and coalesces multi-key writers before publishing settings.
    [[nodiscard]] bool startMonitor() noexcept;
}
