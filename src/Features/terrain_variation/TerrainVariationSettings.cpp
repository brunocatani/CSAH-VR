#include "Features/terrain_variation/TerrainVariationSettings.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <iterator>
#include <string_view>
namespace csah::terrain_variation
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings settings;
        if(path.empty()) return settings;
        const auto read=[&](const wchar_t* key,bool fallback) noexcept {
            wchar_t value[32]{};
            const auto count=GetPrivateProfileStringW(L"TerrainVariation",key,L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
            return count?contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(fallback):fallback;
        };
        settings.enabled=read(L"bEnabled",settings.enabled);
        settings.lodEnabled=read(L"bEnableLOD",settings.lodEnabled);
        return master_settings::gate(path,settings);
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
