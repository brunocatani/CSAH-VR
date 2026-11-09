#pragma once

#include "Features/bloom_glare/BloomGlareSettings.h"
#include "settings/SettingsFile.h"

namespace csah::bloom_glare
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
