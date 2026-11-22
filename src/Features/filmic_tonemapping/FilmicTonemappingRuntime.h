#pragma once

#include "Features/color_grading/ColorGradingTable.h"
#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace csah::filmic_tonemapping
{
    using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
        ID3D11Device*,
        const void*,
        SIZE_T,
        ID3D11ClassLinkage*,
        ID3D11PixelShader**);

    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool gpuReady{};
        std::uint32_t matchingShaders{};
        std::uint32_t trackedShaders{};
        std::uint64_t replacementBinds{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t failures{};
    };

    struct ShaderBinding final
    {
        ID3D11PixelShader* original{};
        ID3D11PixelShader* replacement{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return original && replacement;
        }
    };

    struct PixelShaderSelection final
    {
        ID3D11PixelShader* shader{};
        ShaderBinding binding{};
    };

    class Runtime;

    // The colour tables and sampler the output shader reads while it grades.
    struct GradeBindings final
    {
        std::array<ID3D11ShaderResourceView*, 2> tables{};
        ID3D11SamplerState* sampler{};
    };

    // Binds the output shader's private constants, colour tables and sampler
    // for one draw and puts the previous bindings back afterwards.
    class ScopedConstants final
    {
    public:
        ScopedConstants() noexcept = default;
        ~ScopedConstants() noexcept;
        ScopedConstants(const ScopedConstants&) = delete;
        ScopedConstants(ScopedConstants&&) = delete;
        ScopedConstants& operator=(const ScopedConstants&) = delete;
        ScopedConstants& operator=(ScopedConstants&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedConstants(
            Runtime* owner,
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            const GradeBindings& grade) noexcept;

        Runtime* owner_{};
        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previous_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>
            previousTables_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> previousSampler_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            CreatePixelShaderFunction createPixelShader) noexcept;
        void onPixelShaderCreated(
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11PixelShader* shader) noexcept;
        [[nodiscard]] PixelShaderSelection selectPixelShader(
            ID3D11DeviceContext* context,
            ID3D11PixelShader* requested) noexcept;
        [[nodiscard]] ScopedConstants scopeDraw(
            ID3D11DeviceContext* context,
            ShaderBinding binding) noexcept;
        [[nodiscard]] bool tracksOriginal(
            ID3D11PixelShader* shader) const noexcept;
        [[nodiscard]] bool featureEnabled() const noexcept;
        void setOutputFeatureRequested(bool requested) noexcept;
        [[nodiscard]] bool bindingActive(ShaderBinding binding) const noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        friend class ScopedConstants;
        Runtime() = default;

        // Mirrors cbuffer FilmicSettings of FilmicTonemappingPS.hlsl.
        struct alignas(16) GpuConstants final
        {
            float exposureMultiplier{ 1.0f };
            float nativeAdaptationWeight{ 1.0f };
            float filmicStrength{ 1.0f };
            float whitePointScale{ 1.0f };
            std::array<float, 4> gradeControl{ 0.0f, 0.0f, 0.0f, 1.0f };
            std::array<float, 4> gradeTables{};
            std::array<float, 4> gradeLevels{ 0.0f, 1.0f, 1.0f, 1.0f };
            std::array<float, 4> gradeColor{ 1.0f, 0.0f, 0.0f, 0.0f };
            std::array<float, 4> gradeShadowTint{};
            std::array<float, 4> gradeHighlightTint{};
        };

        enum GradeTable : std::size_t
        {
            kDayTable,
            kNightTable,
            kInteriorTable,
            kGradeTableCount,
        };

        // Which look and which two of its tables this frame blends, and how
        // far toward the second.
        struct GradeSelection final
        {
            std::size_t look{};
            std::size_t first{ kNightTable };
            std::size_t second{ kDayTable };
            float secondWeight{};
            bool tablesUsable{};

            [[nodiscard]] bool operator==(const GradeSelection&) const noexcept = default;
        };

        static constexpr std::size_t kMaximumTrackedShaders = 4;
        [[nodiscard]] GpuConstants gpuConstants(
            const color_grading::Settings& grading,
            const GradeSelection& selection) const noexcept;
        [[nodiscard]] bool gradingActive() const noexcept;
        [[nodiscard]] GradeSelection selectGradeTables() const noexcept;
        void uploadConstants(ID3D11DeviceContext* context) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_{};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> baseReplacement_{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> fadeReplacement_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kMaximumTrackedShaders> originals_{};
        std::array<bool, kMaximumTrackedShaders> originalFade_{};
        std::atomic_uint32_t trackedShaders_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool outputFeatureRequested_{};
        std::atomic_bool nativeAutoExposure_{ true };
        std::atomic<float> exposureCompensationEV_{};
        std::atomic<float> filmicStrength_{ 1.0f };
        std::atomic<float> whitePointScale_{ 1.0f };
        // Colour grading. Settings arrive on the settings thread: grading_ is
        // written under gradingMutex_ and the two flags are published for the
        // lock-free shader selection. The render thread copies grading_ under
        // the mutex only when settingsRevision_ changed. The looks, sampler
        // and uploaded selection belong to the render thread; every look is
        // loaded at device creation, so switching looks reads no file.
        mutable std::mutex gradingMutex_{};
        color_grading::Settings grading_{};
        std::atomic_bool gradingEnabled_{};
        std::atomic_bool gradingControlsNeutral_{ true };
        std::atomic_bool gradingTablesLoaded_{};
        std::atomic_uint32_t gradingLook_{ 1 };
        color_grading::LookLibrary gradeLooks_{};
        Microsoft::WRL::ComPtr<ID3D11SamplerState> gradeSampler_{};
        GradeSelection uploadedSelection_{};
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::uint64_t uploadedRevision_{};
        std::atomic_bool resourcesReady_{};
        std::atomic_uint32_t matchingShaders_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool baseMatchLogged_{};
        std::atomic_bool fadeMatchLogged_{};
        std::atomic_bool firstBindLogged_{};
    };
}
