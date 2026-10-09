#include "Features/ibl/IblSceneRadianceProbeModel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "IBL scene-radiance probe model test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool near(float left, float right) noexcept
    {
        return std::abs(left - right) <= 0.00001f;
    }
}

int main()
{
    using namespace csah::ibl;
    const auto signedHalf = decodeR16G16B16A16Float({0xBC00, 0x0000, 0x3C00, 0});
    require(near(signedHalf.red, -1.0F) && near(signedHalf.green, 0.0F) && near(signedHalf.blue, 1.0F),
        "signed HDR probe decoding must distinguish invalid markers from black");

    constexpr auto coordinates = sceneProbeCoordinates(5376, 2880);
    static_assert(kSceneProbeSampleCount == 64);
    static_assert(coordinates[0].x == 168 && coordinates[0].y == 360);
    static_assert(coordinates[7].x == 2520 && coordinates[7].y == 360);
    static_assert(coordinates[8].x == 2856 && coordinates[8].y == 360);
    static_assert(coordinates[15].x == 5208 && coordinates[15].y == 360);
    static_assert(coordinates[16].y == 1080);
    static_assert(coordinates[32].y == 1800);
    static_assert(coordinates[48].y == 2520);
    static_assert(coordinates[63].y == 2520);

    constexpr auto r11One = 15u << 6;
    constexpr auto g11One = (15u << 6) << 11;
    constexpr auto b10One = (15u << 5) << 22;
    const auto floatOne = decodeR11G11B10Float(
        r11One | g11One | b10One);
    require(
        near(floatOne.red, 1.0f) && near(floatOne.green, 1.0f) &&
            near(floatOne.blue, 1.0f),
        "R11G11B10 one decoding");
    const auto floatZero = decodeR11G11B10Float(0);
    require(
        near(floatZero.red, 0.0f) && near(floatZero.green, 0.0f) &&
            near(floatZero.blue, 0.0f),
        "R11G11B10 zero decoding");

    constexpr auto unorm = decodeR8G8B8A8Unorm(0xFF0080FFu);
    static_assert(unorm.red == 1.0f);
    require(near(unorm.green, 128.0f / 255.0f), "RGBA8 green decoding");
    require(near(unorm.blue, 0.0f), "RGBA8 blue decoding");

    std::array<SceneProbeRgb, kSceneProbeSampleCount> stereoSamples{};
    for (std::size_t index = 0; index < stereoSamples.size(); ++index) {
        const auto column = index % kSceneProbeColumnCount;
        stereoSamples[index] = column < kSceneProbeColumnCount / 2 ?
            SceneProbeRgb{ 1.0f, 0.0f, 0.0f } :
            SceneProbeRgb{ 0.0f, 0.0f, 1.0f };
    }
    const auto summary = summarizeSceneProbe(stereoSamples);
    require(
        summary.validSamples == kSceneProbeSampleCount,
        "all samples are valid");
    require(
        summary.nonBlackSamples == kSceneProbeSampleCount,
        "all samples are non-black");
    require(near(summary.average.red, 0.5f), "combined red average");
    require(near(summary.average.blue, 0.5f), "combined blue average");
    require(near(summary.leftEyeAverage.red, 1.0f), "left-eye partition");
    require(near(summary.rightEyeAverage.blue, 1.0f), "right-eye partition");
    require(
        near(meanAbsoluteSceneProbeDifference(stereoSamples, stereoSamples),
            0.0f),
        "identical samples have zero difference");
    const std::array<SceneProbeRgb, kSceneProbeSampleCount> blackSamples{};
    require(
        near(
            meanAbsoluteSceneProbeDifference(stereoSamples, blackSamples),
            1.0f / 3.0f),
        "mean absolute RGB difference");

    std::cout << "IBL scene-radiance probe model tests passed.\n";
    return EXIT_SUCCESS;
}
