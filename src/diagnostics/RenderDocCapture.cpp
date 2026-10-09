#include "PCH.h"
#include "diagnostics/RenderDocCapture.h"
#include "support/Logger.h"
#include "../../extern/RenderDoc/renderdoc_app.h"

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
            HMODULE module{};
            RENDERDOC_API_1_1_2* api{};
            std::uint32_t previousCaptureCount{};
            bool awaitingCompletion{};

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

    bool requestRenderDocCapture(const wchar_t* directory) noexcept
    {
        try {
            if (!directory || !*directory) {
                logging::warn("RenderDoc capture rejected: set [Diagnostics] sCaptureDirectory to an absolute output directory.");
                return false;
            }
            const std::filesystem::path path(directory);
            if (!path.is_absolute()) {
                logging::warn("RenderDoc capture rejected: sCaptureDirectory must be absolute.");
                return false;
            }
            std::error_code error;
            std::filesystem::create_directories(path, error);
            if (error || !std::filesystem::is_directory(path, error) || error) {
                logging::warn("RenderDoc capture rejected: the configured output directory is unavailable.");
                return false;
            }
            const auto prefix = (path / L"CSAH").wstring();
            std::array<char, 4096> utf8{};
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, prefix.c_str(), -1,
                    utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr) == 0) {
                logging::warn("RenderDoc capture rejected: the output path is invalid or too long.");
                return false;
            }
            auto& capture = state();
            const std::scoped_lock lock(capture.mutex);
            if (capture.pending.load(std::memory_order_relaxed)) {
                logging::warn("RenderDoc already has a queued manual capture request.");
                return false;
            }
            capture.queuedPath = utf8;
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
                logging::warn("RenderDoc capture unavailable: launch the game through RenderDoc, then request a frame from CSAH.");
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
                return;
            }
        }
        if (capture.api->IsFrameCapturing()) {
            logging::warn("RenderDoc is already capturing a frame; this extra request was discarded.");
            return;
        }
        capture.api->SetCaptureFilePathTemplate(path.data());
        capture.previousCaptureCount = capture.api->GetNumCaptures();
        capture.awaitingCompletion = true;
        capture.api->TriggerCapture();
        logging::info("RenderDoc manual capture requested for the next presented frame: {}", path.data());
    }
}
