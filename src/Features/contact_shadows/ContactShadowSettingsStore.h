#pragma once

#include "Features/contact_shadows/ContactShadowSettings.h"
#include "settings/SettingsFile.h"


namespace csah::contact_shadows
{
    [[nodiscard]] Settings loadSettings(const settings_file::Values& values) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
