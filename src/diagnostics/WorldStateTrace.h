#pragma once

namespace csah::diagnostics
{
    // Per-location feature and Sun diagnostics. Called once per Sky::Update
    // on the main thread while the world is loaded, never during a cell load. Logs
    // one line on entering a cell (location), one after it settles (feature
    // activity in that cell) and one on leaving it (activity over the stay),
    // so interior and exterior behaviour can be compared from CSAH.log.
    void onWorldStateFrame() noexcept;
}
