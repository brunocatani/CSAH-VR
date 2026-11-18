#pragma once

namespace csah::render::engine_cubemap_array
{
    // FO4VR packs every material envmap into one texture-cube array that the
    // reflection composite samples at t8. Its cached command record copies
    // the array's texture object when the record is built (0x14291B349); a
    // record built before the engine creates the array (the first world
    // frame after a direct save load) keeps the engine's null stand-in for
    // the whole session, so every metal loses its cubemap reflection.
    //
    // install() makes the array getter hand out a stable stand-in texture
    // while the array does not exist yet, and publishes the array's view
    // into that stand-in as soon as the engine creates it. Records built
    // early then sample the real array on every replay.
    [[nodiscard]] bool install() noexcept;
}
