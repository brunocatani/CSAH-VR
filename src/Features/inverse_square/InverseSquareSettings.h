#pragma once
#include "settings/SettingsFile.h"
namespace csah::inverse_square
{
    struct Settings
    {
        bool enabled{};
        float size{1.41421356237f};
        float intensity{1};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
