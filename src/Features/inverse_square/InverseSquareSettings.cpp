#include "Features/inverse_square/InverseSquareSettings.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
namespace csah::inverse_square
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings s;if(path.empty()) return s;
        wchar_t value[64]{};auto n=GetPrivateProfileStringW(L"InverseSquareLighting",L"bEnabled",L"",value,64,path.c_str());
        if(n) s.enabled=contact_shadows::parseBoolean(std::wstring_view(value,n)).value_or(false);
        const auto number=[&](const wchar_t* key,float fallback,float lo,float hi){
            auto count=GetPrivateProfileStringW(L"InverseSquareLighting",key,L"",value,64,path.c_str());if(!count) return fallback;
            wchar_t* end{};auto f=std::wcstof(value,&end);return end!=value && !*end && std::isfinite(f)?std::clamp(f,lo,hi):fallback;
        };
        s.size=number(L"fSourceSize",s.size,.25f,16);s.intensity=number(L"fIntensity",s.intensity,0,8);
        return master_settings::gate(path,s);
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
