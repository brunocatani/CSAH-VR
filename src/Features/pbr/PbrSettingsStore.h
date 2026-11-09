#pragma once

#include "Features/pbr/PbrSettings.h"
#include "settings/SettingsFile.h"

namespace csah::pbr
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
