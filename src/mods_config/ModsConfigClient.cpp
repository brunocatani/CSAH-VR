#include "mods_config/ModsConfigClient.h"

#include "mods_config/ModsConfigFO4Api.h"
#include "settings/SettingsFile.h"
#include "settings/SharedSettingsRuntime.h"
#include "support/Logger.h"

#include <format>
#include <string>
#include <type_traits>
#include <variant>

namespace csah::mods_config_client
{
    namespace
    {
        using ::mods_config::api::Change;
        using ::mods_config::api::MenuEventType;
        using ::mods_config::api::ModsConfigFO4Client;
        using ::mods_config::api::Saved;
        using ::mods_config::api::Value;

        // The modName of CSAH's page.
        constexpr const char* kModName = "CSAH";

        // A value of the menu as the text CSAH's settings store reads; the
        // store normalizes it.
        std::string valueText(const Value& value)
        {
            return std::visit(
                [](const auto& held) -> std::string {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, bool>)
                        return held ? "true" : "false";
                    else if constexpr (std::is_same_v<Held, std::string>)
                        return held;
                    else
                        return std::format("{}", held);
                },
                value);
        }

        // A live setting's preview, or its Discard back to the file's value.
        // Every CSAH setting sits in one table of CSAH.toml: its path is the
        // table, then the key.
        void onChange(const Change& change)
        {
            if (change.filePath.empty() || change.path.size() != 2)
                return;
            const auto table = change.path[0];
            const auto key = change.path[1];
            if (change.discard) {
                settings_file::File::get().discardPreview(table, key);
            } else if (std::string error;
                       !settings_file::File::get().setPreview(table, key, valueText(change.value), error)) {
                logging::warn("Mods Config preview of [{}] {} ignored: {}.", table, key, error);
                return;
            }
            shared_settings::wakeMonitor();
        }

        // A Save wrote CSAH.toml: the previews end, and the monitor reads the
        // file at once, so the saved values follow them without a pass of the
        // old ones.
        void onSaved(const Saved& saved)
        {
            settings_file::File::get().endPreviewsForSave();
            shared_settings::wakeMonitor();
            logging::info("Mods Config saved {} CSAH setting(s); reading CSAH.toml now.", saved.settings.size());
        }

        // The menu closing ends every preview, should a Discard not have come
        // for one.
        void onMenuEvent(MenuEventType event, std::string_view)
        {
            if (event != MenuEventType::MenuClosed)
                return;
            settings_file::File::get().clearPreviews();
            shared_settings::wakeMonitor();
        }

        // Mods Config's F4SE message after a Save, sent to the plugins of the
        // page's f4sePluginRequirements. CSAH learns of Saves through onSaved,
        // so it ignores the message; without a listener F4SE logs a failed
        // dispatch.
        void onModsConfigMessage(F4SE::MessagingInterface::Message*) {}
    }

    void connect() noexcept
    {
        try {
            auto& client = ModsConfigFO4Client::get();
            client.addState("modVersion", [] { return std::optional<Value>(std::string(CSAH_VERSION_TEXT)); });
            client.subscribeToChanges(&onChange);
            client.subscribeToSaves(&onSaved);
            client.subscribeToMenuEvents(&onMenuEvent);
            if (const auto* messaging = F4SE::GetMessagingInterface())
                (void)messaging->RegisterListener(&onModsConfigMessage, ::mods_config::api::MODS_CONFIG_FO4_F4SE_NAME);
            if (const int error = client.connect(kModName); error != 0)
                logging::info("Mods Config FO4 VR not connected ({}); CSAH.toml remains the settings file.", error);
            else
                logging::info("Connected to Mods Config FO4 VR (API {}) with live preview.", client.apiVersion());
        } catch (const std::exception& error) {
            logging::warn("Connecting to Mods Config FO4 VR failed: {}.", error.what());
        } catch (...) {
            logging::warn("Connecting to Mods Config FO4 VR failed.");
        }
    }
}
