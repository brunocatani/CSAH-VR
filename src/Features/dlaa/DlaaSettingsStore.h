#pragma once

#include "Features/dlaa/DlaaSettings.h"
#include "settings/SettingsFile.h"

namespace csah::dlaa
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
