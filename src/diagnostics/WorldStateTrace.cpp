#include "PCH.h"

#pragma push_macro("MEM_RELEASE")
#pragma push_macro("MAX_PATH")
#pragma push_macro("near")
#pragma push_macro("far")
#undef MEM_RELEASE
#undef MAX_PATH
#undef near
#undef far
#include <RE/Fallout.h>
#pragma pop_macro("far")
#pragma pop_macro("near")
#pragma pop_macro("MAX_PATH")
#pragma pop_macro("MEM_RELEASE")

#include "diagnostics/WorldStateTrace.h"

#include "Features/contact_shadows/ContactShadowRuntime.h"
#include "Features/dlaa/DlaaRuntime.h"
#include "Features/ibl/IblRuntime.h"
#include "Features/linear_lighting/LinearLightingRuntime.h"
#include "Features/pbr/PbrRuntime.h"
#include "Features/screen_space_gi/ScreenSpaceGIRuntime.h"
#include "Features/sky_sync/SkySyncRuntime.h"
#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/surface_classification/SurfaceClassificationRuntime.h"
#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "Features/volumetric_lighting/VolumetricLightingRuntime.h"
#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

namespace csah::diagnostics
{
    namespace
    {
        // Settle long enough for per-cell feature setup (IBL capture, probe
        // fills) to finish; request the Sun two seconds earlier so the next
        // Sky::Update has measured it when the settled line is written.
        constexpr std::uint64_t kSettleMilliseconds = 10000;
        constexpr std::uint64_t kSunRequestMilliseconds = 8000;
        constexpr std::uint64_t kMinimumStayMilliseconds = 1000;

        struct Counters
        {
            std::uint64_t skyUpdates{};
            std::uint64_t shaderSelections{};
            std::uint64_t materialReplacements{};
            std::uint64_t ambientReplacements{};
            std::uint64_t inactiveSelections{};
            std::uint64_t unmatchedSelections{};
            std::uint64_t complexEnvironment{};
            std::uint64_t complexEnvironmentRejects{};
            std::uint64_t descriptorMisses{};
            std::uint64_t descriptorContractMisses{};
            std::uint64_t pbrScopes{};
            std::uint64_t pbrFallbacks{};
            std::uint64_t pbrFailures{};
            std::uint64_t giScopes{};
            std::uint64_t giPrepared{};
            std::uint64_t giRadiance{};
            std::array<std::uint64_t, screen_space_gi::kRejectionCount> giRejections{};
            std::uint64_t skylightingCaptures{};
            std::uint64_t skylightingRejects{};
            std::uint64_t skylightingDispatches{};
            std::uint64_t skylightingAmbient{};
            std::uint64_t volumetricRendered{};
            std::uint64_t volumetricRejected{};
            std::uint64_t contactScopes{};
            std::uint64_t contactFallbacks{};
            std::uint64_t iblMaterial{};
            std::uint64_t iblFitsPublished{};
            std::uint64_t iblFitsRejected{};
            std::uint64_t dlaaCommitted{};
            std::uint64_t dlaaFailures{};
        };

        // Owned by the render owner thread that calls onWorldStateFrame.
        struct TraceState
        {
            const RE::TESObjectCELL* cell{};
            std::uint32_t cellFormId{};
            std::uint64_t sequence{};
            std::uint64_t enteredAt{};
            bool sunRequested{};
            std::uint64_t sunGenerationAtRequest{};
            bool settled{};
            Counters baseline{};
        };
        TraceState state;

        [[nodiscard]] Counters capture() noexcept
        {
            const auto lighting = linear_lighting::Runtime::get().snapshot();
            const auto classification = surface_classification::Runtime::get().snapshot();
            const auto pbr = pbr::Runtime::get().snapshot();
            const auto gi = screen_space_gi::Runtime::get().snapshot();
            const auto skylighting = skylighting::Runtime::get().snapshot();
            const auto volumetric = volumetric_lighting::Runtime::get().snapshot();
            const auto contact = contact_shadows::Runtime::get().snapshot();
            const auto ibl = ibl::Runtime::get().snapshot();
            const auto dlaa = dlaa::Runtime::get().snapshot();
            const auto sky = sky_sync::Runtime::get().snapshot();
            return {
                .skyUpdates = sky.skyUpdates,
                .shaderSelections = lighting.shaderSelectionCalls,
                .materialReplacements = lighting.replacementBinds,
                .ambientReplacements = lighting.dFLightAmbientReplacementBinds,
                .inactiveSelections = lighting.inactiveShaderSelections,
                .unmatchedSelections = lighting.unmatchedShaderSelections,
                .complexEnvironment = lighting.complexEnvironmentReplacementBinds,
                .complexEnvironmentRejects = lighting.complexEnvironmentDescriptorRejects,
                .descriptorMisses = classification.descriptorObservationMisses,
                .descriptorContractMisses = classification.descriptorContractMisses,
                .pbrScopes = pbr.drawScopes,
                .pbrFallbacks = pbr.drawFallbacks,
                .pbrFailures = pbr.failures,
                .giScopes = gi.ambientScopes,
                .giPrepared = gi.preparedFrames,
                .giRadiance = gi.radianceCaptures,
                .giRejections = gi.rejections,
                .skylightingCaptures = skylighting.captureCalls,
                .skylightingRejects = skylighting.rejectedCaptures,
                .skylightingDispatches = skylighting.probeDispatches,
                .skylightingAmbient = skylighting.ambientBinds,
                .volumetricRendered = volumetric.renderedFrames,
                .volumetricRejected = volumetric.rejectedFrames,
                .contactScopes = contact.drawScopes,
                .contactFallbacks = contact.drawFallbacks,
                .iblMaterial = ibl.materialReplacementBinds,
                .iblFitsPublished = ibl.diffuseFitsPublished,
                .iblFitsRejected = ibl.diffuseFitsRejected,
                .dlaaCommitted = dlaa.committedFrames,
                .dlaaFailures = dlaa.stereoEvaluationFailures,
            };
        }

