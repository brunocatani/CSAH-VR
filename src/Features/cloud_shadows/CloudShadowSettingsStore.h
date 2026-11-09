#pragma once

#include "Features/cloud_shadows/CloudShadowSettings.h"
#include "settings/SettingsFile.h"

namespace csah::cloud_shadows
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
