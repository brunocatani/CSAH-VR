#include "support/SettingsPath.h"
#include "settings/FirstRunPreset.h"
#include <Windows.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "Settings path test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    namespace settings_path = csah::settings_path;

    require(
        settings_path::fromDocuments({}).empty(),
        "empty Documents path must fail closed");

    const std::filesystem::path documents = LR"(C:\Users\Test\Documents)";
    const auto actual = settings_path::fromDocuments(documents);
    const auto expected = documents /
        L"My Games" /
        L"Fallout4VR" /
        L"Mods_Config" / L"CSAH" /
        L"CSAH.ini";
    require(actual == expected, "ROCK-style My Games path contract");
    require(
        actual.parent_path().filename() == L"CSAH",
        "plugin must own a dedicated configuration folder");
    require(
        actual.filename() == L"CSAH.ini",
        "INI filename contract");

    const auto resolved = settings_path::resolveIniPath();
    require(!resolved.empty(), "Windows Documents folder must resolve");
    require(
        resolved.parent_path().filename() ==
            L"CSAH",
        "resolved path must use the dedicated configuration folder");
    require(
        resolved.filename() == L"CSAH.ini",
        "resolved path must use the INI filename contract");

    const auto temporary = std::filesystem::temp_directory_path() /
        ("CSAH-settings-migration-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporary);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{ temporary };
    const auto target = temporary / "CSAH" / "CSAH.ini";
    const auto legacy = temporary / "FO4VRCommunityShaders" / "FO4VRCommunityShaders.ini";
    const auto write = [](const auto& path, const std::string& bytes) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "fixture write failed");
    };
    const auto read = [](const auto& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    std::error_code error;
    using Result = settings_path::SetupResult;
    require(settings_path::initializeIni({}, error) == Result::failed && error,
        "empty migration target must fail");
    require(settings_path::initializeIni(target, error) == Result::created && !error,
        "fresh install must create its settings automatically");
    require(read(target) == settings_path::kFirstRunIni,
        "created settings must contain the full shipped preset");
    const auto freshSettings = read(target);
    for (const auto* section : { "[WrappedGrassLighting]", "[HairSpecular]", "[SubsurfaceScattering]" }) {
        require(freshSettings.find(section) == std::string::npos,
            "removed effects must not be written to first-run settings");
    }
    for (const auto* section : { L"CloudShadows", L"VolumetricLighting" }) {
        require(GetPrivateProfileIntW(section, L"bEnabled", 1, target.c_str()) == 0,
            "unfinished effects must be disabled in first-run settings");
    }
    require(GetPrivateProfileIntW(L"LinearLighting", L"bEnabled", 0, target.c_str()) == 1,
        "first-run settings must retain the approved deployed lighting preset");
    std::filesystem::remove(target);
    const std::string original = "\xEF\xBB\xBF; keep comments\r\n[CommunityShaders]\r\nbEnabled=0\r\n";
    write(legacy, original);
    require(settings_path::initializeIni(target, error) == Result::migrated && !error,
        "existing settings should migrate");
    require(read(target) == original && !std::filesystem::exists(legacy),
        "migration must preserve exact bytes and retire the old file");
    require(settings_path::initializeIni(target, error) == Result::existing && !error,
        "migration must be idempotent");
    write(legacy, "legacy settings must not replace CSAH settings");
    require(settings_path::initializeIni(target, error) == Result::existing && !error,
        "existing CSAH settings must take precedence");
    require(read(target) == original && std::filesystem::exists(legacy),
        "existing CSAH settings must not overwrite either file");
    std::filesystem::remove(target);
    std::filesystem::remove(target.parent_path());
    write(target.parent_path(), "blocked directory");
    require(settings_path::initializeIni(target, error) == Result::failed && error,
        "unavailable target directory must report migration failure");
    require(read(legacy) == "legacy settings must not replace CSAH settings",
        "failed migration must preserve the original settings");
    std::filesystem::remove(legacy);
    require(settings_path::initializeIni(target, error) == Result::failed && error,
        "failed first-run creation must report the blocked destination");
    require(read(target.parent_path()) == "blocked directory",
        "failed first-run creation must preserve unrelated files");

    std::cout << "Settings path tests passed.\n";
    return EXIT_SUCCESS;
}
