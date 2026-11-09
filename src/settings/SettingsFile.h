#pragma once

#include <RPS/Settings/Store.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// CSAH.toml, every CSAH setting, through RPS Framework's settings store.
// Startup writes every missing option at its default and, the first time,
// carries the player's changed values over from CSAH.ini (or the older
// FO4VRCommunityShaders.ini) and deletes it. RobCo PALM edits the same file.
namespace csah::settings_file
{
    inline constexpr std::string_view kFileName = "CSAH.toml";

    // Every option, in file order (SettingsDefinitions.cpp).
    [[nodiscard]] const std::vector<RPS::Settings::Definition>& definitions();

    // An immutable snapshot of every option's value. Feature settings read
    // their options from it; a value is always canonical for its option's type.
    class Values final
    {
    public:
        // Every option at its default.
        Values();
        explicit Values(const RPS::Settings::Store& store);

        [[nodiscard]] bool boolean(std::string_view table, std::string_view key, bool fallback) const noexcept;
        [[nodiscard]] float number(std::string_view table, std::string_view key, float fallback) const noexcept;
        [[nodiscard]] std::int32_t integer(std::string_view table, std::string_view key, std::int32_t fallback) const noexcept;
        // A negative value gives the fallback.
        [[nodiscard]] std::uint32_t unsignedInteger(
            std::string_view table, std::string_view key, std::uint32_t fallback) const noexcept;

    private:
        [[nodiscard]] const std::string* find(std::string_view table, std::string_view key) const noexcept;

        std::map<std::string, std::string, std::less<>> _values; // "table.key" -> canonical value
    };

    // Documents\My Games\Fallout4VR\Mods_Config\CSAH, or empty when Documents is unavailable.
    [[nodiscard]] std::filesystem::path resolveDirectory() noexcept;
    [[nodiscard]] std::filesystem::path fromDocuments(const std::filesystem::path& documents);

    // The store for a settings directory, with the INIs it migrates.
    [[nodiscard]] std::unique_ptr<RPS::Settings::Store> makeStore(const std::filesystem::path& directory);

    // Process-lifetime owner of CSAH.toml. Thread-safe: startup runs on the
    // plugin's load thread, reloads on the shared settings monitor and writes
    // from feature runtimes; one mutex serializes every store access.
    class File final
    {
    public:
        [[nodiscard]] static File& get() noexcept;

        // Organizes the file and migrates the INI once. report receives what
        // was done and every problem. Fails only when the directory is unknown
        // or the store cannot be built.
        [[nodiscard]] bool startup(std::vector<std::string>& report) noexcept;
        // Reads the file again after a change; the values stay as they were
        // when it cannot be read.
        [[nodiscard]] Values reload(std::vector<std::string>& report) noexcept;
        [[nodiscard]] Values current() const;
        [[nodiscard]] std::filesystem::path path() const;
        // Writes the given values to the file; the monitor applies them.
        [[nodiscard]] bool write(std::span<const RPS::Settings::Change> changes, std::string& error) noexcept;

    private:
        File() = default;

        mutable std::mutex _mutex;
        std::unique_ptr<RPS::Settings::Store> _store;
        std::shared_ptr<const Values> _values{ std::make_shared<const Values>() };
    };
}
