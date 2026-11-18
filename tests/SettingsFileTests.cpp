// CSAH.toml contracts: every PALM catalog binding names a declared option with
// its type, a first start writes every option, and the INI of earlier versions
// carries the player's changed values over once and is deleted.
#include "settings/SettingsFile.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

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
}

int main(int argc, char** argv)
{
    require(argc == 2, "usage: CSAHSettingsFileTests <PALMSettings.json>");

    // Building the store checks every default is canonical for its type.
    const auto& options = csah::settings_file::definitions();
    std::map<std::string, ValueType> declared;
    for (const auto& option : options)
        require(declared.emplace(option.table + "." + option.key, option.type).second, "duplicate option " + option.key);

    std::ifstream catalogStream(argv[1]);
    const auto catalog = nlohmann::json::parse(catalogStream);
    require(catalog.at("schemaVersion") == 2, "catalog schema");
    std::size_t bindings = 0;
    for (const auto& tab : catalog.at("tabs"))
        for (const auto& page : tab.at("pages"))
            for (const auto& group : page.at("groups"))
                for (const auto& control : group.at("controls")) {
                    const auto& binding = control.at("binding");
                    const auto name = binding.at("table").get<std::string>() + "." + binding.at("key").get<std::string>();
                    const auto found = declared.find(name);
                    require(found != declared.end(), "catalog binds undeclared option " + name);
                    const auto value = binding.at("value").get<std::string>();
                    const auto expected = value == "boolean" ? ValueType::Boolean
                                        : value == "integer" ? ValueType::Integer
                                                             : ValueType::Float;
                    require(found->second == expected, "catalog type differs for " + name);
                    ++bindings;
                }
    require(bindings + 1 == options.size(), "every option except RenderDocCapture has a PALM control");

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
