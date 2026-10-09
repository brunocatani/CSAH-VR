#pragma once
#include <filesystem>
namespace csah::terrain_variation
{
    struct Settings
    {
        bool enabled{false};
        bool lodEnabled{true};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
