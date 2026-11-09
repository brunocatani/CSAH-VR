#include "Features/vanilla_fixes/VanillaFixesRuntime.h"

#include "Features/vanilla_fixes/SslrEnvironmentBinding.h"
#include "Features/vanilla_fixes/SunOcclusionRuntime.h"
#include "Features/vanilla_fixes/VanillaFixesSettingsStore.h"
#include "Features/vanilla_fixes/VanillaShaderFixes.h"
#include "support/Logger.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace csah::vanilla_fixes
{
    namespace
    {
        constexpr auto kPolicyMaintenanceInterval =
            std::chrono::milliseconds(250);
        constexpr std::uintptr_t kIniSettingVtableRva = 0x02C81230;
        constexpr std::uintptr_t kIniPrefSettingVtableRva = 0x02C8C1B0;
        constexpr std::uintptr_t kRendererConfigRva = 0x068787F0;
        constexpr std::uintptr_t kImageSpaceManagerPointerRva = 0x068789E8;
        constexpr std::uintptr_t kSceneRootRegistryPointerRva = 0x06879520;
        constexpr std::uintptr_t kSaoEffectVtableRva = 0x030B8FD8;
        constexpr std::uintptr_t kShaderPropertyRefreshRva = 0x027F5BC0;
        constexpr std::uintptr_t kRefreshSceneRootRva = 0x02804860;

        constexpr std::uint8_t kSaoPolicyBit = 1U << 0;
        constexpr std::uint8_t kScreenSpaceReflectionsPolicyBit = 1U << 1;
        constexpr std::uint8_t
            kNativeScreenSpaceMaterialPipelinePolicyBit = 1U << 2;

        enum class PolicyField : std::uint8_t
        {
            kPrecipitationOcclusion,
            kImageSpaceModifiers,
            kSao,
            kScreenSpaceReflections,
            kNativeScreenSpaceMaterialPipeline,
            kLensFlare,
            kFocusShadows
        };

        struct SettingDescriptor final
        {
            const char* name;
            std::uintptr_t recordRva;
            std::uintptr_t nameRva;
            std::uintptr_t vtableRva;
            PolicyField field;
        };

        constexpr std::array kSettings{
            SettingDescriptor{
                "bPrecipitationOcclusion:VRDisplay",
                0x03740E38,
                0x02CF4248,
                kIniPrefSettingVtableRva,
                PolicyField::kPrecipitationOcclusion },
            SettingDescriptor{
                "bVrAllowFocusShadows:VRDisplay",
                0x037C69F8,
                0x02D78D48,
                kIniSettingVtableRva,
                PolicyField::kFocusShadows },
            SettingDescriptor{
                "bAllowImageSpaceModifiers:VRDisplay",
                0x037C6A10,
                0x02D78D68,
                kIniSettingVtableRva,
                PolicyField::kImageSpaceModifiers },
            SettingDescriptor{
                "bVrAllowSAO:VRDisplay",
                0x03924D50,
                0x030B9658,
                kIniSettingVtableRva,
                PolicyField::kSao },
            SettingDescriptor{
                "bSAOEnable:Display",
                0x03924D68,
                0x030B9670,
                kIniPrefSettingVtableRva,
                PolicyField::kSao },
            SettingDescriptor{
                "bLensFlareVr:VrDisplay",
                0x03924EB8,
                0x030B9810,
                kIniSettingVtableRva,
                PolicyField::kLensFlare },
            SettingDescriptor{
                "bLensFlare:ImageSpace",
                0x03924ED0,
                0x030B9828,
                kIniPrefSettingVtableRva,
                PolicyField::kLensFlare },
            SettingDescriptor{
                "bVrAllowScreenSpaceReflections:VRDisplay",
                0x039255F0,
                0x030BA1F0,
                kIniSettingVtableRva,
                PolicyField::kScreenSpaceReflections },
            SettingDescriptor{
                "bVrAllowScreenSpaceSubsurfaceScattering:VRDisplay",
                0x03925608,
                0x030BA220,
                kIniSettingVtableRva,
                PolicyField::kNativeScreenSpaceMaterialPipeline },
        };

        [[nodiscard]] bool requestedValue(
            const Settings& policy,
            const PolicyField field) noexcept
        {
            if (!policy.enabled) {
                return false;
            }
            switch (field) {
            case PolicyField::kPrecipitationOcclusion:
                return policy.precipitationOcclusion;
            case PolicyField::kImageSpaceModifiers:
                return policy.imageSpaceModifiers;
            case PolicyField::kSao:
                return policy.sao;
            case PolicyField::kScreenSpaceReflections:
                return policy.screenSpaceReflections;
            case PolicyField::kNativeScreenSpaceMaterialPipeline:
                return policy.nativeScreenSpaceMaterialPipeline;
            case PolicyField::kLensFlare:
                return policy.lensFlare;
            case PolicyField::kFocusShadows:
                return policy.focusShadows;
            }
            return false;
        }

        [[nodiscard]] Settings effectivePolicy(Settings policy) noexcept
        {
            if (policy.directionalLightDiagnosticMode ==
                DirectionalLightDiagnosticMode::off) {
                return policy;
            }
            const auto screenSpaceReflectionOnly =
                policy.directionalLightDiagnosticMode ==
                DirectionalLightDiagnosticMode::screenSpaceReflectionOnly;
            const auto lightingOwnershipDiagnostic =
                isLightingOwnershipDiagnostic(
                    policy.directionalLightDiagnosticMode);
            policy.precipitationOcclusion = false;
            policy.imageSpaceModifiers = screenSpaceReflectionOnly;
            policy.sao = false;
            policy.screenSpaceReflections = screenSpaceReflectionOnly;
            // The exact ownership consumers can isolate their final output
            // only if Fallout continues producing the shared DFComposite
            // lighting/material inputs. Older directional diagnostics do not
            // consume that graph and retain their fully suppressed policy.
            policy.nativeScreenSpaceMaterialPipeline =
                lightingOwnershipDiagnostic;
            policy.lensFlare = false;
            policy.focusShadows = false;
            return policy;
        }

        [[nodiscard]] std::uint8_t screenSpacePolicyBits(
            const Settings& policy) noexcept
        {
            std::uint8_t bits{};
            if (requestedValue(policy, PolicyField::kSao)) {
                bits |= kSaoPolicyBit;
            }
            if (requestedValue(
                    policy,
                    PolicyField::kScreenSpaceReflections)) {
                bits |= kScreenSpaceReflectionsPolicyBit;
            }
            if (requestedValue(
                    policy,
                    PolicyField::kNativeScreenSpaceMaterialPipeline)) {
                bits |= kNativeScreenSpaceMaterialPipelinePolicyBit;
            }
            return bits;
        }

        [[nodiscard]] bool policyBit(
            const std::uint8_t policy,
            const std::uint8_t bit) noexcept
        {
            return (policy & bit) != 0;
        }

        [[nodiscard]] bool readableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(address);
            const auto regionBegin =
                reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            const auto regionEnd = regionBegin + information.RegionSize;
            return begin >= regionBegin && begin <= regionEnd &&
                size <= regionEnd - begin;
        }

        [[nodiscard]] bool writableRange(
            const void* address,
            const std::size_t size) noexcept
        {
            if (!readableRange(address, size)) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information{};
            (void)VirtualQuery(address, &information, sizeof(information));
            constexpr DWORD writableMask = PAGE_READWRITE |
                PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (information.Protect & writableMask) != 0;
        }

        void writeBoolean(std::uint8_t* destination, const bool value) noexcept
        {
            (void)InterlockedExchange8(
                reinterpret_cast<volatile char*>(destination),
                value ? 1 : 0);
        }


        class Controller final
        {
        public:
            [[nodiscard]] static Controller& get() noexcept
            {
                static auto* controller = new Controller();
                return *controller;
            }

            [[nodiscard]] bool start(const Settings& settings) noexcept
            {
                auto expected = false;
                if (!started_.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    apply(settings);
                    return contractValid_;
                }
                moduleBase_ = reinterpret_cast<std::uintptr_t>(
                    GetModuleHandleW(nullptr));
                if (!validateContract()) {
                    started_.store(false, std::memory_order_release);
                    return false;
                }
                (void)installSunOcclusionNativeHook();
                apply(settings);
                try {
                    policyMaintenanceThread_ = std::jthread(
                        [this](const std::stop_token stopToken) noexcept {
                            policyMaintenanceMain(stopToken);
                        });
                    policyMaintenanceActive_.store(
                        true,
                        std::memory_order_release);
                } catch (const std::exception& error) {
                    logging::warn(
                        "Vanilla Fixes policy maintenance could not start: {}.",
                        error.what());
                } catch (...) {
                    logging::warn(
                        "Vanilla Fixes policy maintenance could not start.");
                }
                logging::info(
                    "Vanilla Fixes owns 7 verified engine gates, the coordinated stable-reflection suite, and the isolated directional-light ownership diagnostic; shared settings exclusively own INI reloads and native policy maintenance active={}.",
                    policyMaintenanceActive_.load(std::memory_order_acquire));
                return true;
            }

            void apply(const Settings& settings) noexcept
            {
                {
                    std::scoped_lock lock(settingsMutex_);
                    activeSettings_ = settings;
                    applyMemoryPolicy(effectivePolicy(activeSettings_));
                }
                focusEnabled_.store(
                    settings.enabled && settings.focusShadows &&
                        settings.directionalLightDiagnosticMode ==
                            DirectionalLightDiagnosticMode::off,
                    std::memory_order_release);
                setSunOcclusionFixEnabled(
                    settings.enabled && settings.stereoSunOcclusion &&
                    settings.directionalLightDiagnosticMode ==
                        DirectionalLightDiagnosticMode::off);
                setSslrSuiteRequested(
                    settings.enabled && settings.screenSpaceReflections &&
                    settings.directionalLightDiagnosticMode ==
                        DirectionalLightDiagnosticMode::off);
                setSurfaceAnchoredCubemapFixRequested(
                    settings.enabled &&
                    settings.directionalLightDiagnosticMode ==
                        DirectionalLightDiagnosticMode::off);
                const auto diagnosticMode = settings.enabled ?
                    settings.directionalLightDiagnosticMode :
                    DirectionalLightDiagnosticMode::off;
                const auto rawDiagnosticMode =
                    static_cast<std::uint8_t>(diagnosticMode);
                const auto previousDiagnosticMode = diagnosticMode_.exchange(
                    rawDiagnosticMode,
                    std::memory_order_acq_rel);
                if (previousDiagnosticMode != rawDiagnosticMode) {
                    logging::info(
                        "Exclusive lighting diagnostic mode transition: {} -> {}; saved feature settings are preserved and draw-boundary shader reconciliation is armed.",
                        previousDiagnosticMode,
                        rawDiagnosticMode);
                }
                appliedPolicies_.fetch_add(1, std::memory_order_relaxed);
            }

            [[nodiscard]] Settings settings() const noexcept
            {
                std::scoped_lock lock(settingsMutex_);
                return activeSettings_;
            }

            [[nodiscard]] RuntimeSnapshot snapshot() const noexcept
            {
                return {
                    .settings = settings(),
                    .nativeContractValid = contractValid_,
                    .appliedPolicies =
                        appliedPolicies_.load(std::memory_order_relaxed),
                };
            }

            [[nodiscard]] bool focusEnabled() const noexcept
            {
                return focusEnabled_.load(std::memory_order_acquire);
            }

            [[nodiscard]] DirectionalLightDiagnosticMode diagnosticMode()
                const noexcept
            {
                return static_cast<DirectionalLightDiagnosticMode>(
                    diagnosticMode_.load(std::memory_order_acquire));
            }

            void onGameDataReady() noexcept
            {
                gameDataReady_.store(true, std::memory_order_release);
                requestScreenSpacePolicyApply();
            }

        private:
            Controller() = default;

            void policyMaintenanceMain(
                const std::stop_token stopToken) noexcept
            {
                std::unique_lock lock(policyMaintenanceMutex_);
                while (!stopToken.stop_requested()) {
                    policyMaintenanceWake_.wait_for(
                        lock,
                        stopToken,
                        kPolicyMaintenanceInterval,
                        []() noexcept { return false; });
                    if (stopToken.stop_requested()) {
                        break;
                    }
                    lock.unlock();
                    {
                        std::scoped_lock settingsLock(settingsMutex_);
                        applyMemoryPolicy(effectivePolicy(activeSettings_));
                    }
                    lock.lock();
                }
            }

            [[nodiscard]] bool validateRipTarget(
                const std::uintptr_t instructionRva,
                const std::span<const std::uint8_t> opcode,
                const std::size_t displacementOffset,
                const std::size_t instructionLength,
                const std::uintptr_t expectedTargetRva) const noexcept
            {
                const auto* instruction = reinterpret_cast<const std::uint8_t*>(
                    moduleBase_ + instructionRva);
                if (!readableRange(instruction, instructionLength) ||
                    opcode.size() > instructionLength ||
                    std::memcmp(instruction, opcode.data(), opcode.size()) != 0 ||
                    displacementOffset + sizeof(std::int32_t) >
                        instructionLength) {
                    return false;
                }
                std::int32_t displacement{};
                std::memcpy(
                    &displacement,
                    instruction + displacementOffset,
                    sizeof(displacement));
                const auto target = moduleBase_ + instructionRva +
                    instructionLength + displacement;
                return target == moduleBase_ + expectedTargetRva;
            }

            [[nodiscard]] bool validateContract() noexcept
            {
                if (!moduleBase_) {
                    logging::critical(
                        "Visual gate contract rejected: Fallout4VR module base is unavailable.");
                    return false;
                }
                for (const auto& setting : kSettings) {
                    const auto* record = reinterpret_cast<const std::uint8_t*>(
                        moduleBase_ + setting.recordRva);
                    if (!readableRange(record, 0x18) ||
                        !writableRange(record + 8, 1)) {
                        logging::critical(
                            "Visual gate contract rejected: setting record '{}' is inaccessible.",
                            setting.name);
                        return false;
                    }
                    std::uintptr_t vtable{};
                    std::uintptr_t namePointer{};
                    std::memcpy(&vtable, record, sizeof(vtable));
                    std::memcpy(
                        &namePointer,
                        record + 0x10,
                        sizeof(namePointer));
                    const auto expectedNameSize =
                        std::strlen(setting.name) + 1;
                    if (vtable != moduleBase_ + setting.vtableRva ||
                        namePointer != moduleBase_ + setting.nameRva ||
                        !readableRange(
                            reinterpret_cast<const void*>(namePointer),
                            expectedNameSize) ||
                        std::memcmp(
                            reinterpret_cast<const char*>(namePointer),
                            setting.name,
                            expectedNameSize) != 0 ||
                        record[8] > 1) {
                        logging::critical(
                            "Visual gate contract rejected: setting record '{}' failed identity validation.",
                            setting.name);
                        return false;
                    }
                }

                constexpr std::array<std::uint8_t, 2> compareRip{ 0x80, 0x3D };
                constexpr std::array<std::uint8_t, 3> loadRip{
                    0x0F, 0xB6, 0x05 };
                constexpr std::array<std::uint8_t, 3> leaRip{
                    0x48, 0x8D, 0x05 };
                constexpr std::array<std::uint8_t, 3> storeRip{
                    0x48, 0x89, 0x05 };
                constexpr std::array<std::uint8_t, 3> loadPointerRip{
                    0x48, 0x8B, 0x0D };
                constexpr std::array<std::uint8_t, 1> relativeCall{ 0xE8 };
                if (!validateRipTarget(
                        0x0288D68F, compareRip, 2, 7, 0x03924D58) ||
                    !validateRipTarget(
                        0x0288D69D, compareRip, 2, 7, 0x03924D70) ||
                    !validateRipTarget(
                        0x0288D953, compareRip, 2, 7, 0x03924EC0) ||
                    !validateRipTarget(
                        0x0288D95C, compareRip, 2, 7, 0x03924ED8) ||
                    !validateRipTarget(
                        0x0288DA2E, loadRip, 3, 7, 0x039255F8) ||
                    !validateRipTarget(
                        0x0288DA3B, loadRip, 3, 7, 0x03925610) ||
                    !validateRipTarget(
                        0x027AEEB0, leaRip, 3, 7, kRendererConfigRva) ||
                    !validateRipTarget(
                        0x027F4F0A,
                        storeRip,
                        3,
                        7,
                        kImageSpaceManagerPointerRva) ||
                    !validateRipTarget(
                        kShaderPropertyRefreshRva + 4,
                        loadPointerRip,
                        3,
                        7,
                        kSceneRootRegistryPointerRva) ||
                    !validateRipTarget(
                        kShaderPropertyRefreshRva + 0xB,
                        relativeCall,
                        1,
                        5,
                        kRefreshSceneRootRva)) {
                    logging::critical(
                        "Visual gate contract rejected: Fallout4VR consumer signatures do not match 1.2.72.0.");
                    return false;
                }

                constexpr std::array<std::uint8_t, 3> saoStore{
                    0x88, 0x42, 0x32 };
                constexpr std::array<std::uint8_t, 6> lensStore{
                    0x88, 0x8A, 0xEC, 0x00, 0x00, 0x00 };
                constexpr std::array<std::uint8_t, 6> ssrStore{
                    0x88, 0x82, 0x1C, 0x01, 0x00, 0x00 };
                constexpr std::array<std::uint8_t, 6> sssStore{
                    0x88, 0x82, 0x1D, 0x01, 0x00, 0x00 };
                const auto exact = [this](
                                       const std::uintptr_t rva,
                                       const auto& expected) noexcept {
                    const auto* address = reinterpret_cast<const std::uint8_t*>(
                        moduleBase_ + rva);
                    return readableRange(address, expected.size()) &&
                        std::memcmp(
                            address,
                            expected.data(),
                            expected.size()) == 0;
                };
                if (!exact(0x0288D6AC, saoStore) ||
                    !exact(0x0288D96F, lensStore) ||
                    !exact(0x0288DA35, ssrStore) ||
                    !exact(0x0288DA42, sssStore) ||
                    !writableRange(
                        reinterpret_cast<void*>(
                            moduleBase_ + kRendererConfigRva + 0x32),
                        1) ||
                    !writableRange(
                        reinterpret_cast<void*>(
                            moduleBase_ + kRendererConfigRva + 0x11C),
                        2) ||
                    !readableRange(
                        reinterpret_cast<const void*>(
                            moduleBase_ + kSceneRootRegistryPointerRva),
                        sizeof(std::uintptr_t))) {
                    logging::critical(
                        "Visual gate contract rejected: effective runtime targets failed validation.");
                    return false;
                }

                contractValid_ = true;
                logging::info(
                    "Visual gate memory contract passed: 9 setting records, renderer snapshot, coordinated SAO/SSR/SSS policy path, and native shader-property refresh are verified.");
                return true;
            }

            void applyMemoryPolicy(const Settings& policy) noexcept
            {
                if (!contractValid_) {
                    return;
                }
                for (const auto& setting : kSettings) {
                    const auto preserveStartupCapability =
                        setting.field == PolicyField::kSao ||
                        setting.field == PolicyField::kScreenSpaceReflections ||
                        setting.field ==
                            PolicyField::kNativeScreenSpaceMaterialPipeline ||
                        setting.field == PolicyField::kLensFlare;
                    writeBoolean(
                        reinterpret_cast<std::uint8_t*>(
                            moduleBase_ + setting.recordRva + 8),
                        preserveStartupCapability ? true :
                            requestedValue(policy, setting.field));
                }

                auto* renderer = reinterpret_cast<std::uint8_t*>(
                    moduleBase_ + kRendererConfigRva);
                const auto lensFlare =
                    requestedValue(policy, PolicyField::kLensFlare);
                writeBoolean(renderer + 0xEC, lensFlare);

                const auto requestedScreenSpacePolicy =
                    screenSpacePolicyBits(policy);
                const auto previousScreenSpacePolicy =
                    requestedScreenSpacePolicy_.exchange(
                        requestedScreenSpacePolicy,
                        std::memory_order_acq_rel);
                const auto wasInitialized =
                    screenSpacePolicyInitialized_.exchange(
                        true,
                        std::memory_order_acq_rel);
                if (!wasInitialized ||
                    previousScreenSpacePolicy != requestedScreenSpacePolicy) {
                    screenSpacePolicyGeneration_.fetch_add(
                        1,
                        std::memory_order_acq_rel);
                }
                requestScreenSpacePolicyApply();
            }

            void requestScreenSpacePolicyApply() noexcept
            {
                if (!contractValid_ ||
                    !gameDataReady_.load(std::memory_order_acquire) ||
                    !screenSpacePolicyInitialized_.load(
                        std::memory_order_acquire)) {
                    return;
                }
                const auto generation =
                    screenSpacePolicyGeneration_.load(
                        std::memory_order_acquire);
                if (generation == screenSpacePolicyAppliedGeneration_.load(
                                      std::memory_order_acquire) &&
                    !screenSpaceEffectSyncPending_.load(
                        std::memory_order_acquire) &&
                    !nativePropertyRefreshPending_.load(
                        std::memory_order_acquire)) {
                    return;
                }
                auto expected = false;
                if (!screenSpacePolicyTaskQueued_.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel)) {
                    return;
                }
                const auto* tasks = F4SE::GetTaskInterface();
                if (!tasks) {
                    screenSpacePolicyTaskQueued_.store(
                        false,
                        std::memory_order_release);
                    if (!taskInterfaceFailureLogged_.exchange(
                            true,
                            std::memory_order_acq_rel)) {
                        logging::error(
                            "Native screen-space policy apply is pending because the F4SE main-thread task interface is unavailable.");
                    }
                    return;
                }
                try {
                    tasks->AddTask([]() noexcept {
                        Controller::get().applyScreenSpacePolicyOnMainThread();
                    });
                } catch (const std::exception& error) {
                    screenSpacePolicyTaskQueued_.store(
                        false,
                        std::memory_order_release);
                    logging::error(
                        "Native screen-space policy main-thread task could not be queued: {}.",
                        error.what());
                } catch (...) {
                    screenSpacePolicyTaskQueued_.store(
                        false,
                        std::memory_order_release);
                    logging::error(
                        "Native screen-space policy main-thread task could not be queued.");
                }
            }

            void applyScreenSpacePolicyOnMainThread() noexcept
            {
                const auto targetGeneration =
                    screenSpacePolicyGeneration_.load(
                        std::memory_order_acquire);
                const auto previousAppliedGeneration =
                    screenSpacePolicyAppliedGeneration_.load(
                        std::memory_order_acquire);
                const auto policy = requestedScreenSpacePolicy_.load(
                    std::memory_order_acquire);
                const auto sao = policyBit(policy, kSaoPolicyBit);
                const auto screenSpaceReflections = policyBit(
                    policy,
                    kScreenSpaceReflectionsPolicyBit);
                const auto nativeScreenSpaceMaterialPipeline = policyBit(
                    policy,
                    kNativeScreenSpaceMaterialPipelinePolicyBit);
                auto* renderer = reinterpret_cast<std::uint8_t*>(
                    moduleBase_ + kRendererConfigRva);
                const auto nativePropertyRefreshRequired =
                    nativePropertyRefreshPending_.exchange(
                        false,
                        std::memory_order_acq_rel) ||
                    (renderer[0x11C] != 0) != screenSpaceReflections ||
                    (renderer[0x11D] != 0) !=
                        nativeScreenSpaceMaterialPipeline;

                writeBoolean(renderer + 0x32, sao);
                writeBoolean(renderer + 0x11C, screenSpaceReflections);
                writeBoolean(
                    renderer + 0x11D,
                    nativeScreenSpaceMaterialPipeline);

                const auto effectSync = synchronizeSaoAndSslrEffect(
                    sao,
                    screenSpaceReflections);
                screenSpaceEffectSyncPending_.store(
                    !effectSync.has_value(),
                    std::memory_order_release);

                auto nativePropertyRefreshCompleted = true;
                if (nativePropertyRefreshRequired) {
                    nativePropertyRefreshCompleted =
                        refreshNativeShaderProperties();
                    nativePropertyRefreshPending_.store(
                        !nativePropertyRefreshCompleted,
                        std::memory_order_release);
                }

                screenSpacePolicyAppliedGeneration_.store(
                    targetGeneration,
                    std::memory_order_release);
                screenSpacePolicyTaskQueued_.store(
                    false,
                    std::memory_order_release);
                screenSpacePolicyApplies_.fetch_add(
                    1,
                    std::memory_order_relaxed);

                if (targetGeneration != previousAppliedGeneration) {
                    const auto refreshState =
                        !nativePropertyRefreshRequired ? "not-required" :
                        nativePropertyRefreshCompleted ? "completed" :
                        "deferred";
                    logging::info(
                        "Native screen-space policy applied on the F4SE main-thread queue: SAO={}, SSR={}, native SSS/shared graph={}, shared SAO/SSR effect={}, shader-property refresh={}.",
                        sao,
                        screenSpaceReflections,
                        nativeScreenSpaceMaterialPipeline,
                        effectSync.value_or(false),
                        refreshState);
                } else if (nativePropertyRefreshRequired &&
                    nativePropertyRefreshCompleted) {
                    logging::info(
                        "Deferred native screen-space shader-property refresh completed on the F4SE main-thread queue.");
                }

                if (screenSpacePolicyGeneration_.load(
                        std::memory_order_acquire) != targetGeneration) {
                    requestScreenSpacePolicyApply();
                }
            }

            [[nodiscard]] std::optional<bool> synchronizeSaoAndSslrEffect(
                const bool sao,
                const bool screenSpaceReflections) noexcept
            {
                const auto* managerPointer = reinterpret_cast<
                    const std::uintptr_t*>(
                    moduleBase_ + kImageSpaceManagerPointerRva);
                if (!readableRange(managerPointer, sizeof(*managerPointer))) {
                    return std::nullopt;
                }
                std::uintptr_t manager{};
                std::memcpy(&manager, managerPointer, sizeof(manager));
                if (!manager || !readableRange(
                        reinterpret_cast<const void*>(manager), 0xBD)) {
                    return std::nullopt;
                }
                if (*reinterpret_cast<const std::uint8_t*>(manager + 0xBC) ==
                    0) {
                    return std::nullopt;
                }
                std::uintptr_t effects{};
                std::memcpy(
                    &effects,
                    reinterpret_cast<const void*>(manager + 0x18),
                    sizeof(effects));
                // The factory grows the array to count 0x48, then stores the
                // CS SAO object at byte offset 0x238: zero-based slot 0x47.
                constexpr std::size_t saoEffectIndex = 0x47;
                if (!effects || !readableRange(
                        reinterpret_cast<const void*>(
                        effects + saoEffectIndex * sizeof(std::uintptr_t)),
                        sizeof(std::uintptr_t))) {
                    return std::nullopt;
                }
                std::uintptr_t effect{};
                std::memcpy(
                    &effect,
                    reinterpret_cast<const void*>(
                        effects + saoEffectIndex * sizeof(std::uintptr_t)),
                    sizeof(effect));
                if (!effect || !readableRange(
                        reinterpret_cast<const void*>(effect), 0x121) ||
                    !writableRange(
                        reinterpret_cast<const void*>(effect + 8), 1) ||
                    !writableRange(
                        reinterpret_cast<const void*>(effect + 0x120), 1)) {
                    return std::nullopt;
                }
                std::uintptr_t vtable{};
                std::memcpy(
                    &vtable,
                    reinterpret_cast<const void*>(effect),
                    sizeof(vtable));
                if (vtable != moduleBase_ + kSaoEffectVtableRva) {
                    if (!saoIdentityFailureLogged_) {
                        saoIdentityFailureLogged_ = true;
                        logging::critical(
                            "SAO live apply rejected: effect slot 0x47 has an unexpected vtable; source and renderer gates remain controlled.");
                    }
                    return false;
                }
                writeBoolean(
                    reinterpret_cast<std::uint8_t*>(effect + 0x120),
                    screenSpaceReflections);
                writeBoolean(
                    reinterpret_cast<std::uint8_t*>(effect + 8),
                    sao || screenSpaceReflections);
                return true;
            }

            [[nodiscard]] bool refreshNativeShaderProperties() noexcept
            {
                const auto* registryPointer = reinterpret_cast<
                    const std::uintptr_t*>(
                    moduleBase_ + kSceneRootRegistryPointerRva);
                if (!readableRange(registryPointer, sizeof(*registryPointer))) {
                    return false;
                }
                std::uintptr_t registry{};
                std::memcpy(&registry, registryPointer, sizeof(registry));
                if (!registry) {
                    if (!sceneRegistryUnavailableLogged_.exchange(
                            true,
                            std::memory_order_acq_rel)) {
                        logging::warn(
                            "Native screen-space shader-property refresh is deferred until the scene-root registry is available.");
                    }
                    return false;
                }
                using RefreshShaderProperties = void (*)();
                const auto refresh = reinterpret_cast<RefreshShaderProperties>(
                    moduleBase_ + kShaderPropertyRefreshRva);
                refresh();
                nativePropertyRefreshes_.fetch_add(
                    1,
                    std::memory_order_relaxed);
                return true;
            }


            std::uintptr_t moduleBase_{};
            Settings activeSettings_{};
            bool contractValid_{};
            bool saoIdentityFailureLogged_{};
            std::atomic_bool started_{};
            std::atomic_bool policyMaintenanceActive_{};
            std::atomic_bool gameDataReady_{};
            std::atomic_bool focusEnabled_{ true };
            std::atomic_uint8_t diagnosticMode_{};
            std::atomic_uint8_t requestedScreenSpacePolicy_{};
            std::atomic_bool screenSpacePolicyInitialized_{};
            std::atomic_bool screenSpacePolicyTaskQueued_{};
            std::atomic_bool screenSpaceEffectSyncPending_{ true };
            std::atomic_bool nativePropertyRefreshPending_{};
            std::atomic_bool taskInterfaceFailureLogged_{};
            std::atomic_bool sceneRegistryUnavailableLogged_{};
            std::atomic_uint64_t screenSpacePolicyGeneration_{};
            std::atomic_uint64_t screenSpacePolicyAppliedGeneration_{};
            std::atomic_uint64_t screenSpacePolicyApplies_{};
            std::atomic_uint64_t nativePropertyRefreshes_{};
            std::atomic_uint64_t appliedPolicies_{};
            mutable std::mutex settingsMutex_;
            std::mutex policyMaintenanceMutex_;
            std::condition_variable_any policyMaintenanceWake_;
            std::jthread policyMaintenanceThread_;
        };
    }

    bool startRuntime(const Settings& settings) noexcept
    {
        return Controller::get().start(settings);
    }

    void applySettings(const Settings& settings) noexcept
    {
        Controller::get().apply(settings);
    }

    void onGameDataReady() noexcept
    {
        Controller::get().onGameDataReady();
    }

    Settings activeSettings() noexcept
    {
        return Controller::get().settings();
    }

    bool focusShadowsEnabled() noexcept
    {
        return Controller::get().focusEnabled();
    }

    DirectionalLightDiagnosticMode directionalLightDiagnosticMode() noexcept
    {
        return Controller::get().diagnosticMode();
    }

    RuntimeSnapshot runtimeSnapshot() noexcept
    {
        return Controller::get().snapshot();
    }
}
