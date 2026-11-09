#pragma once

#include "Features/vanilla_fixes/VanillaFixesSettings.h"
#include "settings/SettingsFile.h"


namespace csah::vanilla_fixes
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
