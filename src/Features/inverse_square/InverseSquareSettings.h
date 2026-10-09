#pragma once
#include <filesystem>
namespace csah::inverse_square
{
    struct Settings
    {
        bool enabled{};
        float size{1.41421356237f};
        float intensity{1};
        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
    [[nodiscard]] Settings loadSettings(const std::filesystem::path&) noexcept;
    [[nodiscard]] Settings loadSettings() noexcept;
}
