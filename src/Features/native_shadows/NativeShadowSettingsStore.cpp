#include "Features/native_shadows/NativeShadowSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"

#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>

namespace csah::native_shadows
{
    namespace
    {
        constexpr std::string_view kSection = "NativeShadows";

        [[nodiscard]] bool parseBoolean(
            const std::wstring_view source,
            const bool fallback) noexcept
        {
            try {
                std::wstring value;
                value.reserve(source.size());
                for (const auto character : source) {
                    if (std::iswspace(character) == 0) {
                        value.push_back(
                            static_cast<wchar_t>(std::towlower(character)));
                    }
                }
                if (value == L"1" || value == L"true" || value == L"yes" ||
                    value == L"on") {
                    return true;
                }
                if (value == L"0" || value == L"false" || value == L"no" ||
                    value == L"off") {
                    return false;
                }
            } catch (...) {
            }
            return fallback;
        }

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view key,
            const bool fallback) noexcept
        {
            return values.boolean(kSection, key, fallback);
        }

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view key,
            const float fallback) noexcept
        {
            return values.number(kSection, key, fallback);
        }

        [[nodiscard]] std::uint32_t readUnsigned(
            const settings_file::Values& values,
            std::string_view key,
            const std::uint32_t fallback) noexcept
        {
            return values.unsignedInteger(kSection, key, fallback);
        }
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};
        return sanitize({
            .enabled = readBoolean(values, "Enabled", defaults.enabled),
            .extendedDirectionalCascades = readBoolean(values,
                "ExtendedDirectionalCascades",
                defaults.extendedDirectionalCascades),
            .tiledDeferredLighting = readBoolean(values,
                "TiledDeferredLighting",
                defaults.tiledDeferredLighting),
            .directionalShadowDistance = readFloat(values,
                "DirectionalShadowDistance",
                defaults.directionalShadowDistance),
            .cascadeBlendDistance = readFloat(values,
                "CascadeBlendDistance",
                defaults.cascadeBlendDistance),
            .orthographicShadowFilter = readUnsigned(values,
                "OrthographicShadowFilter",
                defaults.orthographicShadowFilter),
        });
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Native Shadows settings loaded; enabled={}, fourCascades={}, tiledDeferredLighting={}, fixedDistance={}, cascadeBlend={}, orthoFilter={}. No FPS or adaptive-quality controller is present.",
            result.enabled,
            result.extendedDirectionalCascades,
            result.tiledDeferredLighting,
            result.directionalShadowDistance,
            result.cascadeBlendDistance,
            result.orthographicShadowFilter);
        return result;
    }
}
