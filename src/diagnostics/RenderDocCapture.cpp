#include "PCH.h"
#include "diagnostics/RenderDocCapture.h"
#include "support/Logger.h"
#include "../../extern/RenderDoc/renderdoc_app.h"
#include <RobCoPALM/Client.h>
#include <RobCoPALM/Developer.h>

#include <array>
#include <filesystem>
#include <mutex>

namespace csah::diagnostics
{
    namespace
    {
        struct CaptureState
        {
            // Only the settings worker writes the queued path. The renderer
            // uses try_lock, then owns the API and completion state entirely.
            std::mutex mutex;
            std::array<char, 4096> queuedPath{};
            std::atomic_bool pending{};
            std::atomic_bool busy{};
            HMODULE module{};
            RENDERDOC_API_1_1_2* api{};
            std::uint32_t previousCaptureCount{};
            bool awaitingCompletion{};
            std::uint64_t requestSequence{};
            std::atomic_bool wheelRequest{};
            palm::api::developer::Handle wheelAction{};

            ~CaptureState()
            {
                if (module) FreeLibrary(module);
            }
        };

        CaptureState& state()
        {
            static CaptureState value;
            return value;
        }
    }

    void initializeDeveloperCapture(const wchar_t* csahIniPath) noexcept
    {
        try {
            if(!csahIniPath || !*csahIniPath)return;
            const auto palmIni=std::filesystem::path(csahIniPath).parent_path().parent_path()/L"RobCo_PALM"/L"PALM.ini";
            if(GetPrivateProfileIntW(L"Developer",L"bEnabled",0,palmIni.c_str())==0)return;
            std::array<wchar_t,1024> programFiles{};
            const auto count=GetEnvironmentVariableW(L"ProgramFiles",programFiles.data(),static_cast<DWORD>(programFiles.size()));
            if(!count || count>=programFiles.size()) {logging::warn("Developer GPU capture could not resolve the installed Program Files directory.");return;}
            const auto module=std::filesystem::path(programFiles.data())/L"RenderDoc"/L"renderdoc.dll";
            (void)initializeRenderDocCapture(module.c_str());
        }catch(...){logging::warn("Developer GPU capture initialization failed.");}
    }

    void registerDeveloperCapture() noexcept
    {
        try {
            auto& capture=state();if(capture.wheelAction)return;
            const auto module=GetModuleHandleW(palm::api::kModuleName);
            const auto query=module?reinterpret_cast<palm::api::QueryInterfaceV1>(GetProcAddress(module,palm::api::kQueryExportName)):nullptr;
            const palm::api::developer::Api* api{};
            if(palm::api::acquire(query,api)!=palm::api::Result::Ok || !api->enabled())return;
            palm::api::developer::Action action;
            strcpy_s(action.id,"csah.lighting.gpu");strcpy_s(action.label,"CSAH: Capture lighting data");
            action.invoke=[](void*) noexcept {state().wheelRequest.store(true,std::memory_order_release);};
            const auto result=api->registerAction(&action,&capture.wheelAction);
            if(result==palm::api::Result::Ok)logging::info("CSAH lighting GPU data capture registered in PALM's Developer wheel.");
            else logging::warn("CSAH Developer action registration rejected ({}); existing actions preserved. If full, ask which action may be removed.",static_cast<unsigned>(result));
        }catch(...){logging::warn("CSAH Developer action registration failed.");}
    }

    void serviceDeveloperCapture(const wchar_t* csahIniPath) noexcept
    {
        if(state().wheelRequest.exchange(false,std::memory_order_acq_rel))
            (void)requestRenderDocCapture(L"F:/fo4dev/misc/CSAH-lighting-captures",csahIniPath);
    }

