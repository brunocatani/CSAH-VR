#pragma once

#include "settings/SettingsFile.h"

namespace csah::master_settings
{
    inline constexpr std::string_view kSection = "CommunityShaders";
    inline constexpr std::string_view kEnabledKey = "Enabled";

    [[nodiscard]] inline bool enabled(const settings_file::Values& values) noexcept
    {
        return values.boolean(kSection, kEnabledKey, true);
    }

    // Applies only to CSAH visual features. DLAA/DLSS,
    // Vanilla Fixes, and Native Shadows own independent master gates and
    // deliberately do not call this helper.
    template <class Settings>
    [[nodiscard]] Settings gate(
        const settings_file::Values& values,
        Settings settings) noexcept
    {
        if (!enabled(values)) {
            settings.enabled = false;
        }
        return settings;
    }
}
