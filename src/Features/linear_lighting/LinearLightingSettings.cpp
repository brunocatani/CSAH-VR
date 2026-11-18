#include "Features/linear_lighting/LinearLightingSettings.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace csah::linear_lighting
{
    namespace
    {
        [[nodiscard]] float sanitizeValue(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept
        {
            if (!std::isfinite(value)) {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        [[nodiscard]] float sanitizeGamma(float value, float fallback) noexcept
        {
            return sanitizeValue(value, fallback, 0.1f, 4.4f);
        }

        [[nodiscard]] float sanitizeMultiplier(
            float value,
            float fallback) noexcept
        {
            return sanitizeValue(value, fallback, 0.0f, 10.0f);
        }
    }

    Settings sanitize(const Settings& settings) noexcept
    {
        const Settings defaults{};
        Settings result = settings;

        result.lightGamma = sanitizeGamma(result.lightGamma, defaults.lightGamma);
        result.colorGamma = sanitizeGamma(result.colorGamma, defaults.colorGamma);
        result.emitColorGamma = sanitizeGamma(result.emitColorGamma, defaults.emitColorGamma);
        result.glowmapGamma = sanitizeGamma(result.glowmapGamma, defaults.glowmapGamma);
        result.ambientGamma = sanitizeGamma(result.ambientGamma, defaults.ambientGamma);
        result.fogGamma = sanitizeGamma(result.fogGamma, defaults.fogGamma);
        result.fogAlphaGamma = sanitizeGamma(result.fogAlphaGamma, defaults.fogAlphaGamma);
        result.effectGamma = sanitizeGamma(result.effectGamma, defaults.effectGamma);
        result.effectAlphaGamma = sanitizeGamma(result.effectAlphaGamma, defaults.effectAlphaGamma);
        result.skyGamma = sanitizeGamma(result.skyGamma, defaults.skyGamma);
        result.waterGamma = sanitizeGamma(result.waterGamma, defaults.waterGamma);
        result.volumetricLightingGamma = sanitizeGamma(
            result.volumetricLightingGamma,
            defaults.volumetricLightingGamma);

        result.vanillaDiffuseColorMultiplier = sanitizeMultiplier(
            result.vanillaDiffuseColorMultiplier,
            defaults.vanillaDiffuseColorMultiplier);
        result.directionalLightMultiplier = sanitizeMultiplier(
            result.directionalLightMultiplier,
            defaults.directionalLightMultiplier);
        result.pointLightMultiplier = sanitizeMultiplier(
            result.pointLightMultiplier,
            defaults.pointLightMultiplier);
        result.ambientMultiplier = sanitizeMultiplier(
            result.ambientMultiplier,
            defaults.ambientMultiplier);
        result.emitColorMultiplier = sanitizeMultiplier(
            result.emitColorMultiplier,
            defaults.emitColorMultiplier);
        result.glowmapMultiplier = sanitizeMultiplier(
            result.glowmapMultiplier,
            defaults.glowmapMultiplier);
        result.effectLightingMultiplier = sanitizeMultiplier(
            result.effectLightingMultiplier,
            defaults.effectLightingMultiplier);
        result.membraneEffectMultiplier = sanitizeMultiplier(
            result.membraneEffectMultiplier,
            defaults.membraneEffectMultiplier);
        result.otherEffectMultiplier = sanitizeMultiplier(
            result.otherEffectMultiplier,
            defaults.otherEffectMultiplier);
        return result;
    }

    FrameData makeFrameData(
        const Settings& settings,
        bool runtimeEnabled,
        bool isDirectionalLightLinear,
        float directionalLightRuntimeMultiplier,
        float lightProducerGamma,
        const complex_materials::Settings& complexMaterialSettings) noexcept
    {
        const auto safe = sanitize(settings);
        FrameData data{};
        data.enableLinearLighting = safe.enabled && runtimeEnabled ? 1u : 0u;
        data.isDirectionalLightLinear = isDirectionalLightLinear ? 1u : 0u;
        data.directionalLightRuntimeMultiplier = sanitizeValue(
            directionalLightRuntimeMultiplier,
            1.0f,
            0.0f,
            100.0f);
        data.lightGamma = safe.lightGamma;
        data.colorGamma = safe.colorGamma;
        data.emitColorGamma = safe.emitColorGamma;
        data.glowmapGamma = safe.glowmapGamma;
        data.ambientGamma = safe.ambientGamma;
        data.fogGamma = safe.fogGamma;
        data.fogAlphaGamma = safe.fogAlphaGamma;
        data.effectGamma = safe.effectGamma;
        data.effectAlphaGamma = safe.effectAlphaGamma;
        data.skyGamma = safe.skyGamma;
        data.waterGamma = safe.waterGamma;
        data.volumetricLightingGamma = safe.volumetricLightingGamma;
        data.vanillaDiffuseColorMultiplier = safe.vanillaDiffuseColorMultiplier;
        data.directionalLightMultiplier = safe.directionalLightMultiplier;
        data.pointLightMultiplier = safe.pointLightMultiplier;
        data.ambientMultiplier = safe.ambientMultiplier;
        data.emitColorMultiplier = safe.emitColorMultiplier;
        data.glowmapMultiplier = safe.glowmapMultiplier;
        data.effectLightingMultiplier = safe.effectLightingMultiplier;
        data.membraneEffectMultiplier = safe.membraneEffectMultiplier;
        data.bloodEffectMultiplier = 1.0f;
        data.projectedEffectMultiplier = 1.0f;
        data.deferredEffectMultiplier = 1.0f;
        data.otherEffectMultiplier = safe.otherEffectMultiplier;
        data.lightProducerGammaBits = std::bit_cast<std::uint32_t>(
            sanitizeGamma(lightProducerGamma, 2.2f));
        data.complexParallax =
            complex_materials::makeFrameData(complexMaterialSettings);
        return data;
    }
}
