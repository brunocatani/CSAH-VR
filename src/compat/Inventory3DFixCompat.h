#pragma once

namespace csah::compat::inventory_3d_fix
{
    // Limits Inventory3DFix's material save and restore to lighting
    // materials. Call once on the main thread after every F4SE plugin has
    // loaded and before the first frame is drawn.
    void apply() noexcept;
}
