#include "Features/linear_lighting/LinearLightingSettings.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    template <class T>
    concept HasBloodEffectMultiplier = requires(T value) {
        value.bloodEffectMultiplier;
    };

    template <class T>
    concept HasProjectedEffectMultiplier = requires(T value) {
        value.projectedEffectMultiplier;
    };

    template <class T>
    concept HasDeferredEffectMultiplier = requires(T value) {
        value.deferredEffectMultiplier;
    };

    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Linear Lighting settings test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool near(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) <= 0.00001f;
    }
}

int main()
{
    using namespace csah::linear_lighting;

    static_assert(!HasBloodEffectMultiplier<Settings>);
    static_assert(!HasProjectedEffectMultiplier<Settings>);
    static_assert(!HasDeferredEffectMultiplier<Settings>);

    const Settings defaults{};
    require(!defaults.enabled, "feature must default disabled");

    Settings invalid = defaults;
    invalid.enabled = true;
    invalid.lightGamma = std::numeric_limits<float>::quiet_NaN();
    invalid.colorGamma = -100.0f;
    invalid.fogGamma = 100.0f;
    invalid.emitColorMultiplier = -1.0f;
    invalid.glowmapMultiplier = std::numeric_limits<float>::infinity();
    const auto safe = sanitize(invalid);
    require(near(safe.lightGamma, defaults.lightGamma), "non-finite response uses default");
    require(near(safe.colorGamma, 0.1f), "response lower bound");
    require(near(safe.fogGamma, 4.4f), "response upper bound");
    require(near(safe.emitColorMultiplier, 0.0f), "multiplier lower bound");
    require(near(safe.glowmapMultiplier, defaults.glowmapMultiplier), "non-finite multiplier uses default");

    const auto disabled = makeFrameData(invalid, false, false, 2.0f);
    require(disabled.enableLinearLighting == 0, "runtime gate disables GPU feature");
    const auto enabled = makeFrameData(invalid, true, true, 2.0f);
    require(enabled.enableLinearLighting == 1, "settings and runtime gate enable GPU feature");
    require(enabled.isDirectionalLightLinear == 1, "directional light space flag");
    require(near(enabled.directionalLightRuntimeMultiplier, 2.0f), "runtime directional multiplier");
    require(near(enabled.lightGamma, safe.lightGamma), "light response is published unchanged");
    require(near(enabled.fogGamma, safe.fogGamma), "fog response is published unchanged");
    require(
        near(std::bit_cast<float>(enabled.lightProducerGammaBits), 2.2f),
        "native producer gamma defaults to 2.2");
    const auto customProducer =
        makeFrameData(invalid, true, true, 2.0f, 1.7f);
    require(
        near(
            std::bit_cast<float>(customProducer.lightProducerGammaBits),
            1.7f),
        "verified producer gamma is published in the ABI slot");
    require(near(enabled.bloodEffectMultiplier, 1.0f),
        "unsupported Blood ABI slot must remain neutral");
    require(near(enabled.projectedEffectMultiplier, 1.0f),
        "unsupported Projected ABI slot must remain neutral");
    require(near(enabled.deferredEffectMultiplier, 1.0f),
        "unsupported Deferred ABI slot must remain neutral");

    std::cout << "Linear Lighting settings tests passed.\n";
    return EXIT_SUCCESS;
}
