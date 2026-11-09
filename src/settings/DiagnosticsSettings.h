#pragma once

#include "settings/SettingsFile.h"

#include <cstdint>

namespace csah::diagnostics_settings
{
    inline constexpr std::string_view kSection = "Diagnostics";
    inline constexpr std::string_view kGpuProfilingModeKey = "GpuProfilingMode";

    struct Settings final
    {
        std::uint32_t gpuProfilingGroups{};
        bool measureStereoSharing{};

        [[nodiscard]] bool operator==(
            const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings load(const settings_file::Values& values) noexcept
    {
        const auto value = values.integer(kSection, kGpuProfilingModeKey, 0);
        return {
            .gpuProfilingGroups = static_cast<std::uint32_t>(value >= 0 && value <= 7 ? value : 0),
            .measureStereoSharing = values.boolean(kSection, "MeasureStereoSharing", false),
        };
    }
}
