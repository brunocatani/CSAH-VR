#include "Features/ibl/IblSettingsStore.h"
#include "settings/SettingsFile.h"

#include "support/Logger.h"
#include "settings/MasterSettings.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>

namespace csah::ibl
{
    namespace
    {
        constexpr std::string_view kIblSection = "ImageBasedLighting";
        constexpr std::string_view kDynamicCubemapsSection = "DynamicCubemaps";
        constexpr auto kEnabledKey = "Enabled";
        constexpr auto kDynamicCubemapsEnabledKey = "Enabled";
        constexpr auto kDiffuseEnabledKey = "DiffuseEnabled";
        constexpr auto kDiffuseLevelKey = "DiffuseLevel";

        [[nodiscard]] std::wstring normalized(
            std::wstring_view text)
        {
            const auto first = std::ranges::find_if_not(
                text,
                [](wchar_t value) { return std::iswspace(value) != 0; });
            const auto last = std::find_if_not(
                text.rbegin(),
                text.rend(),
                [](wchar_t value) { return std::iswspace(value) != 0; })
                                  .base();
            if (first >= last) {
                return {};
            }
            std::wstring result(first, last);
            std::ranges::transform(
                result,
                result.begin(),
                [](wchar_t value) { return std::towlower(value); });
            return result;
        }

        [[nodiscard]] bool readBoolean(
            const settings_file::Values& values,
            std::string_view section,
            std::string_view key,
            bool fallback) noexcept
        {
            return values.boolean(section, key, fallback);
        }

        [[nodiscard]] float readFloat(
            const settings_file::Values& values,
            std::string_view key,
            float fallback) noexcept
        {
            return values.number(kIblSection, key, fallback);
        }
    }

    std::optional<bool> parseBoolean(std::wstring_view text) noexcept
    {
        try {
            const auto value = normalized(text);
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
        return std::nullopt;
    }

    Settings loadSettings(const settings_file::Values& values) noexcept
    {
        const Settings defaults{};

        return master_settings::gate(values, sanitize({
            .enabled = readBoolean(values,
                kIblSection,
                kEnabledKey,
                defaults.enabled),
            .dynamicCubemapsEnabled = readBoolean(values,
                kDynamicCubemapsSection,
                kDynamicCubemapsEnabledKey,
                defaults.dynamicCubemapsEnabled),
            .diffuseEnabled = readBoolean(values,
                kIblSection,
                kDiffuseEnabledKey,
                defaults.diffuseEnabled),
            .diffuseLevel = readFloat(values,
                kDiffuseLevelKey,
                defaults.diffuseLevel),
        }));
    }

    Settings loadSettings() noexcept
    {
        const auto result = loadSettings(settings_file::File::get().current());
        logging::info(
            "Image Based Lighting settings loaded; enabled={}, Dynamic Cubemaps enabled={}, diffuse enabled={}, diffuse level={}.",
            result.enabled,
            result.dynamicCubemapsEnabled,
            result.diffuseEnabled,
            result.diffuseLevel);
        return result;
    }
}
