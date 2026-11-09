#pragma once

// A CSAH.toml fixture in the test's build folder, read through the same
// store and definitions as the plugin reads the player's file.
#include "settings/SettingsFile.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

class SettingsFixture final
{
public:
    explicit SettingsFixture(std::string_view name) :
        directory_(std::filesystem::path(CSAH_TEST_SETTINGS_DIR) / std::string(name))
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        std::filesystem::create_directories(directory_);
    }

    ~SettingsFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    SettingsFixture(const SettingsFixture&) = delete;
    SettingsFixture& operator=(const SettingsFixture&) = delete;

    void write(std::string_view toml) const
    {
        std::ofstream stream(path(), std::ios::binary | std::ios::trunc);
        stream << toml;
    }

    // The values CSAH reads from the file as last written; every option the
    // file omits has its default.
    [[nodiscard]] csah::settings_file::Values values() const
    {
        auto store = csah::settings_file::makeStore(directory_);
        store->load(false);
        return csah::settings_file::Values(*store);
    }

    [[nodiscard]] std::filesystem::path path() const { return directory_ / csah::settings_file::kFileName; }
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    std::filesystem::path directory_;
};
