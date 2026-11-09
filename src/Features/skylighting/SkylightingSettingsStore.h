#pragma once

#include "Features/skylighting/SkylightingSettings.h"
#include "settings/SettingsFile.h"

namespace csah::skylighting
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
