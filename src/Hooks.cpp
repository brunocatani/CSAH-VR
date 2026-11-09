#include "Hooks.h"
#include "Globals.h"
#include "State.h"
#include "Feature.h"
#include "settings/SettingsFile.h"
#include "Menu.h"
#include "EngineFixes.h"
#include "ShaderCache.h"
#include <imgui.h>
#include <atomic>
#include <unordered_set>
#include <d3dcompiler.h>
#include "ParallaxHashes.h"

namespace {

    // ---- Forward declarations for deferred D3D init ----
    static bool s_deferredD3DInitDone = false;
    static void TryDeferredD3DInit();

    // ---- Draw-time PS replacement cache ----
    static std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D11PixelShader>> s_compiledPS;
    static std::mutex s_compiledPSMutex;

    // ---- Visual test shader (flat red) ----
    static Microsoft::WRL::ComPtr<ID3D11PixelShader> s_testRedPS;
    static bool s_testRedEnabled = false;  // toggle with F9

    static void CompileTestRedPS() {
        // Bright magenta — unmistakable visual indicator for BSLightingShader surfaces
        const char* hlsl =
            "struct PS_OUT {\n"
            "  float4 color0 : SV_Target0;\n"
            "  float4 color1 : SV_Target1;\n"
            "  float4 color2 : SV_Target2;\n"
            "  float4 color3 : SV_Target3;\n"
            "  float4 color4 : SV_Target4;\n"
            "};\n"
            "PS_OUT PSMain() {\n"
            "  PS_OUT o;\n"
            "  o.color0 = float4(1, 0, 1, 1);\n"   // magenta albedo
            "  o.color1 = float4(0.5, 0.5, 1, 0);\n" // up-facing normal
            "  o.color2 = float4(0, 0, 0, 0);\n"
            "  o.color3 = float4(0, 0, 0, 0);\n"
            "  o.color4 = float4(0, 0, 0, 0);\n"
            "  return o;\n"
            "}\n";

        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "TestRedPS", nullptr, nullptr,
                                "PSMain", "ps_5_0", 0, 0, &blob, &errors);
        if (FAILED(hr)) {
            if (errors) spdlog::error("Test PS compile error: {}", (char*)errors->GetBufferPointer());
            return;
        }

