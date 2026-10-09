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
        bool captureFrame{};
        std::array<wchar_t, 1024> captureDirectory{};

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
            .captureFrame = GetPrivateProfileIntW(kSection, L"bCaptureFrame", 0, path.c_str()) != 0,
        };
        const auto length = GetPrivateProfileStringW(kSection, L"sCaptureDirectory", L"",
            result.captureDirectory.data(), static_cast<DWORD>(result.captureDirectory.size()), path.c_str());
        if (length >= result.captureDirectory.size() - 1) result.captureDirectory = {};
        return result;
    }
}
