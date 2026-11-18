#pragma once

#include "Features/complex_materials/ComplexParallaxSettings.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace csah::linear_lighting
{
    // FO4VR lights in linear space natively. Each response is relative to the
    // game's own decode (sRGB texture views, pow 2.2 in its shaders and CPU
    // producers): 2.2 reproduces the game, lower brightens dark values,
    // higher darkens them.
    constexpr float kNativeLightingResponseGamma = 2.2f;

    struct Settings
    {
        bool enabled{ false };

        float lightGamma{ kNativeLightingResponseGamma };
        float colorGamma{ kNativeLightingResponseGamma };
        float emitColorGamma{ kNativeLightingResponseGamma };
        float glowmapGamma{ kNativeLightingResponseGamma };
        float ambientGamma{ kNativeLightingResponseGamma };
        float fogGamma{ kNativeLightingResponseGamma };
        float fogAlphaGamma{ kNativeLightingResponseGamma };
        float effectGamma{ kNativeLightingResponseGamma };
        float effectAlphaGamma{ kNativeLightingResponseGamma };
        float skyGamma{ kNativeLightingResponseGamma };
        float waterGamma{ kNativeLightingResponseGamma };
        float volumetricLightingGamma{ kNativeLightingResponseGamma };

        float vanillaDiffuseColorMultiplier{ 1.0f };
        float directionalLightMultiplier{ 1.0f };
        float pointLightMultiplier{ 1.0f };
        float ambientMultiplier{ 1.0f };
        float emitColorMultiplier{ 1.0f };
        float glowmapMultiplier{ 1.0f };

        float effectLightingMultiplier{ 1.0f };
        float membraneEffectMultiplier{ 1.0f };
        float otherEffectMultiplier{ 1.0f };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };

    struct alignas(16) FrameData
    {
        std::uint32_t enableLinearLighting{};
        std::uint32_t isDirectionalLightLinear{};
        float directionalLightRuntimeMultiplier{ 1.0f };
        float lightGamma{ kNativeLightingResponseGamma };
        float colorGamma{ kNativeLightingResponseGamma };
        float emitColorGamma{ kNativeLightingResponseGamma };
        float glowmapGamma{ kNativeLightingResponseGamma };
        float ambientGamma{ kNativeLightingResponseGamma };
        float fogGamma{ kNativeLightingResponseGamma };
        float fogAlphaGamma{ kNativeLightingResponseGamma };
        float effectGamma{ kNativeLightingResponseGamma };
        float effectAlphaGamma{ kNativeLightingResponseGamma };
        float skyGamma{ kNativeLightingResponseGamma };
        float waterGamma{ kNativeLightingResponseGamma };
        float volumetricLightingGamma{ kNativeLightingResponseGamma };
        float vanillaDiffuseColorMultiplier{ 1.0f };
        float directionalLightMultiplier{ 1.0f };
        float pointLightMultiplier{ 1.0f };
        float ambientMultiplier{ 1.0f };
        float emitColorMultiplier{ 1.0f };
        float glowmapMultiplier{ 1.0f };
        float effectLightingMultiplier{ 1.0f };
        float membraneEffectMultiplier{ 1.0f };
        // Fixed compatibility slots. The active FO4VR Effect domain has no
        // independently selectable Blood, Projected, or Deferred contract,
        // but packaged replacement bytecode relies on the established b5 ABI.
        float bloodEffectMultiplier{ 1.0f };
        float projectedEffectMultiplier{ 1.0f };
        float deferredEffectMultiplier{ 1.0f };
        float otherEffectMultiplier{ 1.0f };
        std::uint32_t lightProducerGammaBits{
            std::bit_cast<std::uint32_t>(2.2f) };
        complex_materials::FrameData complexParallax{};
    };

    struct alignas(16) GeometryData
    {
        float emissiveMultiplier{ 1.0f };
        float padding[3]{};
    };

    static_assert(sizeof(FrameData) == 144);
    static_assert(alignof(FrameData) == 16);
    static_assert(sizeof(GeometryData) == 16);
    static_assert(alignof(GeometryData) == 16);
    static_assert(offsetof(FrameData, enableLinearLighting) == 0);
    static_assert(offsetof(FrameData, directionalLightRuntimeMultiplier) == 8);
    static_assert(offsetof(FrameData, colorGamma) == 16);
    static_assert(offsetof(FrameData, vanillaDiffuseColorMultiplier) == 60);
    static_assert(offsetof(FrameData, otherEffectMultiplier) == 104);
    static_assert(offsetof(FrameData, lightProducerGammaBits) == 108);
    static_assert(offsetof(FrameData, complexParallax) == 112);

    [[nodiscard]] Settings sanitize(const Settings& settings) noexcept;

    [[nodiscard]] FrameData makeFrameData(
        const Settings& settings,
        bool runtimeEnabled,
        bool isDirectionalLightLinear,
        float directionalLightRuntimeMultiplier,
        float lightProducerGamma = 2.2f,
        const complex_materials::Settings& complexMaterialSettings = {}) noexcept;
}