    bool initializeRenderDocCapture(const wchar_t* modulePath) noexcept
    {
        try {
            auto& capture = state();
            if (capture.api) return true;
            const std::filesystem::path path(modulePath ? modulePath : L"");
            if (!path.is_absolute() || path.filename() != L"renderdoc.dll") {
                logging::warn("GPU data capture startup rejected: the backend must be an absolute installed renderdoc.dll.");
                return false;
            }
            HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            const auto getApi = module ? reinterpret_cast<pRENDERDOC_GetAPI>(
                GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
            RENDERDOC_API_1_1_2* api{};
            if (!getApi || getApi(eRENDERDOC_API_Version_1_1_2,
                    reinterpret_cast<void**>(&api)) != 1 || !api) {
                if (module) FreeLibrary(module);
                logging::warn("GPU data capture startup failed: installed RenderDoc API unavailable (Windows error {}).", GetLastError());
                return false;
            }
            capture.module = module;
            capture.api = api;
            api->SetCaptureKeys(nullptr, 0);
            api->SetFocusToggleKeys(nullptr, 0);
            api->MaskOverlayBits(0, 0);
            logging::info("GPU data capture backend enabled by PALM Developer mode; only explicit Developer wheel actions capture data. Disable PALM.ini [Developer] bEnabled and restart to unload it.");
            return true;
        } catch (...) {
            logging::warn("GPU data capture startup could not load the configured backend.");
            return false;
        }
    }

    bool isRenderDocCreationHook(const void* address) noexcept
    {
        // Do not relax the native import gate for unrelated overlays/hooks.
        HMODULE owner{};
        if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(address), &owner)) return false;
        const auto loaded = GetModuleHandleW(L"renderdoc.dll");
        const auto getApi = owner == loaded ? reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(owner, "RENDERDOC_GetAPI")) : nullptr;
        RENDERDOC_API_1_1_2* api{};
        const bool valid = getApi && getApi(eRENDERDOC_API_Version_1_1_2,
            reinterpret_cast<void**>(&api)) == 1 && api;
        FreeLibrary(owner);
        return valid;
    }

    bool requestRenderDocCapture(const wchar_t* directory, const wchar_t* settingsPath) noexcept
    {
        try {
            if (!directory || !*directory) {
                logging::warn("RenderDoc capture rejected: an absolute output directory is required.");
                return false;
            }
            const std::filesystem::path path(directory);
            if (!path.is_absolute()) {
                logging::warn("RenderDoc capture rejected: the output directory must be absolute.");
                return false;
            }
            std::error_code error;
            std::filesystem::create_directories(path, error);
            if (error || !std::filesystem::is_directory(path, error) || error) {
                logging::warn("RenderDoc capture rejected: the configured output directory is unavailable.");
                return false;
            }
            auto& capture = state();
            const std::scoped_lock lock(capture.mutex);
            if (capture.busy.load(std::memory_order_acquire)) {
                logging::warn("RenderDoc already has a queued manual capture request.");
                return false;
            }
            const auto prefixPath = path / (L"CSAH_" + std::to_wstring(GetCurrentProcessId()) +
                L"_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(++capture.requestSequence));
            const auto prefix = prefixPath.wstring();
            std::array<char, 4096> utf8{};
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, prefix.c_str(), -1,
                    utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr) == 0) {
                logging::warn("RenderDoc capture rejected: the output path is invalid or too long.");
                return false;
            }
            if (settingsPath && *settingsPath) {
                std::filesystem::copy_file(settingsPath, prefix + L".settings.ini", error);
                if (error) {
                    logging::warn("GPU data capture rejected: could not save the settings sidecar: {}.", error.message());
                    return false;
                }
            }
            capture.queuedPath = utf8;
            capture.busy.store(true, std::memory_order_release);
            capture.pending.store(true, std::memory_order_release);
            return true;
        } catch (...) {
            logging::warn("RenderDoc capture could not prepare its manual request.");
            return false;
        }
    }

    void processRenderDocCapture() noexcept
    {
        auto& capture = state();
        if (capture.awaitingCompletion && capture.api) {
            const auto count = capture.api->GetNumCaptures();
            if (count > capture.previousCaptureCount) {
                std::array<char, 4096> path{};
                std::uint32_t length{};
                std::uint64_t timestamp{};
                if (capture.api->GetCapture(count - 1, nullptr, &length, &timestamp) &&
                    length < path.size() &&
                    capture.api->GetCapture(count - 1, path.data(), nullptr, nullptr))
                    logging::info("RenderDoc manual frame capture completed: {}", path.data());
                else logging::info("RenderDoc manual frame capture completed (capture {}).", count);
                capture.awaitingCompletion = false;
                capture.busy.store(false, std::memory_order_release);
            }
        }
        if (!capture.pending.load(std::memory_order_acquire)) return;
        std::unique_lock lock(capture.mutex, std::try_to_lock);
        if (!lock.owns_lock()) return;
        if (!capture.pending.exchange(false, std::memory_order_acq_rel)) return;
        const auto path = capture.queuedPath;
        lock.unlock();
        if (!capture.api) {
            if (!GetModuleHandleExW(0, L"renderdoc.dll", &capture.module)) {
                logging::warn("GPU capture unavailable: enable PALM.ini [Developer] bEnabled and restart, then select CSAH: Capture lighting data in the Developer wheel.");
                capture.busy.store(false, std::memory_order_release);
                return;
            }
            const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
                GetProcAddress(capture.module, "RENDERDOC_GetAPI"));
            if (!getApi || getApi(eRENDERDOC_API_Version_1_1_2,
                    reinterpret_cast<void**>(&capture.api)) != 1 || !capture.api) {
                logging::warn("RenderDoc capture unavailable: the loaded module does not expose API 1.1.2.");
                capture.api = nullptr;
                FreeLibrary(capture.module);
                capture.module = nullptr;
                capture.busy.store(false, std::memory_order_release);
                return;
            }
        }
        if (capture.api->IsFrameCapturing()) {
            logging::warn("RenderDoc is already capturing a frame; this extra request was discarded.");
            capture.busy.store(false, std::memory_order_release);
            return;
        }
        capture.api->SetCaptureFilePathTemplate(path.data());
        capture.previousCaptureCount = capture.api->GetNumCaptures();
        capture.awaitingCompletion = true;
        capture.api->TriggerCapture();
        logging::info("RenderDoc manual capture requested for the next presented frame: {}", path.data());
    }
}
