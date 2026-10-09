#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace csah::ibl
{
    enum class MaterialBindingRejection : std::uint8_t
    {
        none,
        nullContext,
        missingConstants,
        deviceMismatch,
        applyFailed,
    };

    // Render-thread-only transaction for the bindings added to exact
    // DFComposite replacement shaders. Construction captures and validates
    // the complete t29..t37/b5 state; destruction restores that state exactly.
    // Null views are intentional for the reflection-free duplicate, whose
    // explicit zero-weight b5 keeps both material paths disabled.
    class ScopedMaterialBindings final
    {
    public:
        static constexpr UINT kAlbedoSlot = 29;
        static constexpr UINT kRadianceSlot = 30;
        static constexpr UINT kValiditySlot = 31;
        static constexpr UINT kPreviousRadianceSlot = 32;
        static constexpr UINT kPreviousValiditySlot = 33;
        static constexpr UINT kPositionSlot = 34;
        static constexpr UINT kPreviousPositionSlot = 35;
        static constexpr UINT kMaterialPropertiesSlot = 36;
        static constexpr UINT kStyleSlot = 37;
        static constexpr UINT kPbrMaterialSlot = 45;
        static constexpr UINT kSurfaceClassSlot = 47;
        static constexpr UINT kConstantSlot = 5;

        ScopedMaterialBindings() noexcept = default;
        ScopedMaterialBindings(
            ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* albedo,
            ID3D11ShaderResourceView* radiance,
            ID3D11ShaderResourceView* validity,
            ID3D11ShaderResourceView* previousRadiance,
            ID3D11ShaderResourceView* previousValidity,
            ID3D11ShaderResourceView* position,
            ID3D11ShaderResourceView* previousPosition,
            ID3D11ShaderResourceView* materialProperties,
            ID3D11ShaderResourceView* pbrMaterial,
            ID3D11ShaderResourceView* surfaceClass,
            ID3D11Buffer* constants,
            ID3D11ShaderResourceView* style = nullptr) noexcept;
        ~ScopedMaterialBindings();

        ScopedMaterialBindings(const ScopedMaterialBindings&) = delete;
        ScopedMaterialBindings& operator=(
            const ScopedMaterialBindings&) = delete;
        ScopedMaterialBindings(ScopedMaterialBindings&& other) noexcept;
        ScopedMaterialBindings& operator=(
            ScopedMaterialBindings&& other) noexcept;

        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool restore() noexcept;
        [[nodiscard]] MaterialBindingRejection rejection() const noexcept;

    private:
        [[nodiscard]] bool restoreCaptured() noexcept;
        void moveFrom(ScopedMaterialBindings&& other) noexcept;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,
            9>
            previousResources_{};
        Microsoft::WRL::ComPtr<ID3D11Buffer> previousConstants_;
        std::array<
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,
            2>
            previousPbrResources_{};
        MaterialBindingRejection rejection_{
            MaterialBindingRejection::nullContext
        };
        bool captured_{};
        bool pbrResourcesCaptured_{};
        bool active_{};
        bool restored_{};
    };
}