        [[nodiscard]] std::string describeLocation(const RE::TESObjectCELL* cell)
        {
            std::array<char, 256> worldspace{};
            bool tesInterior = false;
            const bool worldRead = terrain_shadows::Runtime::readWorldspace(worldspace, tesInterior);
            const char* name = static_cast<const RE::TESFullName*>(cell)->GetFullName();
            const auto map = terrain_shadows::Runtime::get().selectedMapName();
            return std::format(
                "cell=0x{:08X} '{}' {} worldspace='{}'{} terrainHeightmap='{}'",
                cell->GetFormID(),
                name ? name : "",
                cell->IsInterior() ? "interior" : "exterior",
                worldspace.data(),
                worldRead ? "" : " (unread)",
                map.empty() ? std::string_view("none") : map);
        }

        [[nodiscard]] std::string describeSun(std::uint64_t requestedAfterGeneration)
        {
            const auto sun = sky_sync::Runtime::get().sunMeasurement();
            if (sun.generation <= requestedAfterGeneration) {
                return "sun=unmeasured (no Sky::Update since the request)";
            }
            std::string result = std::format(
                "sun disc={} light={}",
                sun.discValid ?
                    std::format("[{:.3f},{:.3f},{:.3f}]", sun.discDirection[0], sun.discDirection[1], sun.discDirection[2]) :
                    std::string("invalid"),
                sun.lightValid ?
                    std::format("[{:.3f},{:.3f},{:.3f}]", sun.lightDirection[0], sun.lightDirection[1], sun.lightDirection[2]) :
                    std::string("invalid"));
            if (sun.discValid && sun.lightValid) {
                const auto dot = sun.discDirection[0] * sun.lightDirection[0] +
                    sun.discDirection[1] * sun.lightDirection[1] +
                    sun.discDirection[2] * sun.lightDirection[2];
                const auto degrees = std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot))) * 57.29578f;
                result += std::format(" discToLight={:.1f}deg", degrees);
            }
            return result;
        }

        [[nodiscard]] std::string describeEnabled()
        {
            const auto pbr = pbr::Runtime::get().snapshot();
            const auto skylighting = skylighting::Runtime::get().snapshot();
            const auto ibl = ibl::Runtime::get().snapshot();
            const auto dlaa = dlaa::Runtime::get().snapshot();
            return std::format(
                "enabled[linear={} pbr={}(effective={}) ssgi={} skylighting={}(exteriorActive={}) volumetric={} contact={} ibl={}(diffuse={},shUsable={}) terrain={} dlaa={}(operational={})]",
                linear_lighting::Runtime::get().snapshot().enabled,
                pbr.settings.enabled,
                pbr::Runtime::get().requested(),
                screen_space_gi::Runtime::get().requested(),
                skylighting.requested,
                skylighting.exteriorActive,
                volumetric_lighting::Runtime::get().snapshot().settings.enabled,
                contact_shadows::Runtime::get().snapshot().settings.enabled,
                ibl.enabled,
                ibl.diffuseEnabled,
                ibl.diffuseSHUsable,
                terrain_shadows::Runtime::get().requested(),
                dlaa.settings.enabled,
                dlaa.operational);
        }

        [[nodiscard]] std::string describeActivity(const Counters& now, const Counters& base)
        {
            const auto d = [](std::uint64_t current, std::uint64_t previous) {
                return current >= previous ? current - previous : 0;
            };
            std::string giRejects;
            constexpr std::array<const char*, screen_space_gi::kRejectionCount> giNames{
                "retry", "gbuffer", "viewport", "camera", "resources", "computeState" };
            for (std::size_t i = 0; i < giNames.size(); ++i) {
                const auto count = d(now.giRejections[i], base.giRejections[i]);
                if (count) {
                    giRejects += std::format(" {}={}", giNames[i], count);
                }
            }
            const auto gi = screen_space_gi::Runtime::get().snapshot();
            if (d(now.giRejections[1], base.giRejections[1])) {
                giRejects += std::format(" lastGbufferFormats={}/{}/{}",
                    gi.lastRejectedFormats[0], gi.lastRejectedFormats[1], gi.lastRejectedFormats[2]);
            }
            std::string misses;
            for (const auto descriptor : surface_classification::Runtime::get().takeRecentMissDescriptors()) {
                if (descriptor) {
                    misses += std::format(" 0x{:08X}", descriptor);
                }
            }
            return std::format(
                "skyUpdates={} | shaders selections={} material={} ambient={} inactive={} unmatched={} complexEnv={} complexEnvRejects={} | "
                "descriptorMisses={} contractMisses={} missing=[{}] | pbr scopes={} fallbacks={} failures={} | "
                "ssgi scopes={} prepared={} radiance={} rejects=[{}] | skylighting captures={} rejects={} dispatches={} ambient={} | "
                "volumetric rendered={} rejected={} | contact scopes={} fallbacks={} | ibl material={} fits={} fitRejects={} | dlaa committed={} failures={}",
                d(now.skyUpdates, base.skyUpdates),
                d(now.shaderSelections, base.shaderSelections),
                d(now.materialReplacements, base.materialReplacements),
                d(now.ambientReplacements, base.ambientReplacements),
                d(now.inactiveSelections, base.inactiveSelections),
                d(now.unmatchedSelections, base.unmatchedSelections),
                d(now.complexEnvironment, base.complexEnvironment),
                d(now.complexEnvironmentRejects, base.complexEnvironmentRejects),
                d(now.descriptorMisses, base.descriptorMisses),
                d(now.descriptorContractMisses, base.descriptorContractMisses),
                misses.empty() ? std::string() : misses.substr(1),
                d(now.pbrScopes, base.pbrScopes),
                d(now.pbrFallbacks, base.pbrFallbacks),
                d(now.pbrFailures, base.pbrFailures),
                d(now.giScopes, base.giScopes),
                d(now.giPrepared, base.giPrepared),
                d(now.giRadiance, base.giRadiance),
                giRejects.empty() ? std::string() : giRejects.substr(1),
                d(now.skylightingCaptures, base.skylightingCaptures),
                d(now.skylightingRejects, base.skylightingRejects),
                d(now.skylightingDispatches, base.skylightingDispatches),
                d(now.skylightingAmbient, base.skylightingAmbient),
                d(now.volumetricRendered, base.volumetricRendered),
                d(now.volumetricRejected, base.volumetricRejected),
                d(now.contactScopes, base.contactScopes),
                d(now.contactFallbacks, base.contactFallbacks),
                d(now.iblMaterial, base.iblMaterial),
                d(now.iblFitsPublished, base.iblFitsPublished),
                d(now.iblFitsRejected, base.iblFitsRejected),
                d(now.dlaaCommitted, base.dlaaCommitted),
                d(now.dlaaFailures, base.dlaaFailures));
        }

        void leaveCell(std::uint64_t now)
        {
            if (!state.cell) {
                return;
            }
            const auto stay = now - state.enteredAt;
            if (stay >= kMinimumStayMilliseconds) {
                logging::info("World trace #{} leave after {:.1f}s: {} | {}",
                    state.sequence, static_cast<double>(stay) / 1000.0,
                    describeActivity(capture(), state.baseline),
                    state.sunRequested ? describeSun(state.sunGenerationAtRequest) : std::string("sun=not requested (short stay)"));
            }
            state.cell = nullptr;
        }

        void enterCell(const RE::TESObjectCELL* cell, std::uint64_t now)
        {
            state.cell = cell;
            state.cellFormId = cell->GetFormID();
            state.sequence += 1;
            state.enteredAt = now;
            state.sunRequested = false;
            state.settled = false;
            (void)surface_classification::Runtime::get().takeRecentMissDescriptors();
            state.baseline = capture();
            logging::info("World trace #{} enter: {} | {}",
                state.sequence, describeLocation(cell), describeEnabled());
        }
    }

    void onWorldStateFrame() noexcept
    {
        try {
            const auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* cell = player ? player->GetParentCell() : nullptr;
            const auto now = GetTickCount64();
            if (cell != state.cell || (cell && cell->GetFormID() != state.cellFormId)) {
                leaveCell(now);
                if (cell) {
                    enterCell(cell, now);
                }
                return;
            }
            if (!cell || state.settled) {
                return;
            }
            const auto elapsed = now - state.enteredAt;
            if (!state.sunRequested && elapsed >= kSunRequestMilliseconds) {
                state.sunRequested = true;
                state.sunGenerationAtRequest = sky_sync::Runtime::get().sunMeasurement().generation;
                sky_sync::Runtime::get().requestSunMeasurement();
            }
            if (elapsed >= kSettleMilliseconds) {
                state.settled = true;
                logging::info("World trace #{} settled ({}s): {} | {} | {}",
                    state.sequence, kSettleMilliseconds / 1000,
                    describeActivity(capture(), state.baseline),
                    describeSun(state.sunGenerationAtRequest), describeEnabled());
            }
        } catch (...) {
        }
    }
}
