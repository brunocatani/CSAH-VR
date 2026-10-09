#include "Features/water_effects/WaterEffectsSettings.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <iterator>
#include <string_view>
#include <cstdlib>
namespace csah::water_effects
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings settings;
        if(path.empty()) return settings;
        wchar_t value[64]{};
        auto count=GetPrivateProfileStringW(L"WaterEffects",L"bEnabled",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) settings.enabled=contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(settings.enabled);
        count=GetPrivateProfileStringW(L"WaterEffects",L"fParallaxScale",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.parallaxScale=number;}
        return master_settings::gate(path,sanitize(settings));
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
