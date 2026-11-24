#pragma once

// CSAH's client of Mods Config FO4 VR's native API. ModsConfigFO4Api.h beside
// it is Mods Config's own client header, copied as is: refresh it from Mods
// Config's src/api/ when its API grows, never edit it.
//
// The client connects under "CSAH", the modName of CSAH's page
// (package/ModsConfigFO4/Config/CSAH/spec.json). Graphics are tuned by eye, so
// each change of a `live` setting becomes a preview value in
// settings_file::File and shows in the game before Save, without writing
// CSAH.toml. Its Discard, or the menu closing, drops it, and the file's value
// returns. A Save ends every preview and has the file read at once instead of
// after the monitor sees it settle. The modVersion state gives the page its
// version line. Without Mods Config nothing is called and CSAH runs as before.
//
// Threads: Mods Config calls the client on the game's main thread. The client
// only records previews in settings_file::File and wakes the shared settings
// monitor, which applies them to the feature runtimes on its own thread as it
// applies a file edit (settings/SharedSettingsRuntime.h).
namespace csah::mods_config_client
{
    // From F4SE's kPostLoad, when every plugin's DLL is loaded: adds CSAH's
    // handlers and connects.
    void connect() noexcept;
}
