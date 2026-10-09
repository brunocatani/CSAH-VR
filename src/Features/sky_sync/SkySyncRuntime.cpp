#include "Features/terrain_shadows/TerrainShadowRuntime.h"
#include "Features/water_effects/WaterEffectsRuntime.h"
#include "PCH.h"

#include "Features/sky_sync/SkySyncRuntime.h"

#include "support/Logger.h"

#include <MinHook.h>

#pragma push_macro("MEM_RELEASE")
#pragma push_macro("MAX_PATH")
#pragma push_macro("near")
#pragma push_macro("far")
#undef MEM_RELEASE
#undef MAX_PATH
#undef near
#undef far
#include <RE/NetImmerse/NiAVObject.h>
#include <RE/NetImmerse/NiNode.h>
#include <RE/NetImmerse/NiUpdateData.h>
#pragma pop_macro("far")
#pragma pop_macro("near")
#pragma pop_macro("MAX_PATH")
#pragma pop_macro("MEM_RELEASE")

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace csah::sky_sync
{
    namespace
    {
        constexpr std::uintptr_t kSkyUpdateRva = 0x0063A3A0;
        constexpr std::uintptr_t kStereoRootPointerRva = 0x06235AC8;
        constexpr std::uintptr_t kStereoUploadIdentityRva = 0x01D939AF;
        constexpr std::uintptr_t kMoonPhaseFunctionRva = 0x00632390;
        constexpr std::uintptr_t kMoonPhaseValueRva = 0x05A3CF78;
        constexpr std::size_t kStereoFirstOriginOffset = 0x2590;
        constexpr std::size_t kSunOwnerOffset = 0x80;
        constexpr std::size_t kMasserOwnerOffset = 0x90;
        constexpr std::size_t kSecundaOwnerOffset = 0x98;
        constexpr std::size_t kSunSourceNodeOffset = 0x18;
        constexpr std::size_t kSunLightNodeOffset = 0x38;
        constexpr std::size_t kMoonSourceNodeOffset = 0x08;
        constexpr std::size_t kWorldPositionOffset = 0xA0;
        constexpr std::size_t kMoonColorOffset = 0x168;
        constexpr float kMinimumDirectionMagnitude = 1.0e-5f;
        constexpr float kMaximumCoordinateMagnitude = 1.0e8f;
        constexpr float kSourceHorizonThreshold = 0.0f;
        constexpr float kDirectionResponseSeconds = 1.5f;

        constexpr std::array<std::byte, 16> kSkyUpdateSignature{
            std::byte{ 0x4C }, std::byte{ 0x8B }, std::byte{ 0xDC },
            std::byte{ 0x55 }, std::byte{ 0x56 }, std::byte{ 0x41 },
            std::byte{ 0x54 }, std::byte{ 0x41 }, std::byte{ 0x56 },
            std::byte{ 0x49 }, std::byte{ 0x8D }, std::byte{ 0xAB },
            std::byte{ 0xE8 }, std::byte{ 0xFE }, std::byte{ 0xFF },
            std::byte{ 0xFF },
        };
        constexpr std::array<std::byte, 44> kStereoUploadIdentity{
            std::byte{ 0x48 }, std::byte{ 0x8B }, std::byte{ 0x05 },
            std::byte{ 0x12 }, std::byte{ 0x21 }, std::byte{ 0x4A },
            std::byte{ 0x04 }, std::byte{ 0x4C }, std::byte{ 0x8B },
            std::byte{ 0x65 }, std::byte{ 0xB0 }, std::byte{ 0x48 },
            std::byte{ 0x8B }, std::byte{ 0x0D }, std::byte{ 0xFF },
            std::byte{ 0x20 }, std::byte{ 0x4A }, std::byte{ 0x04 },
            std::byte{ 0x4C }, std::byte{ 0x8D }, std::byte{ 0xA8 },
            std::byte{ 0xE0 }, std::byte{ 0x1E }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x48 }, std::byte{ 0x85 },
            std::byte{ 0xC0 }, std::byte{ 0x75 }, std::byte{ 0x07 },
            std::byte{ 0x4C }, std::byte{ 0x8D }, std::byte{ 0xA9 },
            std::byte{ 0xE0 }, std::byte{ 0x1E }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x4D }, std::byte{ 0x8B },
            std::byte{ 0xAD }, std::byte{ 0xF0 }, std::byte{ 0x06 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };
        constexpr std::array<std::byte, 20> kMoonPhaseSignature{
            std::byte{ 0x48 }, std::byte{ 0x83 }, std::byte{ 0xEC },
            std::byte{ 0x28 }, std::byte{ 0x48 }, std::byte{ 0x8B },
            std::byte{ 0x41 }, std::byte{ 0x40 }, std::byte{ 0x48 },
            std::byte{ 0x85 }, std::byte{ 0xC0 }, std::byte{ 0x74 },
            std::byte{ 0x4E }, std::byte{ 0x0F }, std::byte{ 0xB6 },
            std::byte{ 0x88 }, std::byte{ 0x85 }, std::byte{ 0x00 },
            std::byte{ 0x00 }, std::byte{ 0x00 },
        };

        using SkyUpdateFunction = void(__fastcall*)(RE::Sky*, float);

        struct DetourIdentity final
        {
            const std::byte* patch{};
            const void* destination{};
        };

        struct StereoOriginFields final
        {
            float first[4]{};
            float second[4]{};
        };

        std::mutex hookMutex;
        SkyUpdateFunction originalSkyUpdate{};
        std::byte* skyUpdateTarget{};
        DetourIdentity hookIdentity{};
        std::uintptr_t moduleBase{};
        std::uintptr_t stereoRootPointerAddress{};
        std::uintptr_t moonPhaseAddress{};

        [[nodiscard]] bool readableRange(
            const void* address,
            std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            if (start > std::numeric_limits<std::uintptr_t>::max() - size) {
                return false;
            }
            const auto finish = start + size;
            auto cursor = start;
            while (cursor < finish) {
                MEMORY_BASIC_INFORMATION information{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor),
                        &information,
                        sizeof(information)) != sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto region =
                    reinterpret_cast<std::uintptr_t>(information.BaseAddress);
                if (region > std::numeric_limits<std::uintptr_t>::max() -
                        information.RegionSize) {
                    return false;
                }
                const auto regionEnd = region + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(finish, regionEnd);
            }
            return true;
        }

        template <class T>
        [[nodiscard]] bool readValue(
            const std::uintptr_t address,
            T& output) noexcept
        {
            if (!readableRange(reinterpret_cast<const void*>(address),
                    sizeof(T))) {
                return false;
            }
            std::memcpy(&output, reinterpret_cast<const void*>(address),
                sizeof(T));
            return true;
        }

        template <std::size_t Size>
        [[nodiscard]] bool matchesBytes(
            const std::uintptr_t address,
            const std::array<std::byte, Size>& expected) noexcept
        {
            return readableRange(reinterpret_cast<const void*>(address), Size) &&
                std::memcmp(
                    reinterpret_cast<const void*>(address),
                    expected.data(),
                    Size) == 0;
        }

        [[nodiscard]] bool executableAddress(const void* address) noexcept
        {
            if (!readableRange(address, 1)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) !=
                sizeof(information)) {
                return false;
            }
            constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & executable) != 0;
        }

        [[nodiscard]] const std::byte* relativeJumpDestination(
            const std::byte* patch) noexcept
        {
            if (!patch || !readableRange(patch, 5) ||
                patch[0] != std::byte{ 0xE9 }) {
                return nullptr;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, patch + 1, sizeof(displacement));
            return patch + 5 + displacement;
        }

        [[nodiscard]] bool captureDetourIdentity(
            const void* target,
            DetourIdentity& output) noexcept
        {
            output = {};
            if (!readableRange(target, 5)) {
                return false;
            }
            auto* entry = static_cast<const std::byte*>(target);
            const std::byte* patch = entry;
            if (entry[0] == std::byte{ 0xEB }) {
                std::int8_t displacement{};
                std::memcpy(&displacement, entry + 1, sizeof(displacement));
                if (displacement != -7 ||
                    reinterpret_cast<std::uintptr_t>(entry) < 5) {
                    return false;
                }
                patch = entry - 5;
            }
            const auto* destination = relativeJumpDestination(patch);
            if (!destination || !executableAddress(destination)) {
                return false;
            }
            output = { patch, destination };
            return true;
        }

        [[nodiscard]] bool finiteVector(
            const std::array<float, 3>& value) noexcept
        {
            for (const auto component : value) {
                if (!std::isfinite(component) ||
                    std::fabs(component) > kMaximumCoordinateMagnitude) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool normalize(
            std::array<float, 3>& value) noexcept
        {
            if (!finiteVector(value)) {
                return false;
            }
            const auto magnitude = std::hypot(value[0], value[1], value[2]);
            if (!std::isfinite(magnitude) ||
                magnitude < kMinimumDirectionMagnitude) {
                return false;
            }
            for (auto& component : value) {
                component /= magnitude;
            }
            return true;
        }

        [[nodiscard]] bool captureStereoCenter(
            std::array<float, 3>& center) noexcept
        {
            std::uintptr_t root{};
            StereoOriginFields origins{};
            if (!readValue(stereoRootPointerAddress, root) || !root ||
                !readValue(root + kStereoFirstOriginOffset, origins)) {
                return false;
            }
            for (std::size_t index = 0; index < center.size(); ++index) {
                center[index] = (origins.first[index] + origins.second[index]) *
                    0.5f;
            }
            return finiteVector(center);
        }

        [[nodiscard]] bool captureWorldDirection(
            const std::uintptr_t owner,
            const std::size_t nodeOffset,
            const std::array<float, 3>& center,
            std::array<float, 3>& direction) noexcept
        {
            std::uintptr_t node{};
            std::array<float, 3> world{};
            if (!owner || !readValue(owner + nodeOffset, node) || !node ||
                !readValue(node + kWorldPositionOffset, world)) {
                return false;
            }
            for (std::size_t index = 0; index < direction.size(); ++index) {
                direction[index] = world[index] - center[index];
            }
            return normalize(direction);
        }

        [[nodiscard]] std::array<float, 3> smoothDirection(
            const std::array<float, 3>& current,
            const std::array<float, 3>& target,
            float deltaSeconds) noexcept
        {
            const auto safeDelta = std::isfinite(deltaSeconds) ?
                std::clamp(deltaSeconds, 0.0f, 0.25f) : 0.0f;
            const auto amount = 1.0f - std::exp(
                -safeDelta / kDirectionResponseSeconds);
            std::array<float, 3> result{
                std::lerp(current[0], target[0], amount),
                std::lerp(current[1], target[1], amount),
                std::lerp(current[2], target[2], amount),
            };
            if (normalize(result)) {
                return result;
            }
            return target;
        }

        [[nodiscard]] bool transformWorldDirectionToParentLocal(
            const RE::NiAVObject& light,
            const std::array<float, 3>& worldDirection,
            std::array<float, 3>& localDirection) noexcept
        {
            const auto* parent = light.parent;
            if (!parent ||
                !readableRange(parent, sizeof(RE::NiAVObject))) {
                return false;
            }

            const auto& parentRotation = parent->world.rotate;
            localDirection = {
                parentRotation.entry[0][0] * worldDirection[0] +
                    parentRotation.entry[1][0] * worldDirection[1] +
                    parentRotation.entry[2][0] * worldDirection[2],
                parentRotation.entry[0][1] * worldDirection[0] +
                    parentRotation.entry[1][1] * worldDirection[1] +
                    parentRotation.entry[2][1] * worldDirection[2],
                parentRotation.entry[0][2] * worldDirection[0] +
                    parentRotation.entry[1][2] * worldDirection[1] +
                    parentRotation.entry[2][2] * worldDirection[2],
            };
            return normalize(localDirection);
        }

        void publishVector(
            std::array<std::atomic_uint32_t, 3>& destination,
            const std::array<float, 3>& value) noexcept
        {
            for (std::size_t index = 0; index < value.size(); ++index) {
                destination[index].store(
                    std::bit_cast<std::uint32_t>(value[index]),
                    std::memory_order_relaxed);
            }
        }

        [[nodiscard]] std::array<float, 3> loadVector(
            const std::array<std::atomic_uint32_t, 3>& source) noexcept
        {
            std::array<float, 3> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = std::bit_cast<float>(
                    source[index].load(std::memory_order_relaxed));
            }
            return result;
        }

        void __fastcall skyUpdateHook(RE::Sky* sky, float deltaSeconds)
        {
            originalSkyUpdate(sky, deltaSeconds);
            Runtime::get().onSkyUpdated(sky, deltaSeconds);
        }
    }

    Runtime& Runtime::get() noexcept
    {
        static Runtime instance;
        return instance;
    }

    void Runtime::applySettings(const Settings& settings) noexcept
    {
        enabled_.store(settings.enabled, std::memory_order_release);
    }

    bool Runtime::installHook() noexcept
    {
        std::scoped_lock lock(hookMutex);
        if (hookInstalled_.load(std::memory_order_acquire)) {
            return validateHook("repeat-install");
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            logging::error(
                "Sky Sync requires Fallout4VR 1.2.72; native lighting remains unchanged.");
            return false;
        }

        moduleBase = REL::Module::get().base();
        skyUpdateTarget = reinterpret_cast<std::byte*>(
            moduleBase + kSkyUpdateRva);
        if (!matchesBytes(
                reinterpret_cast<std::uintptr_t>(skyUpdateTarget),
                kSkyUpdateSignature) ||
            !matchesBytes(
                moduleBase + kStereoUploadIdentityRva,
                kStereoUploadIdentity) ||
            !matchesBytes(
                moduleBase + kMoonPhaseFunctionRva,
                kMoonPhaseSignature)) {
            logging::critical(
                "Sky Sync FO4VR sky-update, stereo-camera, or Moon-phase identity gate failed; native lighting remains unchanged.");
            return false;
        }

        stereoRootPointerAddress = moduleBase + kStereoRootPointerRva;
        moonPhaseAddress = moduleBase + kMoonPhaseValueRva;
        const auto createResult = MH_CreateHook(
            skyUpdateTarget,
            reinterpret_cast<void*>(&skyUpdateHook),
            reinterpret_cast<void**>(&originalSkyUpdate));
        if (createResult != MH_OK || !originalSkyUpdate) {
            logging::critical(
                "Sky Sync could not create the verified Sky::Update detour (MinHook={}); native lighting remains unchanged.",
                static_cast<int>(createResult));
            originalSkyUpdate = nullptr;
            hookIdentity = {};
            return false;
        }
        const auto enableResult = MH_EnableHook(skyUpdateTarget);
        if (enableResult != MH_OK ||
            !captureDetourIdentity(skyUpdateTarget, hookIdentity)) {
            (void)MH_DisableHook(skyUpdateTarget);
            (void)MH_RemoveHook(skyUpdateTarget);
            logging::critical(
                "Sky Sync could not enable or verify the Sky::Update detour (MinHook={}); native lighting remains unchanged.",
                static_cast<int>(enableResult));
            originalSkyUpdate = nullptr;
            hookIdentity = {};
            return false;
        }

        hookInstalled_.store(true, std::memory_order_release);
        hookOwned_.store(true, std::memory_order_release);
        stereoCameraReady_.store(true, std::memory_order_release);
        logging::info(
            "Sky Sync installed the verified FO4VR Sky::Update hook: shared stereo-center Sun and Fallout Moon world directions are armed.");
        return true;
    }

    bool Runtime::validateHook(const char* trigger) noexcept
    {
        DetourIdentity current{};
        const auto owned = hookInstalled_.load(std::memory_order_acquire) &&
            hookIdentity.patch && hookIdentity.destination &&
            captureDetourIdentity(skyUpdateTarget, current) &&
            current.patch == hookIdentity.patch &&
            current.destination == hookIdentity.destination;
        hookOwned_.store(owned, std::memory_order_release);
        if (!owned) {
            logging::error(
                "Sky Sync hook ownership validation failed (trigger={}); the post-update correction is disabled.",
                trigger ? trigger : "unknown");
        }
        return owned;
    }

    void Runtime::onWorldReady(const char* boundary) noexcept
    {
        currentDirectionValid_ = false;
        currentSource_ = CelestialSource::none;
        worldReady_.store(true, std::memory_order_release);
        logging::info(
            "Sky Sync presentation armed at {} after Native Shadows completed its world-ready transaction.",
            boundary ? boundary : "unknown");
    }

    void Runtime::onWorldEnding() noexcept
    {
        worldReady_.store(false, std::memory_order_release);
        water_effects::Runtime::get().resetWorld();
        terrain_shadows::Runtime::get().resetWorld();
        applied_.store(false, std::memory_order_release);
        currentDirectionValid_ = false;
        currentSource_ = CelestialSource::none;
    }

    void Runtime::onSkyUpdated(RE::Sky* sky, float deltaSeconds) noexcept
    {
        skyUpdates_.fetch_add(1, std::memory_order_relaxed);
        terrain_shadows::Runtime::get().captureWorld(sky && hookOwned_.load(std::memory_order_acquire) && worldReady_.load(std::memory_order_acquire));
        water_effects::Runtime::get().onSkyUpdated(sky && hookOwned_.load(std::memory_order_acquire) && worldReady_.load(std::memory_order_acquire),deltaSeconds);
        if (!sky || !enabled_.load(std::memory_order_acquire) ||
            !hookOwned_.load(std::memory_order_acquire) ||
            !worldReady_.load(std::memory_order_acquire)) {
            applied_.store(false, std::memory_order_release);
            currentDirectionValid_ = false;
            currentSource_ = CelestialSource::none;
            return;
        }

        const auto skyAddress = reinterpret_cast<std::uintptr_t>(sky);
        std::array<float, 3> center{};
        std::uintptr_t sunOwner{};
        if (!captureStereoCenter(center) ||
            !readValue(skyAddress + kSunOwnerOffset, sunOwner) || !sunOwner) {
            rejectedFrames_.fetch_add(1, std::memory_order_relaxed);
            applied_.store(false, std::memory_order_release);
            return;
        }

        std::array<float, 3> sunDirection{};
        const auto sunValid = captureWorldDirection(
            sunOwner, kSunSourceNodeOffset, center, sunDirection);
        sunValid_.store(sunValid, std::memory_order_release);
        if (sunValid) {
            publishVector(sunDirectionBits_, sunDirection);
        }

        std::uintptr_t masser{};
        std::uintptr_t secunda{};
        (void)readValue(skyAddress + kMasserOwnerOffset, masser);
        (void)readValue(skyAddress + kSecundaOwnerOffset, secunda);
        std::array<float, 3> moonDirection{};
        auto moonValid = captureWorldDirection(
            secunda, kMoonSourceNodeOffset, center, moonDirection);
        if (!moonValid) {
            moonValid = captureWorldDirection(
                masser, kMoonSourceNodeOffset, center, moonDirection);
        }
        moonValid_.store(moonValid, std::memory_order_release);
        if (moonValid) {
            publishVector(moonDirectionBits_, moonDirection);
        }

        std::array<float, 3> moonColor{};
        if (readValue(skyAddress + kMoonColorOffset, moonColor) &&
            finiteVector(moonColor)) {
            publishVector(moonColorBits_, moonColor);
        }
        std::uint32_t phase{};
        if (readValue(moonPhaseAddress, phase) && phase < 8) {
            moonPhase_.store(phase, std::memory_order_relaxed);
        }

        CelestialSource targetSource{ CelestialSource::none };
        std::array<float, 3> targetDirection{};
        if (sunValid && sunDirection[2] > kSourceHorizonThreshold) {
            targetSource = CelestialSource::sun;
            targetDirection = sunDirection;
        } else if (moonValid && moonDirection[2] > kSourceHorizonThreshold) {
            targetSource = CelestialSource::moon;
            targetDirection = moonDirection;
        }
        source_.store(
            static_cast<std::uint32_t>(targetSource),
            std::memory_order_release);
        if (targetSource == CelestialSource::none) {
            applied_.store(false, std::memory_order_release);
            currentDirectionValid_ = false;
            currentSource_ = CelestialSource::none;
            return;
        }

        const auto sourceChanged = currentSource_ != targetSource;
        if (sourceChanged) {
            sourceTransitions_.fetch_add(1, std::memory_order_relaxed);
        }
        currentSource_ = targetSource;
        currentDirection_ = currentDirectionValid_ ?
            smoothDirection(currentDirection_, targetDirection, deltaSeconds) :
            targetDirection;
        currentDirectionValid_ = true;
        if (sourceChanged) {
            logging::info(
                "Sky Sync source={} phase={} sun=[{:.6f},{:.6f},{:.6f}] moon=[{:.6f},{:.6f},{:.6f}] applied=[{:.6f},{:.6f},{:.6f}] moonColor=[{:.6f},{:.6f},{:.6f}].",
                targetSource == CelestialSource::moon ? "Fallout Moon" : "Sun",
                phase,
                sunDirection[0],
                sunDirection[1],
                sunDirection[2],
                moonDirection[0],
                moonDirection[1],
                moonDirection[2],
                currentDirection_[0],
                currentDirection_[1],
                currentDirection_[2],
                moonColor[0],
                moonColor[1],
                moonColor[2]);
        }

        if (targetSource == CelestialSource::sun) {
            // FO4VR already keeps the visible daytime Sun and the
            // shadow-casting directional light synchronized. Rewriting that
            // native light during initial world setup invalidates its cascade
            // owner even when the requested direction is numerically equal.
            // Retain the Sun as the transition origin but mutate the light
            // only when Fallout's Moon becomes the active caster.
            currentDirection_ = targetDirection;
            publishVector(appliedDirectionBits_, currentDirection_);
            applied_.store(true, std::memory_order_release);
            return;
        }

        std::uintptr_t lightAddress{};
        if (!readValue(sunOwner + kSunLightNodeOffset, lightAddress) ||
            !lightAddress ||
            !readableRange(
                reinterpret_cast<const void*>(lightAddress),
                sizeof(RE::NiAVObject))) {
            rejectedFrames_.fetch_add(1, std::memory_order_relaxed);
            applied_.store(false, std::memory_order_release);
            return;
        }

        auto* light = reinterpret_cast<RE::NiAVObject*>(lightAddress);
        std::array<float, 3> localDirection{};
        if (!transformWorldDirectionToParentLocal(
                *light, currentDirection_, localDirection)) {
            rejectedFrames_.fetch_add(1, std::memory_order_relaxed);
            applied_.store(false, std::memory_order_release);
            return;
        }
        light->local.rotate.entry[0][0] = -localDirection[0];
        light->local.rotate.entry[1][0] = -localDirection[1];
        light->local.rotate.entry[2][0] = -localDirection[2];
        RE::NiUpdateData updateData{};
        light->Update(updateData);
        publishVector(appliedDirectionBits_, currentDirection_);
        const auto previousApplications =
            directionApplications_.fetch_add(1, std::memory_order_relaxed);
        if (previousApplications == 0) {
            logging::info(
                "Sky Sync first Fallout Moon directional-light application converted shared world source=[{:.6f},{:.6f},{:.6f}] to parent-local=[{:.6f},{:.6f},{:.6f}].",
                currentDirection_[0],
                currentDirection_[1],
                currentDirection_[2],
                localDirection[0],
                localDirection[1],
                localDirection[2]);
        }
        applied_.store(true, std::memory_order_release);
    }

    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = { .enabled = enabled_.load(std::memory_order_acquire) },
            .hookInstalled = hookInstalled_.load(std::memory_order_acquire),
            .hookOwned = hookOwned_.load(std::memory_order_acquire),
            .stereoCameraReady =
                stereoCameraReady_.load(std::memory_order_acquire),
            .sunValid = sunValid_.load(std::memory_order_acquire),
            .moonValid = moonValid_.load(std::memory_order_acquire),
            .applied = applied_.load(std::memory_order_acquire),
            .source = static_cast<CelestialSource>(
                source_.load(std::memory_order_acquire)),
            .moonPhase = moonPhase_.load(std::memory_order_relaxed),
            .sunDirection = loadVector(sunDirectionBits_),
            .moonDirection = loadVector(moonDirectionBits_),
            .appliedDirection = loadVector(appliedDirectionBits_),
            .moonColor = loadVector(moonColorBits_),
            .skyUpdates = skyUpdates_.load(std::memory_order_relaxed),
            .directionApplications =
                directionApplications_.load(std::memory_order_relaxed),
            .rejectedFrames = rejectedFrames_.load(std::memory_order_relaxed),
            .sourceTransitions =
                sourceTransitions_.load(std::memory_order_relaxed),
        };
    }
}
