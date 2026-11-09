#pragma once

#include "Features/filmic_tonemapping/FilmicTonemappingSettings.h"
#include "settings/SettingsFile.h"

namespace csah::filmic_tonemapping
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