        hr = Globals::GetDevice()->CreatePixelShader(
            blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s_testRedPS);
        if (SUCCEEDED(hr)) {
            spdlog::info("Test RED pixel shader compiled and created successfully");
        } else {
            spdlog::error("Failed to create test PS: {:#x}", (uint32_t)hr);
        }
    }

    // ---- Parallax PS identification (from DXBC hash matching) ----
    static std::unordered_set<ID3D11PixelShader*> s_parallaxPS;
    static std::unordered_set<uint64_t> s_parallaxHashes;
    static bool s_parallaxHashesLoaded = false;

    // ---- Scatter table reverse map: PS pointer -> technique ID ----
    static std::unordered_map<ID3D11PixelShader*, uint32_t> s_psTechIDMap;
    static std::mutex s_psTechIDMutex;
    static uint32_t s_lastReverseMapSize = 0;

    // PS scatter table entry layout (from Ghidra decompilation of BSShader::BeginTechnique):
    //   +0x00: BSGraphics::PixelShader* value  (8 bytes)
    //   +0x08: entry_type*              next   (8 bytes, nullptr=empty, sentinel=end)
    // Total: 0x10 (16 bytes) per entry.
    //
    // Key comparison: entry->value->id == techID  (id is at offset 0x00 of BSGraphics::PixelShader)
    // D3D shader:     entry->value->shader        (at offset 0x08 of BSGraphics::PixelShader)
    //
    // CRITICAL: The engine checks entry->next != nullptr to detect empty slots.
    // Empty slots may have STALE non-zero values in entry->value — do NOT read value if next is null.
    struct PSScatterEntry {
        void* value;    // BSGraphics::PixelShader*
        void* next;     // chain pointer (nullptr=empty, sentinel=end-of-chain)
    };

    static void RebuildReverseMap()
    {
        auto bsShader = Globals::GetBSLightingShader();
        if (!bsShader) return;

        // PS BSTScatterTable offsets (from BSShader base):
        //   +0xBC: capacity (uint32, always power of 2)
        //   +0xC8: sentinel pointer
        //   +0xD8: entries array pointer
        uint32_t capacity = *reinterpret_cast<uint32_t*>(bsShader + 0xBC);
        auto*    sentinel = *reinterpret_cast<void**>(bsShader + 0xC8);
        auto*    entries  = reinterpret_cast<PSScatterEntry*>(
                            *reinterpret_cast<uintptr_t*>(bsShader + 0xD8));

        if (!entries || capacity == 0) return;

        uint32_t mask = capacity - 1;  // capacity is always power of 2

        // Diagnostic on first successful call
        static bool s_loggedOnce = false;
        if (!s_loggedOnce) {
            s_loggedOnce = true;
            spdlog::info("RebuildReverseMap: obj={:#x} capacity={} sentinel={} entries={}",
                         bsShader, capacity, fmt::ptr(sentinel), fmt::ptr(entries));
        }

        std::lock_guard<std::mutex> lock(s_psTechIDMutex);
        uint32_t newEntries = 0;

        for (uint32_t bucket = 0; bucket <= mask; ++bucket) {
            auto* entry = &entries[bucket];  // each entry is 16 bytes (sizeof PSScatterEntry)

            // Engine checks: entry->next != nullptr means slot is populated
            if (!entry->next) continue;  // empty slot — DO NOT read entry->value

            uint32_t chainLen = 0;
            while (entry && entry != sentinel && chainLen < 256) {
                auto* shaderObj = entry->value;  // BSGraphics::PixelShader*
                if (shaderObj) {
                    // id at shaderObj+0x00, D3D shader at shaderObj+0x08
                    uint32_t techID = *reinterpret_cast<uint32_t*>(shaderObj);
                    auto*    d3dPS  = *reinterpret_cast<ID3D11PixelShader**>(
                                      reinterpret_cast<uintptr_t>(shaderObj) + 0x08);
                    if (d3dPS && s_psTechIDMap.find(d3dPS) == s_psTechIDMap.end()) {
                        s_psTechIDMap[d3dPS] = techID;
                        ++newEntries;
                    }
                }

                // Follow chain: next is at entry+0x08
                auto* next = reinterpret_cast<PSScatterEntry*>(entry->next);
                if (next == sentinel || !next) break;  // end of chain
                entry = next;
                ++chainLen;
            }
        }

        if (newEntries > 0) {
            spdlog::info("RebuildReverseMap: {} new entries, {} total", newEntries, s_psTechIDMap.size());
        }
        s_lastReverseMapSize = static_cast<uint32_t>(s_psTechIDMap.size());
    }

    // BSLightingShader technique ID helpers (inlined from Ghidra FUN_14293a4f0 / FUN_14293a520)
    static uint32_t ExtractPSTechID(uint32_t combined) {
        uint32_t id = combined;
        if ((id & 4) == 0) id &= ~2u;
        return id | 1;
    }

    // ---- Hook thunks ----

    // BSLightingShader::BeginTechnique (vtable[4]) — the MAIN draw-time hook
    // This is BSLightingShader's own override, NOT the base 6-param version
    // Signature: char(this, combinedTechID, renderPass)
    static char __fastcall Hook_LightingBeginTechnique(void* shader, uint32_t techID, void* renderPass)
    {
        // Fallback deferred D3D init
        if (!s_deferredD3DInitDone) {
            TryDeferredD3DInit();
        }

        // Call the original BSLightingShader::BeginTechnique
        char result = Hooks::OriginalLightingBeginTechnique(shader, techID, renderPass);

        // Track current state for feature callbacks
        auto& state = State::GetSingleton();
        state.currentShader = shader;
        state.currentTechniqueID = techID;

        // Log first few calls to confirm this hook fires
        static uint32_t s_logCount = 0;
        if (s_logCount < 5) {
            spdlog::info("LightingBT[{}]: tech={:#x} result={} parallax={}",
                         s_logCount, techID, (int)result, (techID & 0x0800) != 0);
            ++s_logCount;
        }

        // Draw-time PS swap for parallax techniques (Skyrim CS approach)
        if (result && (techID & 0x0800) && Globals::GetContext()) {
            uint32_t psTechID = ExtractPSTechID(techID);

            std::lock_guard<std::mutex> lock(s_compiledPSMutex);
            auto it = s_compiledPS.find(psTechID);

            if (it != s_compiledPS.end()) {
                // Cache hit — swap if we have a valid shader
                if (it->second) {
                    Globals::GetContext()->PSSetShader(it->second.Get(), nullptr, 0);
                }
                // else: compile failed previously, skip
            } else {
                // First encounter — compile on demand (will stutter, optimize later)
                auto& cache = ShaderCache::GetSingleton();
                auto defines = cache.BuildDefines(8, psTechID, true);
                auto compiled = cache.CompileShader(
                    L"Data/Shaders/Community/Lighting.hlsl", "PSMain", "ps_5_0", defines);

                if (compiled.valid && compiled.ps) {
                    s_compiledPS[psTechID] = compiled.ps;
                    Globals::GetContext()->PSSetShader(compiled.ps.Get(), nullptr, 0);
                    spdlog::info("Draw-time PS swap: tech={:#x} psTech={:#x} — compiled OK",
                                 techID, psTechID);
                } else {
                    s_compiledPS[psTechID] = nullptr;  // mark failed
                    spdlog::warn("Draw-time PS compile FAILED: tech={:#x} psTech={:#x}",
                                 techID, psTechID);
                }
            }
        }

        return result;
    }

    static void __fastcall Hook_LightingSetupGeometry(void* shader, void* renderPass)
    {
        Hooks::OriginalLightingSetupGeometry(shader, renderPass);

        // Log first call to confirm hook fires
        static bool s_loggedOnce = false;
        if (!s_loggedOnce) {
            s_loggedOnce = true;
            spdlog::info("SetupGeometry FIRES! shader={} renderPass={}", fmt::ptr(shader), fmt::ptr(renderPass));
        }

        // Bind shared data CB
        State::GetSingleton().BindSharedData();

        // Feature callbacks
        for (auto* f : Feature::GetFeatureList()) {
            if (f->loaded && f->enabled) {
                f->OnSetupGeometry(renderPass);
            }
        }
    }

    static void TryDeferredD3DInit()
    {
        if (s_deferredD3DInitDone) return;

        if (!Globals::GetDevice()) {
            Globals::Initialize();
            if (!Globals::GetDevice()) return;
        }

        spdlog::info("=== Deferred D3D init (device now available) ===");
        spdlog::info("  Device: {}, Context: {}", fmt::ptr(Globals::GetDevice()), fmt::ptr(Globals::GetContext()));
        s_deferredD3DInitDone = true;

        State::GetSingleton().Initialize();
        Feature::InitializeAll();
        Hooks::InstallRenderHooks();
        Hooks::InstallD3DHooks();
        EngineFixes::ApplyPostLoadFixes();
        EngineFixes::StartCascadeRuntime();

        const auto settingsDirectory = csah::settings_file::resolveDirectory();
        if (!settingsDirectory.empty()) {
            Feature::SaveAllSettings(settingsDirectory / "CommunityShaders.json");
        }
        spdlog::info("=== Deferred D3D init complete ===");
    }

    // ---- ISGN layout classification for PS ----
    // Layout groups that we can replace (GBuffer deferred with TBN + packed UVs)
    enum class PSLayout : uint8_t {
        Unknown = 0,
        Layout3_GBuffer = 3,    // TEXCOORD0-4, COLOR0, EYEINDEX, SV_IsFrontFace (134 POM)
        Layout4_GBuffer = 4,    // Same as #3 but no COLOR0 (117 POM)
        Forward = 10,           // TEXCOORD0 + COLOR0/COLOR1 (forward pass, 1 RT)
        DepthOnly = 11,         // SV_POSITION + EYEINDEX only (depth pre-pass)
    };
    static std::unordered_map<ID3D11PixelShader*, PSLayout> s_psLayoutMap;
    static std::unordered_map<ID3D11PixelShader*, uint8_t> s_psMRTCountMap;  // SV_Target count per PS
    static std::mutex s_psLayoutMutex;

    // Count OSGN SV_Target entries in DXBC bytecode
    static uint8_t CountOSGNTargets(const uint8_t* dxbc, SIZE_T size) {
        if (size < 32 || memcmp(dxbc, "DXBC", 4) != 0) return 0;
        uint32_t numChunks = *reinterpret_cast<const uint32_t*>(dxbc + 28);
        if (numChunks > 32) return 0;

        for (uint32_t ci = 0; ci < numChunks; ++ci) {
            uint32_t chunkRel = *reinterpret_cast<const uint32_t*>(dxbc + 32 + ci * 4);
            if (chunkRel + 8 > size) break;
            if (memcmp(dxbc + chunkRel, "OSGN", 4) != 0) continue;

            uint32_t numElems = *reinterpret_cast<const uint32_t*>(dxbc + chunkRel + 8);
            uint32_t elemBase = chunkRel + 16;
            uint32_t chunkDataStart = chunkRel + 8;
            uint8_t targetCount = 0;

            for (uint32_t i = 0; i < numElems && i < 64; ++i) {
                uint32_t eoff = elemBase + i * 24;
                if (eoff + 24 > size) break;
                uint32_t nameRel = *reinterpret_cast<const uint32_t*>(dxbc + eoff);
                uint32_t nameAbs = chunkDataStart + nameRel;
                if (nameAbs >= size) continue;
                const char* name = reinterpret_cast<const char*>(dxbc + nameAbs);
                if (strncmp(name, "SV_Target", 9) == 0) ++targetCount;
            }
            return targetCount;
        }
        return 0;
    }

    // Classify ISGN layout from DXBC bytecode
    static PSLayout ClassifyISGN(const uint8_t* dxbc, SIZE_T size) {
        if (size < 32 || memcmp(dxbc, "DXBC", 4) != 0) return PSLayout::Unknown;

        uint32_t numChunks = *reinterpret_cast<const uint32_t*>(dxbc + 28);
        if (numChunks > 32) return PSLayout::Unknown;

        // Find ISGN chunk
        for (uint32_t ci = 0; ci < numChunks; ++ci) {
            uint32_t chunkRel = *reinterpret_cast<const uint32_t*>(dxbc + 32 + ci * 4);
            if (chunkRel + 8 > size) break;

            if (memcmp(dxbc + chunkRel, "ISGN", 4) != 0) continue;

            // Parse ISGN elements
            uint32_t numElems = *reinterpret_cast<const uint32_t*>(dxbc + chunkRel + 8);
            if (numElems > 64) return PSLayout::Unknown;

            uint32_t elemBase = chunkRel + 16;
            bool hasTexcoord0 = false, hasTexcoord1 = false, hasTexcoord2 = false;
            bool hasTexcoord3 = false, hasTexcoord4 = false;
            bool hasColor0 = false, hasColor1 = false;
            bool hasEyeIndex = false, hasFrontFace = false;

            uint32_t chunkDataStart = chunkRel + 8;

            for (uint32_t i = 0; i < numElems; ++i) {
                uint32_t eoff = elemBase + i * 24;
                if (eoff + 24 > size) break;

                uint32_t nameRel = *reinterpret_cast<const uint32_t*>(dxbc + eoff);
                uint32_t semIdx  = *reinterpret_cast<const uint32_t*>(dxbc + eoff + 4);
                uint32_t nameAbs = chunkDataStart + nameRel;
                if (nameAbs >= size) continue;

                const char* name = reinterpret_cast<const char*>(dxbc + nameAbs);

                if (strncmp(name, "TEXCOORD", 8) == 0) {
                    if (semIdx == 0) hasTexcoord0 = true;
                    else if (semIdx == 1) hasTexcoord1 = true;
                    else if (semIdx == 2) hasTexcoord2 = true;
                    else if (semIdx == 3) hasTexcoord3 = true;
                    else if (semIdx == 4) hasTexcoord4 = true;
                } else if (strncmp(name, "COLOR", 5) == 0) {
                    if (semIdx == 0) hasColor0 = true;
                    else if (semIdx == 1) hasColor1 = true;
                } else if (strncmp(name, "EYEINDEX", 8) == 0) {
                    hasEyeIndex = true;
                } else if (strncmp(name, "SV_IsFrontFace", 14) == 0) {
                    hasFrontFace = true;
                }
            }

            // Classify based on which semantics are present
            // Layout #3: TEXCOORD0-4, COLOR0, EYEINDEX, SV_IsFrontFace
            if (hasTexcoord0 && hasTexcoord1 && hasTexcoord2 && hasTexcoord3 &&
                hasTexcoord4 && hasColor0 && hasFrontFace) {
                return PSLayout::Layout3_GBuffer;
            }
            // Layout #4: same but no COLOR0
            if (hasTexcoord0 && hasTexcoord1 && hasTexcoord2 && hasTexcoord3 &&
                hasTexcoord4 && !hasColor0 && hasFrontFace) {
                return PSLayout::Layout4_GBuffer;
            }
            // Forward pass: TEXCOORD0 + COLOR0 + COLOR1, no TBN (TEXCOORD1-2)
            if (hasTexcoord0 && hasColor0 && hasColor1 && !hasTexcoord1) {
                return PSLayout::Forward;
            }
            // Depth only: no TEXCOORD at all
            if (!hasTexcoord0 && !hasTexcoord1) {
                return PSLayout::DepthOnly;
            }

            return PSLayout::Unknown;
        }

        return PSLayout::Unknown;
    }

    static PSLayout GetPSLayout(ID3D11PixelShader* ps)
    {
        std::lock_guard<std::mutex> lock(s_psLayoutMutex);
        auto it = s_psLayoutMap.find(ps);
        return (it != s_psLayoutMap.end()) ? it->second : PSLayout::Unknown;
    }

    static uint32_t GetPSTechID(ID3D11PixelShader* ps)
    {
        std::lock_guard<std::mutex> lock(s_psTechIDMutex);
        auto it = s_psTechIDMap.find(ps);
        return (it != s_psTechIDMap.end()) ? it->second : 0;
    }

    // ID3D11Device::CreatePixelShader hook — classifies ISGN layout per PS
    static HRESULT __fastcall Hook_CreatePixelShader(ID3D11Device* device, const void* bytecode,
        SIZE_T bytecodeLength, ID3D11ClassLinkage* classLinkage, ID3D11PixelShader** ppPS)
    {
        HRESULT hr = Hooks::OriginalCreatePixelShader(device, bytecode, bytecodeLength, classLinkage, ppPS);

        if (SUCCEEDED(hr) && ppPS && *ppPS && bytecode && bytecodeLength >= 32) {
            auto* dxbc = reinterpret_cast<const uint8_t*>(bytecode);

            // Classify ISGN layout and count output MRTs
            PSLayout layout = ClassifyISGN(dxbc, bytecodeLength);
            uint8_t mrtCount = CountOSGNTargets(dxbc, bytecodeLength);
            {
                std::lock_guard<std::mutex> lock(s_psLayoutMutex);
                s_psLayoutMap[*ppPS] = layout;
                s_psMRTCountMap[*ppPS] = mrtCount;
            }

            // Hash matching for parallax identification
            if (!s_parallaxHashesLoaded) {
                s_parallaxHashes = ParallaxHashes::GetHashSet();
                s_parallaxHashesLoaded = true;
                spdlog::info("Loaded {} parallax DXBC hashes for matching", s_parallaxHashes.size());
            }

            if (dxbc[0] == 'D' && dxbc[1] == 'X' && dxbc[2] == 'B' && dxbc[3] == 'C') {
                uint64_t hash64 = *reinterpret_cast<const uint64_t*>(dxbc + 4);
                if (s_parallaxHashes.count(hash64) > 0) {
                    s_parallaxPS.insert(*ppPS);
                }
            }

            // Log creation stats
            static uint32_t s_totalPS = 0;
            static uint32_t s_layout3Count = 0, s_layout4Count = 0;
            ++s_totalPS;
            if (layout == PSLayout::Layout3_GBuffer) ++s_layout3Count;
            if (layout == PSLayout::Layout4_GBuffer) ++s_layout4Count;

            if (s_totalPS <= 5 || (s_totalPS % 500 == 0)) {
                spdlog::info("CreatePS[{}]: {} layout={} (L3={}, L4={} total)",
                             s_totalPS, fmt::ptr(*ppPS), (int)layout, s_layout3Count, s_layout4Count);
            }
        }

        return hr;
    }

    static void __fastcall Hook_PSSetShader(ID3D11DeviceContext* context, ID3D11PixelShader* ps,
        ID3D11ClassInstance* const* ppCI, UINT numCI)
    {
        static uint32_t s_psCallCount = 0;
        ++s_psCallCount;

        if (!ps) {
            Hooks::OriginalPSSetShader(context, ps, ppCI, numCI);
            return;
        }

        // Only consider 6-MRT GBuffer deferred shaders for replacement.
        // 5-MRT shaders have different output layout (alpha-only MRT0, no motion vectors).
        PSLayout layout = GetPSLayout(ps);
        bool isGBuffer = (layout == PSLayout::Layout3_GBuffer || layout == PSLayout::Layout4_GBuffer);
        if (isGBuffer) {
            std::lock_guard<std::mutex> lock(s_psLayoutMutex);
            auto it = s_psMRTCountMap.find(ps);
            if (it != s_psMRTCountMap.end() && it->second != 6) {
                isGBuffer = false;  // Not our 6-MRT format — skip replacement
            }
        }

        // Test shader mode (F7): replace all GBuffer PS with test/lighting shader
        if (s_testRedEnabled && isGBuffer && s_testRedPS) {
            State::GetSingleton().BindSharedData();
            for (auto* f : Feature::GetFeatureList()) {
                if (f->loaded && f->enabled) {
                    f->OnSetupGeometry(nullptr);
                }
            }
            Hooks::OriginalPSSetShader(context, s_testRedPS.Get(), ppCI, numCI);
            return;
        }

        // Technique-based replacement via ShaderCache
        // Use technique ID from BeginTechnique hook (Path A) or reverse map (Path B)
        if (isGBuffer) {
            auto& state = State::GetSingleton();
            uint32_t techID = state.currentTechniqueID;  // Set by BeginTechnique hook
            if (!techID) techID = GetPSTechID(ps);        // Fallback to reverse map

            // Diagnostic: log what we see when F6 is on
            static uint32_t s_techDiagCount = 0;
            if (ShaderCache::GetSingleton().techniqueReplacementEnabled && s_techDiagCount < 20) {
                ++s_techDiagCount;
                uint32_t techType = techID ? ((techID >> 8) & 0x1F) : 0xFF;
                spdlog::info("PSSetShader DIAG[{}]: isGBuffer={} techID={:#x} techType={} supported={}",
                    s_techDiagCount, isGBuffer, techID, techType,
                    techID ? ShaderCache::GetSingleton().IsSupportedTechnique(techID) : false);
            }

            if (techID) {
                auto* replacement = ShaderCache::GetSingleton().GetOrCompilePS(techID);
                if (replacement) {
                    state.BindSharedData();
                    for (auto* f : Feature::GetFeatureList()) {
                        if (f->loaded && f->enabled) {
                            f->OnSetupGeometry(nullptr);
                        }
                    }
                    Hooks::OriginalPSSetShader(context, replacement, ppCI, numCI);
                    return;
                }
            }
        }

        // Log stats periodically
        if (s_psCallCount == 3000 || s_psCallCount == 30000) {
            uint32_t mapped = 0;
            {
                std::lock_guard<std::mutex> lock(s_psTechIDMutex);
                mapped = static_cast<uint32_t>(s_psTechIDMap.size());
            }
            spdlog::info("=== PSSetShader report at {} calls ===", s_psCallCount);
            spdlog::info("  Reverse map: {} PS with technique IDs", mapped);
            {
                std::lock_guard<std::mutex> lock(s_psLayoutMutex);
                uint32_t l3 = 0, l4 = 0, fwd = 0, dep = 0, unk = 0;
                for (auto& [p, l] : s_psLayoutMap) {
                    switch (l) {
                        case PSLayout::Layout3_GBuffer: ++l3; break;
                        case PSLayout::Layout4_GBuffer: ++l4; break;
                        case PSLayout::Forward: ++fwd; break;
                        case PSLayout::DepthOnly: ++dep; break;
                        default: ++unk; break;
                    }
                }
                spdlog::info("  ISGN layouts: L3={} L4={} Fwd={} Depth={} Unk={}", l3, l4, fwd, dep, unk);
            }
        }

        Hooks::OriginalPSSetShader(context, ps, ppCI, numCI);
    }

    static LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto& menu = Menu::GetSingleton();
        if (menu.initialized && menu.isVisible) {
            menu.ProcessWndProc(hwnd, msg, wParam, lParam);

            ImGuiIO& io = ImGui::GetIO();
            if ((io.WantCaptureKeyboard && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) ||
                (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST))) {
                return 1;
            }
        }

        return CallWindowProcA(Hooks::OriginalWndProc, hwnd, msg, wParam, lParam);
    }

    static HRESULT __stdcall Hook_Present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        static bool menuInitialized = false;

        if (!menuInitialized) {
            DXGI_SWAP_CHAIN_DESC desc{};
            swapChain->GetDesc(&desc);
            Menu::GetSingleton().Initialize(desc.OutputWindow, Globals::GetDevice(), Globals::GetContext());

            Hooks::OriginalWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(desc.OutputWindow, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(Hook_WndProc)));
            if (Hooks::OriginalWndProc) {
                spdlog::info("  Subclassed WndProc for ImGui input");
            }

            menuInitialized = true;
        }

        // Rebuild scatter table reverse map periodically (PS pointer -> technique ID)
        static uint32_t s_reverseMapTimer = 0;
        ++s_reverseMapTimer;
        if (s_reverseMapTimer == 5 || (s_reverseMapTimer % 1000 == 0)) {
            RebuildReverseMap();
        }

        // Per-frame updates
        auto& state = State::GetSingleton();
        state.UpdatePerFrame();

        Feature::ResetAll();
        Feature::PrepassAll();

        if (GetAsyncKeyState(VK_F10) & 1) {
            Menu::GetSingleton().Toggle();
        }

        // F7: Toggle visual test (magenta shader replaces all BSLightingShader PS)
        if (GetAsyncKeyState(VK_F7) & 1) {
            if (!s_testRedPS) {
                CompileTestRedPS();
            }
            s_testRedEnabled = !s_testRedEnabled;
            spdlog::info("Visual test shader: {}", s_testRedEnabled ? "ENABLED (magenta)" : "DISABLED");
        }

        // F6: Toggle technique-based shader replacement
        if (GetAsyncKeyState(VK_F6) & 1) {
            auto& cache = ShaderCache::GetSingleton();
            cache.techniqueReplacementEnabled = !cache.techniqueReplacementEnabled;
            spdlog::info("Technique-based shader replacement: {}",
                         cache.techniqueReplacementEnabled ? "ENABLED" : "DISABLED");
        }

        // F8: Cycle MRT debug mode (0=off, 1=albedo, 2=normals, 3=material, 4=secNorm, 5=emissive, 6=motion)
        if (GetAsyncKeyState(VK_F8) & 1) {
            auto& s = State::GetSingleton();
            s.sharedData.DebugMRTMode = fmodf(s.sharedData.DebugMRTMode + 1.0f, 7.0f);
            int mode = (int)s.sharedData.DebugMRTMode;
            const char* names[] = {"OFF", "Albedo", "Normals", "Material", "SecNormal", "Emissive", "MotionVec"};
            spdlog::info("MRT Debug: {} ({})", names[mode], mode);
        }

        Menu::GetSingleton().Draw();

        return Hooks::OriginalPresent(swapChain, syncInterval, flags);
    }

    // ---- Helpers ----

    bool PatchVtableEntry(uintptr_t* vtable, size_t index, void* newFunc, void** originalOut)
    {
        *originalOut = reinterpret_cast<void*>(vtable[index]);

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            spdlog::error("VirtualProtect failed for vtable[{}] at {:X}", index,
                reinterpret_cast<uintptr_t>(&vtable[index]));
            return false;
        }

        vtable[index] = reinterpret_cast<uintptr_t>(newFunc);

        VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }

}  // anonymous namespace

