#pragma once
#include "settings/SettingsFile.h"
namespace csah::terrain_shadows
{
    struct Settings
    {
        bool enabled{};
        float strength{1};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
