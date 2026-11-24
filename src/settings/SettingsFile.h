#pragma once

#include <RPS/Settings/Store.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// CSAH.toml, every CSAH setting, through RPS Framework's settings store.
// Startup writes every missing option at its default and, the first time,
// carries the player's changed values over from CSAH.ini (or the older
// FO4VRCommunityShaders.ini) and deletes it. Mods Config FO4 VR edits the
// same file from CSAH's page (package/ModsConfigFO4/Config/CSAH/spec.json).
namespace csah::settings_file
{
    inline constexpr std::string_view kFileName = "CSAH.toml";

    // Every option, in file order (SettingsDefinitions.cpp).
    [[nodiscard]] const std::vector<RPS::Settings::Definition>& definitions();

    // Preview values by "table.key", each canonical for its option's type.
    using PreviewValues = std::map<std::string, std::string, std::less<>>;

    // An immutable snapshot of every option's value. Feature settings read
    // their options from it; a value is always canonical for its option's type.
    class Values final
    {
    public:
        // Every option at its default.
        Values();
        // The store's values as last loaded, with the previews over them.
        explicit Values(const RPS::Settings::Store& store, const PreviewValues& previews = {});

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

    // Process-lifetime owner of CSAH.toml and of the preview values laid over
    // it while Mods Config's page has unsaved live changes.
    //
    // Threads: startup runs on the plugin's load thread; reload and
    // applyPreviews on the shared settings monitor, the only thread that
    // applies values to the feature runtimes; the preview calls on the game's
    // main thread, where Mods Config calls CSAH's client. The main thread only
    // records previews here and wakes the monitor, so every value reaches the
    // runtimes through the monitor as a file edit does. One mutex serializes
    // the store, the previews and the published values.
    class File final
    {
    public:
        [[nodiscard]] static File& get() noexcept;

        // Organizes the file and migrates the INI once. report receives what
        // was done and every problem. Fails only when the directory is unknown
        // or the store cannot be built.
        [[nodiscard]] bool startup(std::vector<std::string>& report) noexcept;
        // Reads the file again after a change, with the previews over it; the
        // values stay as they were when it cannot be read. previewRevision
        // receives the revision of the previews the values carry.
        [[nodiscard]] Values reload(std::vector<std::string>& report, std::uint64_t& previewRevision) noexcept;
        // The file's values as last read with the current previews over them,
        // reading no file; nullopt while a Save waits to be read (readPending).
        [[nodiscard]] std::optional<Values> applyPreviews(std::uint64_t& previewRevision) noexcept;
        [[nodiscard]] Values current() const;
        [[nodiscard]] std::filesystem::path path() const;
        // Writes the given values to the file; the monitor applies them.
        [[nodiscard]] bool write(std::span<const RPS::Settings::Change> changes, std::string& error) noexcept;

        // Keeps a value of an option over the file's until it is discarded,
        // writing nothing. False for an option CSAH.toml lacks or a value its
        // type cannot hold, with the reason in error.
        [[nodiscard]] bool setPreview(
            std::string_view table, std::string_view key, std::string_view value, std::string& error) noexcept;
        void discardPreview(std::string_view table, std::string_view key) noexcept;
        void clearPreviews() noexcept;
        // Ends every preview after a Save wrote the file: the values stay on
        // the previews until the monitor reads the file, so the saved values
        // replace them without a pass of the old ones.
        void endPreviewsForSave() noexcept;
        // Advances with each change of the previews.
        [[nodiscard]] std::uint64_t previewRevision() const noexcept;
        // A Save ended the previews and the file has not been read since.
        [[nodiscard]] bool readPending() const noexcept;

    private:
        File() = default;

        mutable std::mutex _mutex;
        std::unique_ptr<RPS::Settings::Store> _store;
        std::shared_ptr<const Values> _values{ std::make_shared<const Values>() };
        PreviewValues _previews;
        std::uint64_t _previewRevision{};
        bool _readPending{};
    };
}
