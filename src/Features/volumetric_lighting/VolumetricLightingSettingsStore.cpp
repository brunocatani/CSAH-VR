#include "Features/volumetric_lighting/VolumetricLightingSettingsStore.h"

#include "Features/contact_shadows/ContactShadowSettingsStore.h"
#include "settings/MasterSettings.h"
#include "support/Logger.h"
#include "support/SettingsPath.h"

#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <system_error>

namespace csah::volumetric_lighting
{
    namespace
    {
        constexpr auto kSection = L"VolumetricLighting";

        [[nodiscard]] bool readBoolean(
            const std::filesystem::path& path,
            const wchar_t* key,
            bool fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            return count == 0 ? fallback :
                contact_shadows::parseBoolean(
                    std::wstring_view(value, count)).value_or(fallback);
        }

        [[nodiscard]] float readFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstof(value, &end);
            return errno != ERANGE && end != value && *end == L'\0' &&
                    std::isfinite(parsed) ?
                parsed : fallback;
        }

        [[nodiscard]] std::uint32_t readUnsigned(
            const std::filesystem::path& path,
            const wchar_t* key,
            std::uint32_t fallback) noexcept
        {
            wchar_t value[64]{};
            const auto count = GetPrivateProfileStringW(
                kSection, key, L"", value, static_cast<DWORD>(std::size(value)),
                path.c_str());
            if (count == 0) {
                return fallback;
            }
            wchar_t* end{};
            errno = 0;
            const auto parsed = std::wcstoul(value, &end, 10);
            return errno != ERANGE && end != value && *end == L'\0' ?
                static_cast<std::uint32_t>(parsed) : fallback;
        }

        [[nodiscard]] bool writeValue(
            const std::filesystem::path& path,
            const wchar_t* key,
            const wchar_t* value) noexcept
        {
            return WritePrivateProfileStringW(
                       kSection, key, value, path.c_str()) != FALSE;
        }

        [[nodiscard]] bool writeFloat(
            const std::filesystem::path& path,
            const wchar_t* key,
            float value) noexcept
        {
            wchar_t text[64]{};
            _snwprintf_s(
                text,
                std::size(text),
                _TRUNCATE,
                L"%.9g",
                static_cast<double>(value));
            return writeValue(path, key, text);
        }
    }

    Settings loadSettings(const std::filesystem::path& path) noexcept
    {
        const Settings defaults{};
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) ||
            error) {
            return defaults;
        }
        auto result = sanitize({
            .enabled = readBoolean(path, L"bEnabled", defaults.enabled),
            .quality = readUnsigned(path, L"iQuality", defaults.quality),
            .intensity = readFloat(path, L"fIntensity", defaults.intensity),
            .baseScattering = readFloat(
                path, L"fBaseScattering", defaults.baseScattering),
            .shaftIntensity = readFloat(
                path, L"fShaftIntensity", defaults.shaftIntensity),
            .densityContribution = readFloat(
                path, L"fDensityContribution", defaults.densityContribution),
            .densityScale = readFloat(
                path, L"fDensityScale", defaults.densityScale),
            .windSpeed = readFloat(path, L"fWindSpeed", defaults.windSpeed),
            .maxDistance = readFloat(
                path, L"fMaxDistance", defaults.maxDistance),
            .temporalWeight = readFloat(
                path, L"fTemporalWeight", defaults.temporalWeight),
            .heightFog = readBoolean(path,L"bHeightFogEnabled",defaults.heightFog),
            .heightFogDensity = readFloat(path,L"fHeightFogDensity",defaults.heightFogDensity),
            .heightFogHeight = readFloat(path,L"fHeightFogHeight",defaults.heightFogHeight),
            .heightFogFalloff = readFloat(path,L"fHeightFogFalloff",defaults.heightFogFalloff),
            .heightFogStart = readFloat(path,L"fHeightFogStart",defaults.heightFogStart),
            .heightFogOpacity = readFloat(path,L"fHeightFogOpacity",defaults.heightFogOpacity),
            .heightFogWeather = readFloat(path,L"fHeightFogWeather",defaults.heightFogWeather),
            .heightFogVolume = readFloat(path,L"fHeightFogVolume",defaults.heightFogVolume),
        });
        if(!master_settings::enabled(path)) {result.enabled=false;result.heightFog=false;}
        return result;
    }

    Settings loadSettings() noexcept
    {
        const auto path = settings_path::resolveIniPath();
        const auto result = loadSettings(path);
        logging::info(
            "Volumetric Lighting settings loaded from '{}'; enabled={}, quality={}, intensity={}, base={}, shafts={}, densityContribution={}, densityScale={}, windSpeed={}, maxDistance={}, temporalWeight={}.",
            path.string(),
            result.enabled,
            result.quality,
            result.intensity,
            result.baseScattering,
            result.shaftIntensity,
            result.densityContribution,
            result.densityScale,
            result.windSpeed,
            result.maxDistance,
            result.temporalWeight);
        return result;
    }

    bool saveSettings(
        const std::filesystem::path& path,
        const Settings& settings) noexcept
    {
        if (!settings_path::ensureParentDirectory(path)) {
            return false;
        }
        const auto safe = sanitize(settings);
        wchar_t quality[16]{};
        _snwprintf_s(
            quality,
            std::size(quality),
            _TRUNCATE,
            L"%u",
            safe.quality);
        auto success = writeValue(
            path, L"bEnabled", safe.enabled ? L"1" : L"0");
        success = writeValue(path, L"iQuality", quality) && success;
        success = writeFloat(path, L"fIntensity", safe.intensity) && success;
        success = writeFloat(
                      path, L"fBaseScattering", safe.baseScattering) &&
            success;
        success = writeFloat(
                      path, L"fShaftIntensity", safe.shaftIntensity) &&
            success;
        success = writeFloat(
                      path,
                      L"fDensityContribution",
                      safe.densityContribution) &&
            success;
        success = writeFloat(path, L"fDensityScale", safe.densityScale) &&
            success;
        success = writeFloat(path, L"fWindSpeed", safe.windSpeed) && success;
        success = writeFloat(path, L"fMaxDistance", safe.maxDistance) &&
            success;
        success = writeFloat(
                      path, L"fTemporalWeight", safe.temporalWeight) &&
            success;
        success=writeValue(path,L"bHeightFogEnabled",safe.heightFog?L"1":L"0") && success;
        success=writeFloat(path,L"fHeightFogDensity",safe.heightFogDensity) && success;
        success=writeFloat(path,L"fHeightFogHeight",safe.heightFogHeight) && success;
        success=writeFloat(path,L"fHeightFogFalloff",safe.heightFogFalloff) && success;
        success=writeFloat(path,L"fHeightFogStart",safe.heightFogStart) && success;
        success=writeFloat(path,L"fHeightFogOpacity",safe.heightFogOpacity) && success;
        success=writeFloat(path,L"fHeightFogWeather",safe.heightFogWeather) && success;
        success=writeFloat(path,L"fHeightFogVolume",safe.heightFogVolume) && success;
        return success;
    }

    bool saveSettings(const Settings& settings) noexcept
    {
        return saveSettings(settings_path::resolveIniPath(), settings);
    }
}
