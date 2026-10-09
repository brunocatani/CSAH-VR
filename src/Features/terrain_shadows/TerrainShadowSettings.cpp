#include "Features/terrain_shadows/TerrainShadowSettings.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
namespace csah::terrain_shadows
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings s;
        if(path.empty()) return s;
        wchar_t value[64]{};
        auto n=GetPrivateProfileStringW(L"TerrainShadows",L"bEnabled",L"",value,64,path.c_str());
        if(n) s.enabled=contact_shadows::parseBoolean(std::wstring_view(value,n)).value_or(false);
        n=GetPrivateProfileStringW(L"TerrainShadows",L"fStrength",L"",value,64,path.c_str());
        if(n) {wchar_t* end{};const auto number=std::wcstof(value,&end);if(end!=value && !*end && std::isfinite(number)) s.strength=std::clamp(number,0.0f,1.0f);}
        return master_settings::gate(path,s);
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
