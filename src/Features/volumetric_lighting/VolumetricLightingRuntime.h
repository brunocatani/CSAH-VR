#pragma once

#include "Features/volumetric_lighting/VolumetricLightingSettings.h"

#include <d3d11.h>

#include <cstddef>
#include <cstdint>

namespace csah::volumetric_lighting
{
    struct HostShaderSelection final
    {
        const void* bytecode{};
        std::size_t bytecodeLength{};
        bool matched{};

        [[nodiscard]] bool replaced() const noexcept
        {
            return matched && bytecode && bytecodeLength != 0;
        }
    };

    struct RuntimeSnapshot final
    {
        Settings settings{};
        bool nativeContractValid{};
        bool engineDataValid{};
        bool gpuReady{};
        bool diagnosticSuppressed{};
        bool hostShaderObserved{};
        bool temporalHistoryValid{};
        bool outputQualified{};
        std::uint64_t directionalCaptures{};
        std::uint64_t renderedFrames{};
        std::uint64_t rejectedFrames{};
        std::uint64_t qualificationPasses{};
        std::uint64_t qualificationFailures{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        [[nodiscard]] bool start(
            const Settings& settings,
            bool diagnosticSuppressed) noexcept;
        void onGameDataReady() noexcept;
        void applySettings(const Settings& settings) noexcept;
        void setDiagnosticSuppressed(bool suppressed) noexcept;

        [[nodiscard]] HostShaderSelection selectHostPixelShader(
            const void* bytecode,
            std::size_t bytecodeLength) noexcept;
        void recordHostPixelShaderCreation(
            const HostShaderSelection& selection,
            HRESULT result,
            ID3D11PixelShader* shader) noexcept;
        [[nodiscard]] bool isHostPixelShader(
            ID3D11PixelShader* shader) const noexcept;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept;
        void beginFrame() noexcept;
        void captureDirectionalFrame(
            ID3D11DeviceContext* context,
            bool directionalShaderActive, bool ambientShaderActive = false) noexcept;
        void renderAfterHostDraw(
            ID3D11DeviceContext* context,
            bool hostShaderActive) noexcept;

        [[nodiscard]] bool requested() const noexcept;
        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;
        [[nodiscard]] static bool internalRenderActive() noexcept;

    private:
        Runtime() = default;
        [[nodiscard]] bool initializeNativeGate() noexcept;
        void applyNativeGate() noexcept;
    };
}
