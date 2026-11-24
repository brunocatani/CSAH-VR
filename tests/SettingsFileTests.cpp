// CSAH.toml contracts: CSAH's Mods Config page binds every declared option
// (but those left out by name) once, with its type and the default CSAH
// writes, so Reset matches the mod; a first start writes every option; and the
// INI of earlier versions carries the player's changed values over once and
// is deleted.
#include "settings/SettingsFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace
{
    namespace fs = std::filesystem;
    using RPS::Settings::ValueType;

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            std::cerr << "CSAH settings file test failed: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void write(const fs::path& path, const char* text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
    }

    // Options deliberately not on the page.
    // RenderDocCapture loads RenderDoc for RobCo PALM's Developer wheel and
    // disables DLAA/DLSS; it stays a CSAH.toml edit for developers.
    constexpr std::array<std::string_view, 1> kLeftOut{ "Diagnostics.RenderDocCapture" };

    constexpr std::array<std::string_view, 5> kSettingTypes{ "switcher", "slider", "stepper", "dropdown", "textinput" };

    struct Declared
    {
        ValueType type;
        std::string defaultValue;
    };

    // The page's default as the canonical text CSAH's store writes.
    std::string canonicalDefault(const nlohmann::json& value, ValueType type)
    {
        if (type == ValueType::Boolean)
            return value.get<bool>() ? "true" : "false";
        const auto text = type == ValueType::Integer ? std::to_string(value.get<std::int64_t>()) : value.dump();
        return RPS::Settings::normalizeValue(type, text).value_or("<not a value>");
    }

    void checkSetting(const nlohmann::json& item, const std::map<std::string, Declared>& declared,
        std::set<std::string>& bound)
    {
        const auto& path = item.at("storage").at("path");
        require(path.size() == 2, "a setting's storage is [table, key]");
        const auto name = path[0].get<std::string>() + "." + path[1].get<std::string>();
        const auto found = declared.find(name);
        require(found != declared.end(), "the page binds undeclared option " + name);
        require(bound.insert(name).second, "the page binds " + name + " twice");

        const auto valueType = item.at("valueType").get<std::string>();
        const auto expected = found->second.type == ValueType::Boolean ? "bool"
                            : found->second.type == ValueType::Integer ? "int"
                                                                       : "float";
        require(valueType == expected, "the page's type differs for " + name);
        require(canonicalDefault(item.at("default"), found->second.type) == found->second.defaultValue,
            "the page's default differs from CSAH's for " + name);

        if (!item.contains("valueOptions"))
            return;
        const auto& options = item.at("valueOptions");
        if (options.contains("options")) {
            const auto& choices = options.at("options");
            const bool listed = std::ranges::any_of(choices, [&](const nlohmann::json& choice) {
                return choice.at("value") == item.at("default");
            });
            require(listed, "the default of " + name + " is not one of its choices");
        } else {
            const auto value = item.at("default").get<double>();
            require(options.at("min").get<double>() <= value && value <= options.at("max").get<double>(),
                "the default of " + name + " is outside its range");
        }
    }

    void walk(const nlohmann::json& items, const std::map<std::string, Declared>& declared, std::set<std::string>& bound)
    {
        for (const auto& item : items) {
            const auto type = item.at("type").get<std::string>();
            if (item.contains("content"))
                walk(item.at("content"), declared, bound);
            if (std::ranges::find(kSettingTypes, type) != kSettingTypes.end())
                checkSetting(item, declared, bound);
        }
    }

    void walkPages(const nlohmann::json& pages, const std::map<std::string, Declared>& declared, std::set<std::string>& bound)
    {
        for (const auto& page : pages) {
            walk(page.at("content"), declared, bound);
            if (page.contains("pages"))
                walkPages(page.at("pages"), declared, bound);
        }
    }
}

int main(int argc, char** argv)
{
    require(argc == 2, "usage: CSAHSettingsFileTests <spec.json>");

    // Building the store checks every default is canonical for its type.
    const auto& options = csah::settings_file::definitions();
    std::map<std::string, Declared> declared;
    for (const auto& option : options)
        require(declared.emplace(option.table + "." + option.key, Declared{ option.type, option.defaultValue }).second,
            "duplicate option " + option.key);

    std::ifstream specStream(argv[1]);
    require(specStream.good(), "the page is missing");
    const auto spec = nlohmann::json::parse(specStream);
    require(spec.at("modName") == "CSAH", "the page's modName is the client's name");
    const auto& file = spec.at("files").at(spec.at("defaultFile").get<std::string>());
    require(file.at("root") == "myGames" &&
                file.at("path") == "Mods_Config/CSAH/" + std::string(csah::settings_file::kFileName),
        "the page edits the CSAH.toml CSAH reads");

    std::set<std::string> bound;
    walk(spec.at("content"), declared, bound);
    walkPages(spec.at("pages"), declared, bound);
    for (const auto& [name, option] : declared) {
        const bool leftOut = std::ranges::find(kLeftOut, name) != kLeftOut.end();
        require(bound.contains(name) != leftOut,
            leftOut ? name + " is on the page but listed as left out" : name + " is neither on the page nor left out");
    }

    const fs::path root = fs::path(CSAH_TEST_SETTINGS_DIR) / "SettingsFileTests";
    std::error_code ignored;
    fs::remove_all(root, ignored);

    const auto fresh = root / "Mods_Config" / "CSAH";
    fs::create_directories(fresh);
    auto store = csah::settings_file::makeStore(fresh);
    store->load(true);
    require(store->messages().empty() && fs::exists(fresh / "CSAH.toml"), "a first start writes CSAH.toml cleanly");

    // CSAH.ini wins over the file it was renamed from; a value equal to its
    // old default follows the current default.
    const auto upgrade = root / "upgrade" / "Mods_Config" / "CSAH";
    fs::create_directories(upgrade);
    fs::create_directories(upgrade.parent_path() / "FO4VRCommunityShaders");
    write(upgrade.parent_path() / "FO4VRCommunityShaders" / "FO4VRCommunityShaders.ini",
        "[Bloom]\r\nbEnabled=0\r\n[DLAA]\r\niMode=2\r\n");
    write(upgrade / "CSAH.ini",
        "[DLAA]\r\niMode=3\r\nfSharpness=0.5\r\n[ContactShadows]\r\nbEnabled=1\r\n"
        "[LinearLighting]\r\nfLightGamma=1.79999995\r\n[Retired]\r\nbGone=1\r\n");
    store = csah::settings_file::makeStore(upgrade);
    store->load(true);
    require(!fs::exists(upgrade / "CSAH.ini") &&
            !fs::exists(upgrade.parent_path() / "FO4VRCommunityShaders" / "FO4VRCommunityShaders.ini"),
        "the INIs are deleted after migration");
    const csah::settings_file::Values values(*store);
    require(values.integer("DLAA", "Mode", -1) == 3, "CSAH.ini wins over the older INI");
    require(values.number("DLAA", "Sharpness", -1.0f) == 0.5f, "changed value carried over");
    require(values.boolean("ContactShadows", "Enabled", false), "changed boolean carried over");
    require(!values.boolean("Bloom", "Enabled", true), "older INI value carried over");
    require(values.number("LinearLighting", "LightResponse", -1.0f) == 2.2f, "a retired gamma key does not carry into its response");

    fs::remove_all(root, ignored);
    std::cout << "CSAH settings file tests passed.\n";
    return EXIT_SUCCESS;
}
