#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>

namespace csah::diagnostics_settings
{
    inline constexpr auto kSection = L"Diagnostics";
    inline constexpr auto kGpuProfilingModeKey = L"iGpuProfilingMode";

    struct Settings final
    {
        std::uint32_t gpuProfilingGroups{};
        bool measureStereoSharing{};

        [[nodiscard]] bool operator==(
            const Settings&) const noexcept = default;
    };

    [[nodiscard]] inline Settings load(
        const std::filesystem::path& path) noexcept
    {
        if (path.empty()) {
            return {};
        }
        const auto value = static_cast<int>(GetPrivateProfileIntW(
            kSection,
            kGpuProfilingModeKey,
            0,
            path.c_str()));
        Settings result{
            .gpuProfilingGroups = static_cast<std::uint32_t>(
                value >= 0 && value <= 7 ? value : 0),
            .measureStereoSharing = GetPrivateProfileIntW(kSection,L"bMeasureStereoSharing",0,path.c_str()) != 0,
        };
        return result;
    }
}
