#include "PCH.h"

#include "Features/dlaa/StreamlineBackend.h"

#include "support/Logger.h"

#include <Windows.h>
#include <sl.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cfloat>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace csah::dlaa
{
    namespace
    {
        struct BackendState
        {
            struct DlssOptionsKey
            {
                std::uint32_t outputWidth{};
                std::uint32_t outputHeight{};
                Mode mode{ Mode::dlaa };
                ModelPreset modelPreset{ ModelPreset::qualityK };
                bool valid{};
            };

            std::mutex mutex;
            bool initializationAttempted{};
            std::filesystem::path directory;
            HMODULE interposer{};
            PFun_slInit* init{};
            PFun_slShutdown* shutdown{};
            PFun_slSetD3DDevice* setD3DDevice{};
            PFun_slUpgradeInterface* upgradeInterface{};
            PFun_slIsFeatureLoaded* isFeatureLoaded{};
            PFun_slIsFeatureSupported* isFeatureSupported{};
            PFun_slGetFeatureRequirements* getFeatureRequirements{};
            PFun_slGetFeatureFunction* getFeatureFunction{};
            PFun_slGetNewFrameToken* getNewFrameToken{};
            PFun_slSetConstants* setConstants{};
            PFun_slSetTagForFrame* setTagForFrame{};
            PFun_slEvaluateFeature* evaluateFeature{};
            PFun_slAllocateResources* allocateResources{};
            PFun_slFreeResources* freeResources{};
            PFun_slDLSSGetOptimalSettings* dlssGetOptimalSettings{};
            PFun_slDLSSSetOptions* dlssSetOptions{};
            PFun_slDLSSGetState* dlssGetState{};
            bool dlaaResourcesAllocated{};
            std::array<DlssOptionsKey, 2> dlssOptions{};
            std::atomic_bool initialized{};
            std::atomic_bool deviceBound{};
            std::atomic_bool swapChainUpgraded{};
            std::atomic_bool featureLoaded{};
            std::atomic_bool featureSupported{};
            std::atomic_bool featureFunctionsBound{};
            std::atomic_uint32_t initializationResult{};
            std::atomic_uint32_t deviceResult{};
            std::atomic_uint32_t supportResult{};
        };

        BackendState state;
        constexpr std::array<sl::Feature, 1> kRequestedFeatures{
            sl::kFeatureDLSS
        };
        // Stable identity generated specifically for the FO4VR Community
        // Shaders DLAA integration. Do not reuse the flat DLAA or Open Shaders
        // project identities; NGX uses this together with the custom engine
        // name/version when no NVIDIA-issued numeric application ID exists.
        constexpr char kProjectId[] =
            "615adbde-c997-4380-a9b0-73dfb99fb0b1";

        void streamlineLogCallback(
            sl::LogType type,
            const char* message) noexcept
        {
            if (!message || *message == '\0') {
                return;
            }
            try {
                switch (type) {
                case sl::LogType::eError:
                    logging::error("[Streamline] {}", message);
                    break;
                case sl::LogType::eWarn:
                    logging::warn("[Streamline] {}", message);
                    break;
                case sl::LogType::eInfo:
                default:
                    logging::info("[Streamline] {}", message);
                    break;
                }
            } catch (...) {
                OutputDebugStringA(message);
            }
        }

        template <class Function>
        [[nodiscard]] bool resolve(
            HMODULE module,
            const char* name,
            Function*& output) noexcept
        {
            output = module ? reinterpret_cast<Function*>(
                GetProcAddress(module, name)) : nullptr;
            return output != nullptr;
        }

        [[nodiscard]] std::filesystem::path pluginDirectory()
        {
            std::array<wchar_t, 32768> path{};
            const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
            const auto count = GetModuleFileNameW(
                module,
                path.data(),
                static_cast<DWORD>(path.size()));
            if (count == 0 || count >= path.size()) {
                return {};
            }
            return std::filesystem::path(path.data()).parent_path();
        }

        [[nodiscard]] bool verifyRuntimeFiles(
            const std::filesystem::path& directory)
        {
            constexpr std::array<std::wstring_view, 4> runtimeFiles{
                L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss.dll", L"nvngx_dlss.dll"
            };
            for (const auto fileName : runtimeFiles) {
                const auto path = directory / fileName;
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error) || error) {
                    logging::error(
                        "DLAA Streamline runtime is missing '{}'.",
                        path.string());
                    return false;
                }
                if (std::filesystem::file_size(path, error) == 0 || error) {
                    logging::error("DLAA Streamline runtime is empty or unreadable: '{}'.", path.string());
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool resolveCoreFunctions() noexcept
        {
            return resolve(state.interposer, "slInit", state.init) &&
                resolve(state.interposer, "slShutdown", state.shutdown) &&
                resolve(
                    state.interposer,
                    "slSetD3DDevice",
                    state.setD3DDevice) &&
                resolve(
                    state.interposer,
                    "slUpgradeInterface",
                    state.upgradeInterface) &&
                resolve(
                    state.interposer,
                    "slIsFeatureLoaded",
                    state.isFeatureLoaded) &&
                resolve(
                    state.interposer,
                    "slIsFeatureSupported",
                    state.isFeatureSupported) &&
                resolve(
                    state.interposer,
                    "slGetFeatureRequirements",
                    state.getFeatureRequirements) &&
                resolve(
                    state.interposer,
                    "slGetFeatureFunction",
                    state.getFeatureFunction) &&
                resolve(
                    state.interposer,
                    "slGetNewFrameToken",
                    state.getNewFrameToken) &&
                resolve(
                    state.interposer,
                    "slSetConstants",
                    state.setConstants) &&
                resolve(
                    state.interposer,
                    "slSetTagForFrame",
                    state.setTagForFrame) &&
                resolve(
                    state.interposer,
                    "slEvaluateFeature",
                    state.evaluateFeature) &&
                resolve(
                    state.interposer,
                    "slAllocateResources",
                    state.allocateResources) &&
                resolve(
                    state.interposer,
                    "slFreeResources",
                    state.freeResources);
        }

        [[nodiscard]] bool bindDlssFunctions() noexcept
        {
            void* getOptimalSettings{};
            void* setOptions{};
            void* getState{};
            if (!state.getFeatureFunction ||
                state.getFeatureFunction(
                    sl::kFeatureDLSS,
                    "slDLSSGetOptimalSettings",
                    getOptimalSettings) != sl::Result::eOk ||
                state.getFeatureFunction(
                    sl::kFeatureDLSS,
                    "slDLSSSetOptions",
                    setOptions) != sl::Result::eOk ||
                state.getFeatureFunction(
                    sl::kFeatureDLSS,
                    "slDLSSGetState",
                    getState) != sl::Result::eOk) {
                return false;
            }
            state.dlssGetOptimalSettings = reinterpret_cast<
                PFun_slDLSSGetOptimalSettings*>(getOptimalSettings);
            state.dlssSetOptions = reinterpret_cast<
                PFun_slDLSSSetOptions*>(setOptions);
            state.dlssGetState = reinterpret_cast<
                PFun_slDLSSGetState*>(getState);
            return state.dlssGetOptimalSettings && state.dlssSetOptions &&
                state.dlssGetState;
        }

        [[nodiscard]] sl::DLSSMode toStreamlineMode(Mode mode) noexcept
        {
            switch (mode) {
            case Mode::dlssQuality:
                return sl::DLSSMode::eMaxQuality;
            case Mode::dlssBalanced:
                return sl::DLSSMode::eBalanced;
            case Mode::dlssPerformance:
                return sl::DLSSMode::eMaxPerformance;
            case Mode::dlssUltraPerformance:
                return sl::DLSSMode::eUltraPerformance;
            case Mode::dlaa:
            case Mode::centerDlaa:
            default:
                return sl::DLSSMode::eDLAA;
            }
        }

        [[nodiscard]] sl::DLSSPreset toStreamlinePreset(
            ModelPreset preset) noexcept
        {
            switch (preset) {
            case ModelPreset::reducedGhostingJ:
                return sl::DLSSPreset::ePresetJ;
            case ModelPreset::qualityK:
                return sl::DLSSPreset::ePresetK;
            case ModelPreset::stableL:
                return sl::DLSSPreset::ePresetL;
            case ModelPreset::performanceM:
                return sl::DLSSPreset::ePresetM;
            case ModelPreset::automatic:
            default:
                return sl::DLSSPreset::eDefault;
            }
        }

        void populateOptions(
            const StreamlineDlaaFrame& frame,
            sl::DLSSOptions& options) noexcept
        {
            options.mode = toStreamlineMode(frame.mode);
            options.outputWidth = frame.outputWidth;
            options.outputHeight = frame.outputHeight;
            options.preExposure = 1.0f;
            options.exposureScale = 1.0f;
            // The qualified FO4VR TAA boundary is post-tonemap LDR. Its scene
            // input uses R11G11B10 storage for precision, but live samples
            // track the R8 output rather than scene-linear HDR luminance.
            options.colorBuffersHDR = sl::Boolean::eFalse;
            // No FO4VR exposure texture is tagged. Let NGX derive exposure
            // for scaled DLSS modes while preserving the already-qualified
            // native-resolution DLAA color path.
            options.useAutoExposure = isDlssMode(frame.mode) ?
                sl::Boolean::eTrue : sl::Boolean::eFalse;
            // Runtime qualification proved every relevant FO4VR input and
            // output alpha sample is the constant 1.0.
            options.alphaUpscalingEnabled = sl::Boolean::eFalse;
            const auto preset = toStreamlinePreset(frame.modelPreset);
            options.dlaaPreset = preset;
            options.qualityPreset = preset;
            options.balancedPreset = preset;
            options.performancePreset = preset;
            options.ultraPerformancePreset = preset;
            options.ultraQualityPreset = preset;
        }

        [[nodiscard]] Microsoft::WRL::ComPtr<IDXGIAdapter>
            resolveAdapter(
                IDXGIAdapter* requested,
                ID3D11Device* device) noexcept
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if (requested) {
                adapter = requested;
                return adapter;
            }
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
            if (!device || FAILED(device->QueryInterface(
                    IID_PPV_ARGS(dxgiDevice.GetAddressOf()))) ||
                !dxgiDevice || FAILED(dxgiDevice->GetAdapter(
                    adapter.GetAddressOf()))) {
                adapter.Reset();
            }
            return adapter;
        }

        [[nodiscard]] sl::float4x4 toSlMatrix(
            const StreamlineMatrix& source) noexcept
        {
            sl::float4x4 result{};
            static_assert(sizeof(result) == sizeof(source.values));
            std::memcpy(&result, source.values.data(), sizeof(result));
            return result;
        }

        [[nodiscard]] bool finiteMatrix(
            const sl::float4x4& matrix) noexcept
        {
            const auto* values = &matrix.row[0].x;
            bool nonzero{};
            for (std::size_t index = 0; index < 16; ++index) {
                if (!std::isfinite(values[index])) {
                    return false;
                }
                nonzero = nonzero || std::abs(values[index]) > 0.000001f;
            }
            return nonzero;
        }

        [[nodiscard]] bool setEyeConstants(
            const StreamlineEyeConstants& source,
            const sl::FrameToken& token,
            const sl::ViewportHandle& viewport) noexcept
        {
            sl::Constants constants{};
            constants.cameraViewToClip = toSlMatrix(
                source.cameraViewToClip);
            sl::matrixFullInvert(
                constants.clipToCameraView,
                constants.cameraViewToClip);
            const auto current = toSlMatrix(source.currentViewProjection);
            const auto previous = toSlMatrix(source.previousViewProjection);
            sl::float4x4 inverseCurrent{};
            sl::matrixFullInvert(inverseCurrent, current);
            sl::matrixMul(
                constants.clipToPrevClip,
                inverseCurrent,
                previous);
            sl::matrixFullInvert(
                constants.prevClipToClip,
                constants.clipToPrevClip);
            if (!finiteMatrix(constants.cameraViewToClip) ||
                !finiteMatrix(constants.clipToCameraView) ||
                !finiteMatrix(constants.clipToPrevClip) ||
                !finiteMatrix(constants.prevClipToClip)) {
                return false;
            }
            constants.jitterOffset = {
                -source.jitterPixelsX,
                -source.jitterPixelsY,
            };
            constants.mvecScale = {
                source.motionVectorScaleX,
                source.motionVectorScaleY,
            };
            constants.cameraPinholeOffset = { 0.0f, 0.0f };
            constants.cameraRight = {
                source.cameraRight[0],
                source.cameraRight[1],
                source.cameraRight[2],
            };
            constants.cameraUp = {
                source.cameraUp[0],
                source.cameraUp[1],
                source.cameraUp[2],
            };
            constants.cameraFwd = {
                source.cameraForward[0],
                source.cameraForward[1],
                source.cameraForward[2],
            };
            constants.cameraPos = {
                source.cameraPosition[0],
                source.cameraPosition[1],
                source.cameraPosition[2],
            };
            constants.cameraNear = source.cameraNear;
            constants.cameraFar = source.cameraFar;
            constants.cameraFOV = source.cameraFovRadians;
            constants.cameraAspectRatio = source.cameraAspectRatio;
            constants.motionVectorsInvalidValue = -FLT_MAX;
            // Depth preparation publishes the selected world projection's
            // domain. Its endpoints, not a cleared texture sample, establish
            // the direction supplied to Streamline.
            constants.depthInverted = source.depthInverted ?
                sl::Boolean::eTrue : sl::Boolean::eFalse;
            constants.cameraMotionIncluded = sl::Boolean::eTrue;
            constants.motionVectors3D = sl::Boolean::eFalse;
            constants.reset = source.reset ?
                sl::Boolean::eTrue : sl::Boolean::eFalse;
            constants.orthographicProjection = sl::Boolean::eFalse;
            constants.motionVectorsDilated = sl::Boolean::eFalse;
            constants.motionVectorsJittered = sl::Boolean::eFalse;
            return state.setConstants &&
                state.setConstants(constants, token, viewport) ==
                    sl::Result::eOk;
        }

        [[nodiscard]] bool evaluateEye(
            const StreamlineDlaaFrame& frame,
            const sl::FrameToken& token,
            std::uint32_t eye) noexcept
        {
            if (eye >= frame.resources.size()) {
                return false;
            }
            const auto& source = frame.resources[eye];
            if (!source.colorInput || !source.colorOutput || !source.depth ||
                !source.motionVectors ||
                (isDlssMode(frame.mode) && !source.biasCurrentColor)) {
                return false;
            }
            const sl::ViewportHandle viewport(eye);
            sl::DLSSOptions options{};
            populateOptions(frame, options);
            auto& optionsKey = state.dlssOptions[eye];
            const auto optionsChanged = !optionsKey.valid ||
                optionsKey.outputWidth != frame.outputWidth ||
                optionsKey.outputHeight != frame.outputHeight ||
                optionsKey.mode != frame.mode ||
                optionsKey.modelPreset != frame.modelPreset;
            if (!state.dlssSetOptions ||
                (optionsChanged &&
                 state.dlssSetOptions(viewport, options) != sl::Result::eOk) ||
                !setEyeConstants(frame.constants[eye], token, viewport)) {
                return false;
            }
            if (optionsChanged) {
                optionsKey = {
                    .outputWidth = frame.outputWidth,
                    .outputHeight = frame.outputHeight,
                    .mode = frame.mode,
                    .modelPreset = frame.modelPreset,
                    .valid = true,
                };
            }

            sl::Resource inputResource(
                sl::ResourceType::eTex2d,
                source.colorInput);
            sl::Resource outputResource(
                sl::ResourceType::eTex2d,
                source.colorOutput);
            sl::Resource depthResource(
                sl::ResourceType::eTex2d,
                source.depth);
            sl::Resource motionResource(
                sl::ResourceType::eTex2d,
                source.motionVectors);
            sl::Resource biasCurrentColorResource(
                sl::ResourceType::eTex2d,
                source.biasCurrentColor);
            const sl::Extent inputExtent{
                source.inputTop,
                source.inputLeft,
                frame.inputWidth,
                frame.inputHeight,
            };
            const sl::Extent outputExtent{
                0,
                0,
                frame.outputWidth,
                frame.outputHeight,
            };
            std::array<sl::ResourceTag, 5> tags{
                sl::ResourceTag(
                    &inputResource,
                    sl::kBufferTypeScalingInputColor,
                    sl::ResourceLifecycle::eValidUntilEvaluate,
                    &inputExtent),
                sl::ResourceTag(
                    &outputResource,
                    sl::kBufferTypeScalingOutputColor,
                    sl::ResourceLifecycle::eValidUntilEvaluate,
                    &outputExtent),
                sl::ResourceTag(
                    &depthResource,
                    sl::kBufferTypeDepth,
                    sl::ResourceLifecycle::eValidUntilEvaluate,
                    &inputExtent),
                sl::ResourceTag(
                    &motionResource,
                    sl::kBufferTypeMotionVectors,
                    sl::ResourceLifecycle::eValidUntilEvaluate,
                    &inputExtent),
                sl::ResourceTag(
                    &biasCurrentColorResource,
                    sl::kBufferTypeBiasCurrentColorHint,
                    sl::ResourceLifecycle::eValidUntilEvaluate,
                    &inputExtent),
            };
            const auto tagCount = isDlssMode(frame.mode) ?
                static_cast<std::uint32_t>(tags.size()) : 4u;
            if (!state.setTagForFrame ||
                state.setTagForFrame(
                    token,
                    viewport,
                    tags.data(),
                    tagCount,
                    frame.context) != sl::Result::eOk) {
                return false;
            }
            const sl::BaseStructure* inputs[]{ &viewport };
            if (!state.evaluateFeature) {
                return false;
            }
            const auto result = state.evaluateFeature(
                    sl::kFeatureDLSS,
                    token,
                    inputs,
                    static_cast<std::uint32_t>(std::size(inputs)),
                    frame.context);
            state.dlaaResourcesAllocated = true;
            return result == sl::Result::eOk;
        }
    }

    bool initializeStreamlineBeforeDevice() noexcept
    {
        try {
            std::scoped_lock lock(state.mutex);
            if (state.initialized.load(std::memory_order_acquire)) {
                return true;
            }
            if (state.initializationAttempted) {
                return false;
            }
            state.initializationAttempted = true;
            const auto base = pluginDirectory();
            state.directory = base / L"Streamline";
            if (base.empty() || !verifyRuntimeFiles(state.directory)) {
                return false;
            }
            logging::info(
                "DLAA Streamline runtime files are present; core API and feature support will be negotiated.");

            const auto interposerPath =
                state.directory / L"sl.interposer.dll";
            logging::info(
                "DLAA entering LoadLibraryExW for '{}'.",
                interposerPath.string());
            state.interposer = LoadLibraryExW(
                interposerPath.c_str(),
                nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!state.interposer || !resolveCoreFunctions()) {
                logging::error(
                    "DLAA could not load the complete Streamline 2.12 core API from '{}'.",
                    interposerPath.string());
                return false;
            }
            logging::info(
                "DLAA loaded the Streamline interposer and resolved its complete core API.");

            const wchar_t* pluginPath = state.directory.c_str();
            sl::Preferences preferences{};
            preferences.pathsToPlugins = &pluginPath;
            preferences.numPathsToPlugins = 1;
            preferences.flags = sl::PreferenceFlags::eUseManualHooking |
                sl::PreferenceFlags::eUseFrameBasedResourceTagging;
            preferences.featuresToLoad = kRequestedFeatures.data();
            preferences.numFeaturesToLoad = static_cast<std::uint32_t>(
                kRequestedFeatures.size());
            preferences.logLevel = sl::LogLevel::eDefault;
            preferences.logMessageCallback = streamlineLogCallback;
            preferences.engine = sl::EngineType::eCustom;
            preferences.engineVersion = "FO4VR-1.2.72-CS-0.2.0";
            preferences.projectId = kProjectId;
            preferences.renderAPI = sl::RenderAPI::eD3D11;

            logging::info("DLAA entering slInit.");
            const auto result = state.init(preferences, sl::kSDKVersion);
            logging::info(
                "DLAA returned from slInit with result {}.",
                static_cast<std::uint32_t>(result));
            state.initializationResult.store(
                static_cast<std::uint32_t>(result),
                std::memory_order_release);
            if (result != sl::Result::eOk) {
                logging::error(
                    "DLAA Streamline initialization failed with result {}.",
                    static_cast<std::uint32_t>(result));
                return false;
            }
            state.initialized.store(true, std::memory_order_release);
            logging::info(
                "DLAA initialized the signed Streamline 2.12 runtime before D3D11 device creation (manual hooking, frame-based tags).");
            return true;
        } catch (const std::exception& error) {
            logging::error(
                "DLAA Streamline bootstrap failed closed: {}",
                error.what());
        } catch (...) {
            logging::error(
                "DLAA Streamline bootstrap failed closed with an unknown exception.");
        }
        return false;
    }

    bool bindStreamlineDeviceAndSwapChain(
        IDXGIAdapter* requestedAdapter,
        ID3D11Device* device,
        IDXGISwapChain** swapChain) noexcept
    {
        try {
            std::scoped_lock lock(state.mutex);
            if (!state.initialized.load(std::memory_order_acquire) || !device ||
                !swapChain || !*swapChain || !state.setD3DDevice ||
                !state.upgradeInterface) {
                return false;
            }

        const auto deviceResult = state.setD3DDevice(device);
        state.deviceResult.store(
            static_cast<std::uint32_t>(deviceResult),
            std::memory_order_release);
        if (deviceResult != sl::Result::eOk) {
            logging::error(
                "DLAA Streamline rejected the native D3D11 device with result {}.",
                static_cast<std::uint32_t>(deviceResult));
            return false;
        }
        state.deviceBound.store(true, std::memory_order_release);

        auto* upgraded = static_cast<void*>(*swapChain);
        const auto upgradeResult = state.upgradeInterface(&upgraded);
        if (upgradeResult != sl::Result::eOk || !upgraded) {
            logging::error(
                "DLAA Streamline could not upgrade the presentation interface; result {}. DLAA remains disabled so presentCommon ownership cannot be lost.",
                static_cast<std::uint32_t>(upgradeResult));
            return false;
        }
        *swapChain = static_cast<IDXGISwapChain*>(upgraded);
        state.swapChainUpgraded.store(true, std::memory_order_release);

        const auto adapter = resolveAdapter(requestedAdapter, device);
        DXGI_ADAPTER_DESC description{};
        if (!adapter || FAILED(adapter->GetDesc(&description))) {
            logging::error(
                "DLAA Streamline could not resolve the native adapter LUID.");
            return false;
        }
        sl::AdapterInfo adapterInfo{};
        adapterInfo.deviceLUID = reinterpret_cast<std::uint8_t*>(
            &description.AdapterLuid);
        adapterInfo.deviceLUIDSizeInBytes = sizeof(description.AdapterLuid);

        bool loaded{};
        const auto loadedResult = state.isFeatureLoaded(
            sl::kFeatureDLSS,
            loaded);
        state.featureLoaded.store(
            loadedResult == sl::Result::eOk && loaded,
            std::memory_order_release);
        const auto supportedResult = state.isFeatureSupported(
            sl::kFeatureDLSS,
            adapterInfo);
        state.supportResult.store(
            static_cast<std::uint32_t>(supportedResult),
            std::memory_order_release);
        state.featureSupported.store(
            supportedResult == sl::Result::eOk,
            std::memory_order_release);
        const auto functionsBound = loadedResult == sl::Result::eOk &&
            loaded && supportedResult == sl::Result::eOk &&
            bindDlssFunctions();
        state.featureFunctionsBound.store(
            functionsBound,
            std::memory_order_release);
        if (!functionsBound) {
            sl::FeatureRequirements requirements{};
            const auto requirementsResult = state.getFeatureRequirements ?
                state.getFeatureRequirements(
                    sl::kFeatureDLSS,
                    requirements) : sl::Result::eErrorMissingOrInvalidAPI;
            logging::warn(
                "DLAA Streamline is bound but DLSS is unavailable; loadedResult={}, loaded={}, supportResult={}, requirementsResult={}. Vanilla TAA remains active.",
                static_cast<std::uint32_t>(loadedResult),
                loaded,
                static_cast<std::uint32_t>(supportedResult),
                static_cast<std::uint32_t>(requirementsResult));
            return false;
        }

            logging::info(
                "DLAA Streamline bound the native D3D11 device, upgraded the swapchain, and verified DLSS support (vendor=0x{:04X}, device=0x{:04X}).",
                description.VendorId,
                description.DeviceId);
            return true;
        } catch (const std::exception& error) {
            logging::error(
                "DLAA Streamline device binding failed closed: {}",
                error.what());
        } catch (...) {
            logging::error(
                "DLAA Streamline device binding failed closed with an unknown exception.");
        }
        return false;
    }

    bool evaluateStreamlineDlaa(
        const StreamlineDlaaFrame& frame) noexcept
    {
        try {
            std::scoped_lock lock(state.mutex);
            if (!frame.context || frame.inputWidth == 0 ||
                frame.inputHeight == 0 || frame.outputWidth == 0 ||
                frame.outputHeight == 0 ||
                !state.initialized.load(std::memory_order_acquire) ||
                !state.deviceBound.load(std::memory_order_acquire) ||
                !state.featureSupported.load(std::memory_order_acquire) ||
                !state.featureFunctionsBound.load(std::memory_order_acquire) ||
                !state.getNewFrameToken || !state.setConstants ||
                !state.setTagForFrame || !state.evaluateFeature) {
                return false;
            }
            sl::FrameToken* token{};
            const auto frameIndex = frame.frameIndex;
            if (state.getNewFrameToken(token, &frameIndex) !=
                    sl::Result::eOk ||
                !token) {
                return false;
            }
            const auto leftEvaluated = evaluateEye(frame, *token, 0);
            if (frame.timingAfterEye[0]) {
                frame.context->End(frame.timingAfterEye[0]);
            }
            const auto rightEvaluated = leftEvaluated &&
                evaluateEye(frame, *token, 1);
            if (frame.timingAfterEye[1]) {
                frame.context->End(frame.timingAfterEye[1]);
            }
            return leftEvaluated && rightEvaluated;
        } catch (const std::exception& error) {
            logging::error(
                "DLAA Streamline stereo evaluation failed closed: {}",
                error.what());
        } catch (...) {
            logging::error(
                "DLAA Streamline stereo evaluation failed closed with an unknown exception.");
        }
        return false;
    }

    StreamlineOptimalSettings queryStreamlineOptimalSettings(
        Mode mode,
        std::uint32_t outputWidth,
        std::uint32_t outputHeight) noexcept
    {
        if (outputWidth == 0 || outputHeight == 0) {
            return {};
        }
        if (!isDlssMode(mode)) {
            return {
                .valid = true,
                .renderWidth = outputWidth,
                .renderHeight = outputHeight,
                .minimumWidth = outputWidth,
                .minimumHeight = outputHeight,
                .maximumWidth = outputWidth,
                .maximumHeight = outputHeight,
            };
        }
        try {
            std::scoped_lock lock(state.mutex);
            if (!state.featureFunctionsBound.load(
                    std::memory_order_acquire) ||
                !state.dlssGetOptimalSettings) {
                return {};
            }
            sl::DLSSOptions options{};
            options.mode = toStreamlineMode(mode);
            options.outputWidth = outputWidth;
            options.outputHeight = outputHeight;
            options.colorBuffersHDR = sl::Boolean::eFalse;
            options.useAutoExposure = sl::Boolean::eTrue;
            options.alphaUpscalingEnabled = sl::Boolean::eFalse;
            sl::DLSSOptimalSettings optimal{};
            if (state.dlssGetOptimalSettings(options, optimal) !=
                    sl::Result::eOk ||
                optimal.optimalRenderWidth == 0 ||
                optimal.optimalRenderHeight == 0 ||
                optimal.renderWidthMin == 0 ||
                optimal.renderHeightMin == 0 ||
                optimal.renderWidthMax == 0 ||
                optimal.renderHeightMax == 0) {
                return {};
            }
            return {
                .valid = true,
                .renderWidth = optimal.optimalRenderWidth,
                .renderHeight = optimal.optimalRenderHeight,
                .minimumWidth = optimal.renderWidthMin,
                .minimumHeight = optimal.renderHeightMin,
                .maximumWidth = optimal.renderWidthMax,
                .maximumHeight = optimal.renderHeightMax,
            };
        } catch (...) {
            logging::warn(
                "Upscaling could not query the DLSS optimal render extent; the requested mode remains fail-closed.");
        }
        return {};
    }

    void releaseStreamlineDlaaResources() noexcept
    {
        try {
            std::scoped_lock lock(state.mutex);
            if (!state.freeResources ||
                !state.featureLoaded.load(std::memory_order_acquire) ||
                !state.dlaaResourcesAllocated) {
                return;
            }
            for (std::uint32_t eye = 0; eye < 2; ++eye) {
                const sl::ViewportHandle viewport(eye);
                if (state.dlssSetOptions) {
                    sl::DLSSOptions disabled{};
                    disabled.mode = sl::DLSSMode::eOff;
                    (void)state.dlssSetOptions(viewport, disabled);
                }
                const auto result = state.freeResources(
                    sl::kFeatureDLSS,
                    viewport);
                if (result != sl::Result::eOk) {
                    logging::warn(
                        "DLAA Streamline could not free eye {} temporal resources; result {}.",
                        eye,
                        static_cast<std::uint32_t>(result));
                }
            }
            state.dlaaResourcesAllocated = false;
            state.dlssOptions = {};
        } catch (...) {
            logging::warn(
                "DLAA Streamline resource release failed closed.");
        }
    }

    StreamlineSnapshot streamlineSnapshot() noexcept
    {
        return {
            .initialized = state.initialized.load(std::memory_order_acquire),
            .deviceBound = state.deviceBound.load(std::memory_order_acquire),
            .swapChainUpgraded = state.swapChainUpgraded.load(
                std::memory_order_acquire),
            .featureLoaded = state.featureLoaded.load(
                std::memory_order_acquire),
            .featureSupported = state.featureSupported.load(
                std::memory_order_acquire),
            .featureFunctionsBound = state.featureFunctionsBound.load(
                std::memory_order_acquire),
            .initializationResult = state.initializationResult.load(
                std::memory_order_acquire),
            .deviceResult = state.deviceResult.load(
                std::memory_order_acquire),
            .supportResult = state.supportResult.load(
                std::memory_order_acquire),
        };
    }
}
