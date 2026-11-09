#include "Features/dlaa/DlaaSettingsStore.h"
#include "SettingsFixture.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    bool expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << message << '\n';
        }
        return condition;
    }

}

int main()
{
    using namespace csah::dlaa;
    bool valid = true;

    Settings invalid{};
    invalid.mode = static_cast<Mode>(99);
    invalid.modelPreset = static_cast<ModelPreset>(99);
    invalid.sharpness = std::numeric_limits<float>::quiet_NaN();
    invalid.centerWidth = 0.01f;
    invalid.centerHeight = 4.0f;
    invalid.centerFeatherPixels = 1000.0f;
    const auto safe = sanitize(invalid);
    valid &= expect(safe.mode == Mode::dlaa, "invalid mode did not fail to DLAA");
    valid &= expect(
        safe.modelPreset == ModelPreset::qualityK,
        "invalid model did not fail to preset K");
    valid &= expect(
        std::abs(safe.sharpness - 0.25f) < 0.0001f,
        "non-finite sharpness fallback changed");
    valid &= expect(
        std::abs(safe.centerWidth - 0.25f) < 0.0001f &&
            std::abs(safe.centerHeight - 1.0f) < 0.0001f &&
            std::abs(safe.centerFeatherPixels - 128.0f) < 0.0001f,
        "center settings were not bounded");

    const auto center = computeCenterRegion(2688, 2880, 0.55f, 0.55f);
    valid &= expect(
        center.width != 0 && center.height != 0 &&
            center.width % 8 == 0 && center.height % 8 == 0,
        "center region is not nonzero and eight-pixel aligned");
    valid &= expect(
        center.left * 2 + center.width <= 2688 &&
            center.top * 2 + center.height <= 2880,
        "center region escaped its per-eye bounds");

    const auto full = computeCenterRegion(2688, 2880, 1.0f, 1.0f);
    valid &= expect(
        full.left == 0 && full.top == 0 && full.width == 2688 &&
            full.height == 2880,
        "full center coverage did not reproduce the complete eye");

    valid &= expect(isDlssMode(Mode::dlssQuality), "Quality is not DLSS");
    valid &= expect(
        !isDlssMode(Mode::dlaa) && !isDlssMode(Mode::centerDlaa),
        "native AA modes were classified as DLSS upscaling");
    valid &= expect(
        computeJitterPhaseCount(2688, 2688) == 8,
        "native jitter phase count changed");
    valid &= expect(
        computeJitterPhaseCount(1792, 2688) == 18,
        "quality DLSS jitter phase count changed");
    valid &= expect(
        computeJitterPhaseCount(0, 2688) == 1,
        "zero render width did not fail closed");
    valid &= expect(
        isEvaluationViewportValid(
            Mode::dlssQuality,
            1,
            0.0f,
            0.0f,
            5376.0f,
            2880.0f,
            5376,
            2880,
            2.0f / 3.0f,
            2.0f / 3.0f,
            true,
            true),
        "DLSS rejected FO4VR's verified restored post viewport");
    valid &= expect(
        !isEvaluationViewportValid(
            Mode::dlssQuality,
            1,
            0.0f,
            0.0f,
            5376.0f,
            2880.0f,
            5376,
            2880,
            2.0f / 3.0f,
            2.0f / 3.0f,
            false,
            true),
        "DLSS accepted a restored viewport without scale ownership");
    valid &= expect(
        !isEvaluationViewportValid(
            Mode::dlssQuality,
            1,
            0.0f,
            0.0f,
            5376.0f,
            2880.0f,
            5376,
            2880,
            2.0f / 3.0f,
            2.0f / 3.0f,
            true,
            false),
        "DLSS accepted a restored viewport before FO4VR closed DRS");
    valid &= expect(
        !isEvaluationViewportValid(
            Mode::dlaa,
            1,
            0.0f,
            0.0f,
            5376.0f,
            2880.0f,
            5376,
            2880,
            2.0f / 3.0f,
            2.0f / 3.0f,
            true,
            true),
        "native DLAA accepted a DLSS-only restored viewport contract");
    valid &= expect(
        isEvaluationViewportValid(
            Mode::dlssQuality,
            1,
            0.0f,
            0.0f,
            3584.0f,
            1920.0f,
            5376,
            2880,
            2.0f / 3.0f,
            2.0f / 3.0f,
            false,
            false),
        "DLSS rejected an actively scaled render viewport");

    SettingsFixture ini("UpscalingSettingsTests");
    ini.write(
        "[CommunityShaders]\n"
        "Enabled = false\n"
        "[DLAA]\n"
        "Enabled = true\n"
        "Mode = 2\n");
    const auto independent = loadSettings(ini.values());
    valid &= expect(
        independent.enabled && independent.mode == Mode::dlssBalanced,
        "Community Shaders visual gate disabled independent DLAA/DLSS");

    ini.write(
        "[CommunityShaders]\n"
        "Enabled = true\n"
        "[DLAA]\n"
        "Enabled = false\n");
    valid &= expect(
        !loadSettings(ini.values()).enabled,
        "DLAA/DLSS ignored its independent master gate");
    return valid ? 0 : 1;
}
