#include "Features/lod_blending/LodBlendingSettingsStore.h"
#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/SettingsPath.h"
#include <Windows.h>
#include <cerrno>
#include <cwchar>
#include <iterator>
#include <string_view>

namespace csah::lod_blending
{
    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        Settings value;
        if(path.empty()) return value;
        const auto readFloat = [&](const wchar_t* key) noexcept {
            wchar_t text[64]{};
            const auto count=GetPrivateProfileStringW(L"LODBlending",key,L"",text,static_cast<DWORD>(std::size(text)),path.c_str());
            if(!count) return 1.0f;
            wchar_t* end{};errno=0;const auto parsed=std::wcstof(text,&end);
            return errno!=ERANGE && end!=text && *end==L'\0' && std::isfinite(parsed) ? parsed : 1.0f;
        };
        wchar_t enabled[64]{};
        const auto count=GetPrivateProfileStringW(L"LODBlending",L"bEnabled",L"",enabled,static_cast<DWORD>(std::size(enabled)),path.c_str());
        if(count) value.enabled=contact_shadows::parseBoolean(std::wstring_view(enabled,count)).value_or(true);
        value.terrainBrightness=readFloat(L"fTerrainBrightness");
        value.terrainGamma=readFloat(L"fTerrainGamma");
        value.objectBrightness=readFloat(L"fObjectBrightness");
        value.objectGamma=readFloat(L"fObjectGamma");
        return master_settings::gate(path,sanitize(value));
    }
    Settings loadSettings() noexcept {return loadSettings(settings_path::resolveIniPath());}
}
