#pragma once

#include "Features/linear_lighting/LinearLightingSettings.h"
#include "settings/SettingsFile.h"

namespace csah::linear_lighting
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;

    // Updates owned keys individually and preserves unrelated INI content.
}
