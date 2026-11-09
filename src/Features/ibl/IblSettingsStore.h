#pragma once

#include "Features/ibl/IblSettings.h"
#include "settings/SettingsFile.h"

#include <optional>
#include <string_view>

namespace csah::ibl
{
    [[nodiscard]] std::optional<bool> parseBoolean(
        std::wstring_view text) noexcept;
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;

    // Updates only ImageBasedLighting and DynamicCubemaps owned keys and
    // preserves every unrelated INI section and value.
}
