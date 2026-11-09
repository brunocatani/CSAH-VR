#pragma once
#include "Features/lod_blending/LodBlendingSettings.h"
#include "settings/SettingsFile.h"
namespace csah::lod_blending
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
