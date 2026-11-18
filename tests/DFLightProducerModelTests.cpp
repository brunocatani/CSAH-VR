#include "Features/linear_lighting/DFLightProducerModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    using namespace csah::linear_lighting;

    [[nodiscard]] bool expect(bool condition, const char* message) noexcept
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }

    [[nodiscard]] bool nearlyEqual(float left, float right) noexcept
    {
        return std::abs(left - right) <= 1.0e-6f;
    }
}

int main()
{
    using namespace csah::linear_lighting;
    bool passed = true;

    passed &= expect(
        classifyDFLightProducer(kDFLightAmbientDescriptorMask) ==
            DFLightProducerKind::ambient,
        "ambient descriptor was not classified");
    passed &= expect(
        classifyDFLightProducer(0x1u) ==
            DFLightProducerKind::directional,
        "directional descriptor bit 0 was not classified");
    passed &= expect(
        classifyDFLightProducer(0x2u) ==
            DFLightProducerKind::directional,
        "directional descriptor bit 1 was not classified");
    passed &= expect(
        classifyDFLightProducer(kDFLightCharacterDescriptorMask |
            kDFLightAmbientDescriptorMask) == DFLightProducerKind::other,
        "character lighting did not retain vanilla producer ownership");
    passed &= expect(
        classifyDFLightProducer(0x20u) == DFLightProducerKind::other,
        "spot lighting was incorrectly classified as directional");

    Settings disabled{};
    disabled.enabled = false;
    disabled.lightGamma = 1.4f;
    disabled.ambientGamma = 1.6f;
    disabled.directionalLightMultiplier = 2.0f;
    disabled.ambientMultiplier = 3.0f;
    const auto disabledState = makeDFLightProducerState(disabled);
    passed &= expect(!disabledState.enabled,
        "disabled settings activated DFLight producers");
    passed &= expect(
        nearlyEqual(
            disabledState.directionalGamma,
            kVanillaDFLightGamma) &&
            nearlyEqual(disabledState.ambientGamma, kVanillaDFLightGamma) &&
            nearlyEqual(disabledState.directionalMultiplier, 1.0f) &&
            nearlyEqual(disabledState.ambientMultiplier, 1.0f),
        "disabled settings did not preserve vanilla producer values");

    Settings enabled{};
    enabled.enabled = true;
    enabled.lightGamma = 1.8f;
    enabled.ambientGamma = 1.7f;
    enabled.directionalLightMultiplier = 0.5f;
    enabled.ambientMultiplier = 0.75f;
    const auto enabledState = makeDFLightProducerState(enabled);
    passed &= expect(enabledState.enabled,
        "enabled settings did not activate DFLight producers");
    passed &= expect(
        nearlyEqual(enabledState.directionalGamma, 1.8f) &&
            nearlyEqual(enabledState.ambientGamma, 1.7f),
        "DFLight producers did not publish the configured responses");
    passed &= expect(
        nearlyEqual(
            enabledState.directionalMultiplier,
            0.5f) &&
            nearlyEqual(enabledState.ambientMultiplier, 0.75f),
        "FO4VR DFLight producer multipliers were not retained at engine scale");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
