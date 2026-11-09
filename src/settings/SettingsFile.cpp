#include "settings/SettingsFile.h"

#include <Windows.h>
#include <ShlObj.h>

#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace csah::settings_file
{
    namespace
    {
        namespace store = RPS::Settings;

        std::string pathOf(std::string_view table, std::string_view key)
        {
            std::string result;
            result.reserve(table.size() + key.size() + 1);
            result.append(table).append(".").append(key);
            return result;
        }

        // CSAH.ini named every option by its type letter and the same name,
        // in a section named like its table, with the same defaults.
        std::span<const store::LegacyKey> legacyKeys()
        {
            static const auto table = [] {
                struct Table
                {
                    std::vector<std::string> oldKeys;
                    std::vector<store::LegacyKey> keys;
                } result;
                const auto& options = definitions();
                result.oldKeys.reserve(options.size());
                for (const auto& option : options) {
                    const char prefix = option.type == store::ValueType::Boolean ? 'b'
                                      : option.type == store::ValueType::Integer ? 'i'
                                                                                 : 'f';
                    result.oldKeys.push_back(prefix + option.key);
                }
                for (std::size_t index = 0; index < options.size(); ++index) {
                    store::LegacyKey key{};
                    key.oldKey = result.oldKeys[index];
                    key.table = options[index].table;
                    key.key = options[index].key;
                    key.oldDefault = options[index].defaultValue;
                    key.oldSection = options[index].table;
                    result.keys.push_back(key);
                }
                return result;
            }();
            return table.keys;
        }
    }

    Values::Values()
    {
        for (const auto& option : definitions())
            _values.insert_or_assign(pathOf(option.table, option.key), option.defaultValue);
    }

    Values::Values(const RPS::Settings::Store& settings)
    {
        for (const auto& setting : settings.settings())
            _values.insert_or_assign(pathOf(setting.table, setting.key), setting.value);
    }

    const std::string* Values::find(std::string_view table, std::string_view key) const noexcept
    {
        try {
            const auto found = _values.find(pathOf(table, key));
            return found == _values.end() ? nullptr : &found->second;
        } catch (...) {
            return nullptr;
        }
    }

    bool Values::boolean(std::string_view table, std::string_view key, bool fallback) const noexcept
    {
        const auto* value = find(table, key);
        return value ? *value == "true" : fallback;
    }

    float Values::number(std::string_view table, std::string_view key, float fallback) const noexcept
    {
        const auto* value = find(table, key);
        float parsed{};
        if (!value || std::from_chars(value->data(), value->data() + value->size(), parsed).ec != std::errc{} ||
            !std::isfinite(parsed))
            return fallback;
        return parsed;
    }

    std::int32_t Values::integer(std::string_view table, std::string_view key, std::int32_t fallback) const noexcept
    {
        const auto* value = find(table, key);
        std::int32_t parsed{};
        if (!value || std::from_chars(value->data(), value->data() + value->size(), parsed).ec != std::errc{})
            return fallback;
        return parsed;
    }

    std::uint32_t Values::unsignedInteger(std::string_view table, std::string_view key, std::uint32_t fallback) const noexcept
    {
        const auto* value = find(table, key);
        std::uint32_t parsed{};
        if (!value || value->starts_with('-') ||
            std::from_chars(value->data(), value->data() + value->size(), parsed).ec != std::errc{})
            return fallback;
        return parsed;
    }

    std::filesystem::path fromDocuments(const std::filesystem::path& documents)
    {
        if (documents.empty())
            return {};
        return documents / L"My Games" / L"Fallout4VR" / L"Mods_Config" / L"CSAH";
    }

    std::filesystem::path resolveDirectory() noexcept
    {
        try {
            std::array<wchar_t, MAX_PATH> documents{};
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents.data())))
                return {};
            return fromDocuments(documents.data());
        } catch (...) {
            return {};
        }
    }

    std::unique_ptr<RPS::Settings::Store> makeStore(const std::filesystem::path& directory)
    {
        // Read in order, so CSAH.ini wins over the file it was renamed from.
        std::vector<std::filesystem::path> legacy{
            directory.parent_path() / L"FO4VRCommunityShaders" / L"FO4VRCommunityShaders.ini",
            directory / L"CSAH.ini",
        };
        return std::make_unique<store::Store>(
            store::Files{ directory, "CSAH", std::string(kFileName), {},
                "## CSAH.toml - Community Shaders at Home (CSAH) VR settings\n"
                "## RobCo PALM and this file control the same settings. Most apply live; a heading or help line\n"
                "## says where a restart is required. Each option has its help above it.\n"
                "## Your own notes are kept when they start with a single # on the lines above an option.\n",
                {} },
            definitions(), store::LegacyIni{ std::move(legacy), legacyKeys() });
    }

    File& File::get() noexcept
    {
        static File file;
        return file;
    }

    bool File::startup(std::vector<std::string>& report) noexcept
    {
        try {
            std::scoped_lock lock(_mutex);
            if (!_store) {
                const auto directory = resolveDirectory();
                if (directory.empty()) {
                    report.emplace_back("the Documents folder is unavailable");
                    return false;
                }
                std::filesystem::create_directories(directory);
                _store = makeStore(directory);
            }
            _store->load(true);
            report.insert(report.end(), _store->notices().begin(), _store->notices().end());
            report.insert(report.end(), _store->messages().begin(), _store->messages().end());
            _values = std::make_shared<const Values>(*_store);
            return true;
        } catch (const std::exception& error) {
            report.emplace_back(error.what());
            return false;
        }
    }

    Values File::reload(std::vector<std::string>& report) noexcept
    {
        std::scoped_lock lock(_mutex);
        try {
            if (_store) {
                _store->load(false);
                report.insert(report.end(), _store->messages().begin(), _store->messages().end());
                _values = std::make_shared<const Values>(*_store);
            }
        } catch (const std::exception& error) {
            report.emplace_back(error.what());
        }
        return *_values;
    }

    Values File::current() const
    {
        std::scoped_lock lock(_mutex);
        return *_values;
    }

    std::filesystem::path File::path() const
    {
        std::scoped_lock lock(_mutex);
        return _store ? _store->path(RPS::Settings::Group::Player) : std::filesystem::path{};
    }

    bool File::write(std::span<const RPS::Settings::Change> changes, std::string& error) noexcept
    {
        try {
            std::scoped_lock lock(_mutex);
            if (!_store) {
                error = "CSAH.toml is not open";
                return false;
            }
            if (!_store->setValues(RPS::Settings::Group::Player, changes)) {
                error = _store->error();
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
}
