#pragma once

#include "Features/volumetric_lighting/VolumetricLightingSettings.h"
#include "settings/SettingsFile.h"

namespace csah::volumetric_lighting
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
