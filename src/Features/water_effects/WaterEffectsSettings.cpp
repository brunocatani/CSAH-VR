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
        count=GetPrivateProfileStringW(L"WaterEffects",L"bEnableParallax",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) settings.parallax=contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(settings.parallax);
        count=GetPrivateProfileStringW(L"WaterEffects",L"bEnableCaustics",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) settings.caustics=contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(settings.caustics);
        count=GetPrivateProfileStringW(L"WaterEffects",L"bEnableReflections",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) settings.reflections=contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(settings.reflections);
        count=GetPrivateProfileStringW(L"WaterEffects",L"fParallaxScale",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.parallaxScale=number;}
        count=GetPrivateProfileStringW(L"WaterEffects",L"fCausticStrength",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.causticStrength=number;}
        count=GetPrivateProfileStringW(L"WaterEffects",L"fReflectionStrength",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.reflectionStrength=number;}
        count=GetPrivateProfileStringW(L"WaterEffects",L"fReflectionDistance",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.reflectionDistance=number;}
        count=GetPrivateProfileStringW(L"WaterEffects",L"bEnableScreenSpaceReflections",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) settings.screenReflections=contact_shadows::parseBoolean(std::wstring_view(value,count)).value_or(settings.screenReflections);
        count=GetPrivateProfileStringW(L"WaterEffects",L"fReflectionFlatness",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.reflectionFlatness=number;}
        count=GetPrivateProfileStringW(L"WaterEffects",L"fReflectionRoughness",L"",value,static_cast<DWORD>(std::size(value)),path.c_str());
        if(count) {wchar_t* end{};auto number=std::wcstof(value,&end);if(end!=value && *end==0) settings.reflectionRoughness=number;}
        return master_settings::gate(path,sanitize(settings));
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
