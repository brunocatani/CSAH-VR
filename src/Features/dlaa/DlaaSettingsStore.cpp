#include "Features/dlaa/DlaaSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"

#include <array>
#include <string>

namespace csah::dlaa
{
    namespace
    {
        constexpr std::string_view kSection = "DLAA";

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(kSection, key, fallback);
        }

        [[nodiscard]] std::uint32_t readUnsigned(
            const settings_file::Values& values,
            std::string_view key,
            std::uint32_t fallback) noexcept
        {
            return values.unsignedInteger(kSection, key, fallback);
        }

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view key,
            float fallback) noexcept
        {
            return values.number(kSection, key, fallback);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        return sanitize({
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .mode = static_cast<Mode>(readUnsigned(values,
                "Mode",
                static_cast<std::uint32_t>(defaults.mode))),
            .modelPreset = static_cast<ModelPreset>(readUnsigned(values,
                "ModelPreset",
                static_cast<std::uint32_t>(defaults.modelPreset))),
            .motionVectorRepair = readBoolean(values,
                "MotionVectorRepair",
                defaults.motionVectorRepair),
            .sharpening = readBoolean(values,
                "PostSharpening",
                defaults.sharpening),
            .sharpness = readFloat(values,
                "Sharpness",
                defaults.sharpness),
            .centerWidth = readFloat(values,
                "CenterWidth",
                defaults.centerWidth),
            .centerHeight = readFloat(values,
                "CenterHeight",
                defaults.centerHeight),
            .centerFeatherPixels = readFloat(values,
                "CenterFeatherPixels",
                defaults.centerFeatherPixels),
            .visualizeCenter = readBoolean(values,
                "VisualizeCenter",
                defaults.visualizeCenter),
            .hardResetOnLoad = readBoolean(values,
                "HardResetOnLoad",
                defaults.hardResetOnLoad),
            .verboseDiagnostics = readBoolean(values,
                "VerboseDiagnostics",
                defaults.verboseDiagnostics),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto settings = loadSettings(settings_file::File::get().current());
        logging::info(
            "Upscaling settings loaded; enabled={}, mode={}, modelPreset={}, motionVectorRepair={}, CAS={}, sharpness={}, center={}x{}, feather={}px, visualizeCenter={}, hardResetOnLoad={}, diagnostics={}.",
            settings.enabled,
            modeName(settings.mode),
            static_cast<std::uint32_t>(settings.modelPreset),
            settings.motionVectorRepair,
            settings.sharpening,
            settings.sharpness,
            settings.centerWidth,
            settings.centerHeight,
            settings.centerFeatherPixels,
            settings.visualizeCenter,
            settings.hardResetOnLoad,
            settings.verboseDiagnostics);
        return settings;
    }
}
