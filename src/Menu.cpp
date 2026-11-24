#include "Menu.h"
#include "Feature.h"
#include "ShaderCache.h"
#include "ShaderReplacer.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

// Forward declare the Win32 WndProc handler from imgui backend
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Menu& Menu::GetSingleton() {
    static Menu instance;
    return instance;
}

void Menu::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (initialized) {
        spdlog::warn("Menu::Initialize called but menu is already initialized");
        return;
    }

    if (!hwnd || !device || !context) {
        spdlog::error("Menu::Initialize called with null parameter(s) — hwnd:{} device:{} context:{}",
            hwnd != nullptr, device != nullptr, context != nullptr);
        return;
    }

    spdlog::info("Initializing ImGui menu overlay...");

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // Disable imgui.ini persistence — we manage settings ourselves

    // Style setup — dark theme with slight adjustments for readability in VR
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.Alpha = 0.95f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Slightly larger default font for VR readability
    io.FontGlobalScale = 1.25f;

    // Initialize platform/renderer backends
    if (!ImGui_ImplWin32_Init(hwnd)) {
        spdlog::error("ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        return;
    }

    if (!ImGui_ImplDX11_Init(device, context)) {
        spdlog::error("ImGui_ImplDX11_Init failed");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    initialized = true;
    spdlog::info("ImGui menu overlay initialized successfully");
}

void Menu::Shutdown() {
    if (!initialized) {
        return;
    }

    spdlog::info("Shutting down ImGui menu overlay...");

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    initialized = false;
    isVisible = false;

    spdlog::info("ImGui menu overlay shut down");
}

void Menu::Draw() {
    if (!initialized || !isVisible) {
        return;
    }

    // Start new ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Main settings window
    ImGui::SetNextWindowSize(ImVec2(480.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Community Shaders at Home (CSAH) VR", &isVisible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Community Shaders at Home (CSAH) VR v0.0.7");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawFeatureList();
        }

        if (ImGui::CollapsingHeader("Shader Cache")) {
            DrawShaderCacheStatus();
        }
    }
    ImGui::End();

    // Render
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Menu::Toggle() {
    isVisible = !isVisible;
    spdlog::info("Menu overlay toggled: {}", isVisible ? "visible" : "hidden");

    // When showing the menu, capture mouse input; when hiding, release it
    if (initialized) {
        ImGuiIO& io = ImGui::GetIO();
        if (isVisible) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
    }
}

void Menu::ProcessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!initialized) {
        return;
    }

    // Forward input to ImGui when the menu is visible
    if (isVisible) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
    }
}

void Menu::DrawFeatureList() {
    auto& features = Feature::GetFeatureList();

    if (features.empty()) {
        ImGui::TextDisabled("No features registered");
        return;
    }

    for (auto* feature : features) {
        if (!feature) {
            continue;
        }

        ImGui::PushID(feature);

        // Enabled checkbox
        bool enabled = feature->enabled;
        if (ImGui::Checkbox(feature->GetName().c_str(), &enabled)) {
            feature->enabled = enabled;
            spdlog::info("Feature '{}' {} via menu", feature->GetName(), enabled ? "enabled" : "disabled");
        }

        // Status indicator
        ImGui::SameLine();
        if (feature->loaded) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[Loaded]");
        } else if (!feature->enabled) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Disabled]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Error]");
        }

        // Per-feature settings in a tree node
        if (feature->loaded) {
            ImGui::Indent(20.0f);
            if (ImGui::TreeNode("Settings")) {
                feature->DrawSettings();
                ImGui::TreePop();
            }
            ImGui::Unindent(20.0f);
        }

        ImGui::PopID();
    }
}

void Menu::DrawShaderCacheStatus() {
    auto& cache = ShaderCache::GetSingleton();

    ImGui::Text("Shader Cache Status");
    ImGui::Separator();

    ImGui::Text("Compiled Shaders: %u", cache.stats.compiled.load());
    ImGui::Text("Cache Hits: %u", cache.stats.cacheHits.load());
    ImGui::Text("Cache Misses: %u", cache.stats.cacheMisses.load());

    uint32_t errors = cache.stats.errors.load();
    if (errors > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Compilation Errors: %u", errors);
    } else {
        ImGui::Text("Compilation Errors: 0");
    }

    ImGui::Spacing();
    ImGui::Text("Disk Cache: %s", cache.diskCacheEnabled ? "Enabled" : "Disabled");
    ImGui::Text("Cache Path: %s", cache.diskCachePath.string().c_str());

    if (ImGui::Button("Clear Shader Cache")) {
        cache.Clear();
        spdlog::info("Shader cache cleared from menu");
    }

    ImGui::Spacing();
    if (ImGui::Button("Restore Vanilla Shaders")) {
        ShaderReplacer::GetSingleton().RestoreAllVanillaPS();
        spdlog::info("Vanilla shaders restored from menu");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore all original shaders. Useful for A/B comparison.");
    }
}
