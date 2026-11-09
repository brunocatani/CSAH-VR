#pragma once

#include "Features/sky_sync/SkySyncSettings.h"
#include "settings/SettingsFile.h"

namespace csah::sky_sync
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
