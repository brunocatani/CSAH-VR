#pragma once

#include "Features/basic_wetness/BasicWetnessSettings.h"
#include "settings/SettingsFile.h"

namespace csah::basic_wetness
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
