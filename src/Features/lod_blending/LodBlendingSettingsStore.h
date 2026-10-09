#pragma once
#include "Features/lod_blending/LodBlendingSettings.h"
#include <filesystem>
namespace csah::lod_blending
{
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