namespace Hooks {

    void InstallShaderHooks()
    {
        spdlog::info("Hooks::InstallShaderHooks - installing Detour hooks...");

        auto base = REL::Module::get().base();
        spdlog::info("  Module base for hooks: {:#x}", base);
        if (!base) {
            spdlog::error("  Module base is null — cannot install hooks");
            return;
        }

        // CreatePixelShader hooked via InstallEarlyD3DHook() from F4SEPlugin_Load

        // --- VR Extended BSLightingShader::BeginTechnique at base+0x291DA20 (Detour, not vtable) ---
        // Must use Detour because the game devirtualizes this call (direct call, not vtable dispatch)
        OriginalLightingBeginTechnique = reinterpret_cast<LightingBeginTechnique_t>(base + 0x291DA20);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&OriginalLightingBeginTechnique), Hook_LightingBeginTechnique);
        LONG result = DetourTransactionCommit();

        if (result == NO_ERROR) {
            spdlog::info("  Hooked VR Extended BSLightingShader::BeginTechnique (Detour) at {:X}", base + 0x291DA20);
            // Verify Detour actually patched the prologue
            auto* patchedBytes = reinterpret_cast<uint8_t*>(base + 0x291DA20);
            spdlog::info("  Prologue bytes: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                         patchedBytes[0], patchedBytes[1], patchedBytes[2], patchedBytes[3],
                         patchedBytes[4], patchedBytes[5], patchedBytes[6], patchedBytes[7]);
            spdlog::info("  Expected: E9 xx xx xx xx (JMP rel32) if Detour applied");
            spdlog::info("  Original: 48 8B C4 48 83 EC 68 (MOV RAX,RSP; SUB RSP,0x68) if NOT patched");
        } else {
            spdlog::error("  Failed to hook BSLightingShader::BeginTechnique: error {}", result);
        }

    }

    void InstallRenderHooks()
    {
        spdlog::info("Hooks::InstallRenderHooks - installing vtable hooks...");

        auto base = REL::Module::get().base();

        // VR Extended BSLightingShader vtable at base+0x30BF3C8 (from Ghidra constructor)
        // NOTE: NOT 0x30bbdb8 which is the "Lighting" accumulation object
        auto vtable = reinterpret_cast<uintptr_t*>(base + 0x30BF3C8);
        spdlog::info("  VR Extended BSLightingShader vtable at {}", fmt::ptr(vtable));

        // BeginTechnique is hooked via Detour in InstallShaderHooks (devirtualized calls)

        // --- SetupGeometry at vtable[9] ---
        void* origGeom = nullptr;
        if (PatchVtableEntry(vtable, 9, reinterpret_cast<void*>(Hook_LightingSetupGeometry), &origGeom)) {
            OriginalLightingSetupGeometry = reinterpret_cast<SetupGeometry_t>(origGeom);
            spdlog::info("  Hooked VR Extended SetupGeometry (vtable[9]) - original {:X}",
                reinterpret_cast<uintptr_t>(origGeom));
        } else {
            spdlog::error("  Failed to hook BSLightingShader::SetupGeometry");
        }
    }

    void InstallD3DHooks()
    {
        spdlog::info("Hooks::InstallD3DHooks - installing D3D vtable hooks...");

        auto renderer = Globals::GetRenderer();
        if (!renderer) {
            spdlog::error("  Renderer singleton is null - skipping D3D hooks");
            return;
        }

        auto swapChainPtr = reinterpret_cast<IDXGISwapChain**>(renderer + 0x70);
        IDXGISwapChain* swapChain = *swapChainPtr;
        if (!swapChain) {
            spdlog::error("  IDXGISwapChain is null - skipping D3D hooks");
            return;
        }

        spdlog::info("  SwapChain at {:X}", reinterpret_cast<uintptr_t>(swapChain));

        auto swapChainVtable = *reinterpret_cast<uintptr_t**>(swapChain);
        if (!swapChainVtable) {
            spdlog::error("  SwapChain vtable is null - skipping D3D hooks");
            return;
        }

        void* origPresent = nullptr;
        if (PatchVtableEntry(swapChainVtable, 8, reinterpret_cast<void*>(Hook_Present), &origPresent)) {
            OriginalPresent = reinterpret_cast<Present_t>(origPresent);
            spdlog::info("  Hooked IDXGISwapChain::Present (vtable[8]) - original {:X}",
                reinterpret_cast<uintptr_t>(origPresent));
        } else {
            spdlog::error("  Failed to hook IDXGISwapChain::Present");
        }

        // --- ID3D11DeviceContext::PSSetShader at vtable[9] ---
        // Get the REAL immediate context from the device (not the game's cached pointer)
        auto* device = Globals::GetDevice();
        ID3D11DeviceContext* realContext = nullptr;
        if (device) {
            device->GetImmediateContext(&realContext);
        }
        auto* cachedContext = Globals::GetContext();

        spdlog::info("  Context comparison: cached={} vs device->GetImmediateContext={}",
                     fmt::ptr(cachedContext), fmt::ptr(realContext));

        // Hook the REAL context from GetImmediateContext
        ID3D11DeviceContext* contextToHook = realContext ? realContext : cachedContext;
        if (contextToHook) {
            auto contextVtable = *reinterpret_cast<uintptr_t**>(contextToHook);
            void* origPSSet = nullptr;
            if (PatchVtableEntry(contextVtable, 9, reinterpret_cast<void*>(Hook_PSSetShader), &origPSSet)) {
                OriginalPSSetShader = reinterpret_cast<PSSetShader_t>(origPSSet);
                spdlog::info("  Hooked ID3D11DeviceContext::PSSetShader (vtable[9]) on {} - original {:X}",
                    fmt::ptr(contextToHook), reinterpret_cast<uintptr_t>(origPSSet));
            } else {
                spdlog::error("  Failed to hook PSSetShader");
            }
        } else {
            spdlog::error("  No D3D context available — cannot hook PSSetShader");
        }

        // Release the reference from GetImmediateContext
        if (realContext) {
            realContext->Release();
        }

        // CreatePixelShader already hooked early in InstallShaderHooks (via temp device Detour)
    }

    // ---- D3D11CreateDevice Detour — hooks CreatePixelShader on the REAL game device ----
    using D3D11CreateDevice_t = HRESULT(WINAPI*)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*,
        UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    static D3D11CreateDevice_t s_originalD3D11CreateDevice = nullptr;

    static HRESULT WINAPI Hook_D3D11CreateDevice(
        IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
        UINT SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
        ID3D11DeviceContext** ppImmediateContext)
    {
        HRESULT hr = s_originalD3D11CreateDevice(pAdapter, DriverType, Software, Flags,
            pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

        if (SUCCEEDED(hr) && ppDevice && *ppDevice && !OriginalCreatePixelShader) {
            // Hook CreatePixelShader on the REAL game device
            auto deviceVtable = *reinterpret_cast<uintptr_t**>(*ppDevice);
            OriginalCreatePixelShader = reinterpret_cast<CreatePixelShader_t>(deviceVtable[15]);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(reinterpret_cast<PVOID*>(&OriginalCreatePixelShader), Hook_CreatePixelShader);
            LONG result = DetourTransactionCommit();

            if (result == NO_ERROR) {
                spdlog::info("CreatePixelShader hooked via D3D11CreateDevice intercept at {:X}",
                             deviceVtable[15]);
            } else {
                spdlog::error("Failed to Detour CreatePixelShader: error {}", result);
                OriginalCreatePixelShader = nullptr;
            }
        }
        return hr;
    }

    void InstallEarlyD3DHook()
    {
        // Force-load d3d11.dll if not already loaded
        HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
        if (!d3d11) {
            d3d11 = LoadLibraryA("d3d11.dll");
            spdlog::info("Force-loaded d3d11.dll: {}", d3d11 != nullptr);
        } else {
            spdlog::info("d3d11.dll already loaded");
        }

        if (!d3d11) {
            spdlog::error("Cannot load d3d11.dll — CreatePixelShader hook impossible");
            return;
        }

        // Detour D3D11CreateDevice — this fires when the game creates its device
        s_originalD3D11CreateDevice = reinterpret_cast<D3D11CreateDevice_t>(
            GetProcAddress(d3d11, "D3D11CreateDevice"));

        if (!s_originalD3D11CreateDevice) {
            spdlog::error("GetProcAddress(D3D11CreateDevice) failed");
            return;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&s_originalD3D11CreateDevice), Hook_D3D11CreateDevice);
        LONG result = DetourTransactionCommit();

        if (result == NO_ERROR) {
            spdlog::info("Hooked D3D11CreateDevice at {:X} — will intercept CreatePixelShader on device creation",
                         reinterpret_cast<uintptr_t>(s_originalD3D11CreateDevice));
        } else {
            spdlog::error("Failed to Detour D3D11CreateDevice: error {}", result);
        }
    }

    void MarkDeferredInitDone()
    {
        s_deferredD3DInitDone = true;
    }

    void InstallAll()
    {
        spdlog::info("Hooks::InstallAll - beginning hook installation...");

        InstallShaderHooks();
        InstallRenderHooks();
        InstallD3DHooks();

        spdlog::info("Hooks::InstallAll - hook installation complete");
    }

}  // namespace Hooks
