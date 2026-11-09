#pragma once
#include "settings/SettingsFile.h"
namespace csah::terrain_variation
{
    struct Settings
    {
        bool enabled{false};
        bool lodEnabled{true};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
