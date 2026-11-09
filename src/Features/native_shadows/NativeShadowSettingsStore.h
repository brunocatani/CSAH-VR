#pragma once

#include "Features/native_shadows/NativeShadowSettings.h"
#include "settings/SettingsFile.h"

namespace csah::native_shadows
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
