#pragma once

#include "Features/complex_materials/ComplexParallaxSettings.h"
#include "settings/SettingsFile.h"

namespace csah::complex_materials
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
