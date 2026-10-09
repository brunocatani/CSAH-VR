#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace csah::ibl
{
    constexpr std::size_t kSceneProbeColumnCount = 16;
    constexpr std::size_t kSceneProbeRowCount = 4;
    constexpr std::size_t kSceneProbeSampleCount =
        kSceneProbeColumnCount * kSceneProbeRowCount;

    struct SceneProbeCoordinate
    {
        std::uint32_t x{};
        std::uint32_t y{};
    };

    struct SceneProbeRgb
    {
        float red{};
        float green{};
        float blue{};
    };

    struct SceneProbeSummary
    {
        SceneProbeRgb average{};
        SceneProbeRgb leftEyeAverage{};
        SceneProbeRgb rightEyeAverage{};
        float peak{};
        std::uint32_t validSamples{};
        std::uint32_t nonBlackSamples{};
    };

    [[nodiscard]] constexpr std::array<
        SceneProbeCoordinate,
        kSceneProbeSampleCount>
    sceneProbeCoordinates(
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        std::array<SceneProbeCoordinate, kSceneProbeSampleCount> result{};
        if (width == 0 || height == 0) {
            return result;
        }

        std::size_t index{};
        for (std::size_t row = 0; row < kSceneProbeRowCount; ++row) {
            const auto y = static_cast<std::uint32_t>(
                ((2ull * row + 1ull) * height) /
                (2ull * kSceneProbeRowCount));
            for (std::size_t column = 0;
                 column < kSceneProbeColumnCount;
                 ++column) {
                const auto x = static_cast<std::uint32_t>(
                    ((2ull * column + 1ull) * width) /
                    (2ull * kSceneProbeColumnCount));
                result[index++] = {
                    std::min(x, width - 1),
                    std::min(y, height - 1),
                };
            }
        }
        return result;
    }

    [[nodiscard]] inline float decodeUnsignedFloat(
        std::uint32_t value,
        std::uint32_t mantissaBits) noexcept
    {
        const auto mantissaMask = (1u << mantissaBits) - 1u;
        const auto mantissa = value & mantissaMask;
        const auto exponent = (value >> mantissaBits) & 0x1Fu;
        if (exponent == 0) {
            return std::ldexp(
                static_cast<float>(mantissa),
                1 - 15 - static_cast<int>(mantissaBits));
        }
        if (exponent == 0x1Fu) {
            return mantissa == 0 ?
                std::numeric_limits<float>::infinity() :
                std::numeric_limits<float>::quiet_NaN();
        }
        return std::ldexp(
            1.0f +
                static_cast<float>(mantissa) /
                    static_cast<float>(1u << mantissaBits),
            static_cast<int>(exponent) - 15);
    }

    [[nodiscard]] inline SceneProbeRgb decodeR11G11B10Float(
        std::uint32_t packed) noexcept
    {
        return {
            decodeUnsignedFloat(packed & 0x7FFu, 6),
            decodeUnsignedFloat((packed >> 11) & 0x7FFu, 6),
            decodeUnsignedFloat((packed >> 22) & 0x3FFu, 5),
        };
    }

    [[nodiscard]] constexpr SceneProbeRgb decodeR8G8B8A8Unorm(
        std::uint32_t packed) noexcept
    {
        constexpr auto inverseByteMaximum = 1.0f / 255.0f;
        return {
            static_cast<float>(packed & 0xFFu) * inverseByteMaximum,
            static_cast<float>((packed >> 8) & 0xFFu) *
                inverseByteMaximum,
            static_cast<float>((packed >> 16) & 0xFFu) *
                inverseByteMaximum,
        };
    }

    [[nodiscard]] inline SceneProbeRgb decodeR16G16B16A16Float(
        const std::array<std::uint16_t, 4>& packed) noexcept
    {
        const auto component = [](std::uint16_t bits) {
            const auto value = decodeUnsignedFloat(bits & 0x7FFFu, 10);
            return (bits & 0x8000u) != 0 ? -value : value;
        };
        return { component(packed[0]), component(packed[1]), component(packed[2]) };
    }

    [[nodiscard]] inline SceneProbeSummary summarizeSceneProbe(
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& samples)
        noexcept
    {
        SceneProbeSummary result{};
        std::uint32_t leftSamples{};
        std::uint32_t rightSamples{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto& sample = samples[index];
            if (!std::isfinite(sample.red) ||
                !std::isfinite(sample.green) ||
                !std::isfinite(sample.blue) ||
                sample.red < 0.0f || sample.green < 0.0f ||
                sample.blue < 0.0f) {
                continue;
            }
            result.average.red += sample.red;
            result.average.green += sample.green;
            result.average.blue += sample.blue;
            result.peak = std::max({
                result.peak,
                sample.red,
                sample.green,
                sample.blue,
            });
            if (std::max({ sample.red, sample.green, sample.blue }) >
                0.000001f) {
                ++result.nonBlackSamples;
            }

            const auto leftEye =
                (index % kSceneProbeColumnCount) <
                (kSceneProbeColumnCount / 2);
            auto& eyeAverage = leftEye ?
                result.leftEyeAverage :
                result.rightEyeAverage;
            eyeAverage.red += sample.red;
            eyeAverage.green += sample.green;
            eyeAverage.blue += sample.blue;
            if (leftEye) {
                ++leftSamples;
            } else {
                ++rightSamples;
            }
            ++result.validSamples;
        }

        const auto scale = [](SceneProbeRgb& value, std::uint32_t count) {
            if (count == 0) {
                return;
            }
            const auto inverse = 1.0f / static_cast<float>(count);
            value.red *= inverse;
            value.green *= inverse;
            value.blue *= inverse;
        };
        scale(result.average, result.validSamples);
        scale(result.leftEyeAverage, leftSamples);
        scale(result.rightEyeAverage, rightSamples);
        return result;
    }

    [[nodiscard]] inline float meanAbsoluteSceneProbeDifference(
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& left,
        const std::array<SceneProbeRgb, kSceneProbeSampleCount>& right)
        noexcept
    {
        float sum{};
        std::uint32_t componentCount{};
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto& lhs = left[index];
            const auto& rhs = right[index];
            if (!std::isfinite(lhs.red) || !std::isfinite(lhs.green) ||
                !std::isfinite(lhs.blue) || !std::isfinite(rhs.red) ||
                !std::isfinite(rhs.green) || !std::isfinite(rhs.blue) ||
                lhs.red < 0 || lhs.green < 0 || lhs.blue < 0 ||
                rhs.red < 0 || rhs.green < 0 || rhs.blue < 0) {
                continue;
            }
            sum += std::abs(lhs.red - rhs.red);
            sum += std::abs(lhs.green - rhs.green);
            sum += std::abs(lhs.blue - rhs.blue);
            componentCount += 3;
        }
        return componentCount == 0 ?
            0.0f :
            sum / static_cast<float>(componentCount);
    }
}
