#pragma once

#include "Features/contact_shadows/ContactShadowSettings.h"
#include "render/GpuTimingProfiler.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace csah::contact_shadows
{
    struct RuntimeSnapshot
    {
        Settings settings{};
        bool gpuReady{};
        std::uint32_t matchingShaders{};
        std::uint32_t trackedShaders{};
        std::uint64_t replacementBinds{};
        std::uint64_t diagnosticBinds{};
        std::uint64_t maskDispatches{};
        std::uint64_t maskRebuilds{};
        std::uint64_t drawScopes{};
        std::uint64_t drawRestores{};
        std::uint64_t drawFallbacks{};
        std::uint64_t failures{};
    };

    struct ShaderBinding
    {
        ID3D11PixelShader* original{};
        ID3D11PixelShader* replacement{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return original && replacement;
        }
    };

    struct PixelShaderSelection
    {
        ID3D11PixelShader* shader{};
        ShaderBinding binding{};
    };

    class ScopedDrawBindings final
    {
    public:
        ScopedDrawBindings() noexcept = default;
        ~ScopedDrawBindings() noexcept;
        ScopedDrawBindings(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings(ScopedDrawBindings&&) = delete;
        ScopedDrawBindings& operator=(const ScopedDrawBindings&) = delete;
        ScopedDrawBindings& operator=(ScopedDrawBindings&&) = delete;

        [[nodiscard]] bool active() const noexcept { return context_ != nullptr; }

    private:
        friend class Runtime;
        ScopedDrawBindings(
            ID3D11DeviceContext* context,
            ID3D11Buffer* constants,
            ID3D11ShaderResourceView* mask,
            render::GpuTimingProfiler* drawTiming,
            std::atomic_uint64_t* restoreCounter) noexcept;

        ID3D11DeviceContext* context_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousMask_;
        render::GpuTimingProfiler::Scope drawTiming_;
        std::atomic_uint64_t* restoreCounter_{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            HRESULT(STDMETHODCALLTYPE* createPixelShader)(
                ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                ID3D11PixelShader**)) noexcept;
        void onPixelShaderCreated(
            const void* bytecode,
            SIZE_T bytecodeLength,
            ID3D11PixelShader* shader) noexcept;
        [[nodiscard]] PixelShaderSelection selectPixelShader(
            ID3D11PixelShader* requested,
            bool compositorFeatureActive) noexcept;
        [[nodiscard]] PixelShaderSelection
            selectDirectionalDiagnosticPixelShader(
                ID3D11PixelShader* requested,
                std::uint8_t diagnosticMode) noexcept;
        [[nodiscard]] std::uint32_t sunShadowLayout(ID3D11PixelShader* shader) const noexcept;
        [[nodiscard]] bool tracksOriginal(
            ID3D11PixelShader* shader) const noexcept;
        [[nodiscard]] bool isDirectionalDiagnosticPixelShader(
            ID3D11PixelShader* shader) const noexcept;
        [[nodiscard]] ID3D11PixelShader*
            retainedOriginalDirectionalDiagnosticPixelShader(
                ID3D11PixelShader* diagnosticShader) const noexcept;
        [[nodiscard]] ScopedDrawBindings scopeDraw(
            ID3D11DeviceContext* context,
            ShaderBinding binding,
            bool contactShadowsActive,
            bool cloudShadowsActive) noexcept;
        void recordDrawFallback() noexcept;
        [[nodiscard]] bool featureEnabled() const noexcept;
        [[nodiscard]] bool compositorReady(
            bool materialEffectsActive) const noexcept;
        void applySettings(const Settings& settings) noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;
        void uploadSettings(
            ID3D11DeviceContext* context,
            bool contactShadowsActive,
            bool maskActive,
            bool cloudShadowsActive,
            float cloudOpacity) noexcept;
        [[nodiscard]] bool ensureMaskResources(
            ID3D11ShaderResourceView* depth) noexcept;
        [[nodiscard]] bool dispatchMask(
            ID3D11DeviceContext* context,
            bool contactShadowsActive,
            bool cloudShadowsActive,
            bool& maskActive) noexcept;

        struct TrackedShader
        {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
            std::uint16_t contractIndex{};
        };

        static constexpr std::size_t kMaximumShaderContracts = 32;
        static constexpr std::size_t kMaximumTrackedShaders = 128;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            kMaximumShaderContracts> replacements_{};
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, 6>,
            kMaximumShaderContracts> directionalDiagnostics_{};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> dispatchCompute_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> maskCompute_;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> resolveCompute_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> dispatchRecords_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            dispatchRecordsView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            dispatchRecordsOutput_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> dispatchArguments_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
            dispatchArgumentsOutput_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> rawMaskTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rawMaskView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> rawMaskOutput_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> maskTexture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> maskOutput_;
        render::GpuTimingProfiler gpuTiming_;
        render::GpuTimingProfiler drawGpuTiming_;
        UINT maskWidth_{};
        UINT maskHeight_{};
        std::array<TrackedShader, kMaximumTrackedShaders> originals_{};
        std::atomic_bool enabled_{ true };
        std::atomic_bool foveated_{ true };
        std::atomic<float> strength_{ 0.85f };
        std::atomic<float> maxDistance_{ 96.0f };
        std::atomic<float> fadeDistance_{ 2048.0f };
        std::atomic<float> thickness_{ 0.012f };
        std::atomic_uint32_t sampleCount_{ 8 };
        std::atomic_uint64_t settingsRevision_{ 1 };
        std::atomic_bool resourcesReady_{};
        std::uint64_t uploadedRevision_{};
        bool uploadedContactActive_{};
        bool uploadedMaskActive_{};
        bool uploadedCloudActive_{};
        float uploadedCloudOpacity_{};
        std::array<float, 20> uploadedGpuSettings_{};
        std::atomic_uint32_t matchingShaders_{};
        std::atomic_uint32_t trackedShaders_{};
        std::atomic_uint64_t replacementBinds_{};
        std::atomic_uint64_t diagnosticBinds_{};
        std::atomic_uint64_t maskDispatches_{};
        std::atomic_uint64_t maskRebuilds_{};
        std::atomic_uint64_t drawScopes_{};
        std::atomic_uint64_t drawRestores_{};
        std::atomic_uint64_t drawFallbacks_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstMatchLogged_{};
        std::atomic_bool firstReplacementBindLogged_{};
        std::atomic_bool firstDiagnosticBindLogged_{};
        std::atomic_bool firstDispatchLogged_{};
        std::atomic_bool firstDispatchFailureLogged_{};
    };
}
