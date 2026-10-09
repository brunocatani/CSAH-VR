#include "Features/screen_space_gi/ScreenSpaceGISettings.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <iterator>
#include <string_view>
#include <cstdlib>
namespace csah::screen_space_gi
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings s;
        if(path.empty()) return s;
        const auto boolean=[&](const wchar_t* key,bool fallback) {
            wchar_t value[32]{};
            const auto n=GetPrivateProfileStringW(L"ScreenSpaceGI",key,L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
            return n?contact_shadows::parseBoolean(std::wstring_view(value,n)).value_or(fallback):fallback;
        };
        const auto number=[&](const wchar_t* key,float fallback) {
            wchar_t value[64]{};
            if(!GetPrivateProfileStringW(L"ScreenSpaceGI",key,L"",value,static_cast<DWORD>(std::size(value)),path.c_str())) return fallback;
            wchar_t* end{};const auto parsed=std::wcstof(value,&end);
            return end!=value && *end==0?parsed:fallback;
        };
        s.enabled=boolean(L"bEnabled",s.enabled);s.indirectLight=boolean(L"bIndirectLight",s.indirectLight);
        s.specular=boolean(L"bExperimentalSpecular",s.specular);s.temporal=boolean(L"bTemporal",s.temporal);
        s.radius=number(L"fRadius",s.radius);s.thickness=number(L"fThickness",s.thickness);
        s.aoPower=number(L"fAOPower",s.aoPower);s.strength=number(L"fGIStrength",s.strength);
        return master_settings::gate(path,sanitize(s));
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
