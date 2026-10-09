#pragma once
#include <filesystem>
namespace csah::terrain_shadows
{
    struct Settings
    {
        bool enabled{};
        float strength{1};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
