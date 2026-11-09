#include "Features/bloom_glare/BloomGlareSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <cmath>
#include <string_view>

namespace csah::bloom_glare
{
    namespace
    {
        constexpr std::string_view kBloomSection = "Bloom";
        constexpr std::string_view kGlareSection = "PhysicalGlare";

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view section,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(section, key, fallback);
        }

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view section,
            std::string_view key,
            float fallback) noexcept
        {
            return values.number(section, key, fallback);
        }

        [[nodiscard]] std::uint32_t readUnsigned(
            const settings_file::Values& values,
            std::string_view section,
            std::string_view key,
            std::uint32_t fallback) noexcept
        {
            return values.unsignedInteger(section, key, fallback);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        auto result = sanitize({
            .bloom = {
                .enabled = readBoolean(values, kBloomSection, "Enabled", defaults.bloom.enabled),
                .thresholdEV = readFloat(values,
                    kBloomSection,
                    "ThresholdEV",
                    defaults.bloom.thresholdEV),
                .intensity = readFloat(values,
                    kBloomSection,
                    "Intensity",
                    defaults.bloom.intensity),
                .radius = readFloat(values,
                    kBloomSection,
                    "Radius",
                    defaults.bloom.radius),
            },
            .glare = {
                .enabled = readBoolean(values, kGlareSection, "Enabled", defaults.glare.enabled),
                .thresholdEV = readFloat(values,
                    kGlareSection,
                    "ThresholdEV",
                    defaults.glare.thresholdEV),
                .intensity = readFloat(values,
                    kGlareSection,
                    "Intensity",
                    defaults.glare.intensity),
                .fftResolution = readUnsigned(values,
                    kGlareSection,
                    "FftResolution",
                    defaults.glare.fftResolution),
                .paddingRatio = readFloat(values,
                    kGlareSection,
                    "PaddingRatio",
                    defaults.glare.paddingRatio),
                .apertureMode = readUnsigned(values,
                    kGlareSection,
                    "ApertureMode",
                    defaults.glare.apertureMode),
                .apertureBlades = readUnsigned(values,
                    kGlareSection,
                    "ApertureBlades",
                    defaults.glare.apertureBlades),
                .apertureRotationDegrees = readFloat(values,
                    kGlareSection,
                    "ApertureRotationDegrees",
                    defaults.glare.apertureRotationDegrees),
                .fStop = readFloat(values,
                    kGlareSection,
                    "FStop",
                    defaults.glare.fStop),
                .fresnelExponent = readFloat(values,
                    kGlareSection,
                    "FresnelExponent",
                    defaults.glare.fresnelExponent),
                .sphericalAberration = readFloat(values,
                    kGlareSection,
                    "SphericalAberration",
                    defaults.glare.sphericalAberration),
                .chromaticSpread = readFloat(values,
                    kGlareSection,
                    "ChromaticSpread",
                    defaults.glare.chromaticSpread),
                .kernelScale = readFloat(values,
                    kGlareSection,
                    "KernelScale",
                    defaults.glare.kernelScale),
                .psfSharpness = readFloat(values,
                    kGlareSection,
                    "PsfSharpness",
                    defaults.glare.psfSharpness),
                .psfNoiseFloor = readFloat(values,
                    kGlareSection,
                    "PsfNoiseFloor",
                    defaults.glare.psfNoiseFloor),
            },
        });
        if (!master_settings::enabled(values)) {
            result.bloom.enabled = false;
            result.glare.enabled = false;
        }
        return result;
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Bloom/Glare settings loaded; bloom={} thresholdEV={} intensity={} radius={}, glare={} thresholdEV={} intensity={} FFT={} apertureMode={} blades={}.",
            result.bloom.enabled,
            result.bloom.thresholdEV,
            result.bloom.intensity,
            result.bloom.radius,
            result.glare.enabled,
            result.glare.thresholdEV,
            result.glare.intensity,
            result.glare.fftResolution,
            result.glare.apertureMode,
            result.glare.apertureBlades);
        return result;
    }
}
