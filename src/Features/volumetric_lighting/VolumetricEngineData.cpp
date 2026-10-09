#include "Features/volumetric_lighting/VolumetricEngineData.h"

#include "support/Logger.h"

#include <REL/Relocation.h>
#include <F4SE/F4SE.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace csah::volumetric_lighting
{
    namespace
    {
        constexpr std::uintptr_t kStereoRootCellRva = 0x6235AC8;
        constexpr std::uintptr_t kStereoUploadIdentityRva = 0x1D939AF;
        constexpr std::uintptr_t kStereoPrimaryInverseIdentityRva = 0x1D93C65;
        constexpr std::uintptr_t kStereoMatrix90IdentityRva = 0x1D93A7C;
        constexpr std::uintptr_t kStereoMatrixD0IdentityRva = 0x1D93B75;
        constexpr std::uintptr_t kFirstOriginOffset = 0x2590;
        constexpr std::uintptr_t kRecordsPointerOffset = 0x25D0;
        constexpr std::uintptr_t kRecordStride = 0x210;
        constexpr std::uintptr_t kMatrixD0Offset = 0xD0;
        constexpr std::size_t kRecordsReadSize =
            kRecordStride + kMatrixD0Offset + sizeof(float) * 16;

        constexpr std::uintptr_t kSkySingletonCellRva = 0x5A3D000;
        constexpr std::uintptr_t kTesSingletonCellRva = 0x5B042C0;
        constexpr std::uintptr_t kBeginSunriseCacheRva = 0x5A3D090;
        constexpr std::uintptr_t kSunriseEndCacheRva = 0x5A3CF8C;
        constexpr std::uintptr_t kSunsetBeginCacheRva = 0x5A3CF90;
        constexpr std::uintptr_t kEndSunsetCacheRva = 0x5A3D094;
        constexpr std::uintptr_t kGodraysVtableRva = 0x2CB7CF8;
        constexpr std::uintptr_t kSkySingletonIdentityRva = 0x12FB50;
        constexpr std::uintptr_t kSkyConstructorIdentityRva = 0x6395A5;
        constexpr std::uintptr_t kSkyTimeIdentityRva = 0x63E628;
        constexpr std::uintptr_t kSkyCurrentWeatherIdentityRva = 0x63E64C;
        constexpr std::uintptr_t kSkyBlendIdentityRva = 0x63E6B6;
        constexpr std::uintptr_t kSkyOutgoingWeatherIdentityRva = 0x63E7E0;
        constexpr std::uintptr_t kGetCurrentCellRva = 0xF5360;
        constexpr std::uintptr_t kTesSingletonUseIdentityRva = 0x639BA5;
        constexpr std::uintptr_t kCellGodraysResolverRva = 0x39BA60;
        constexpr std::uintptr_t kWeatherSlotsIdentityRva = 0x47647F;
        constexpr std::uintptr_t kWeatherGodraysIdentityRva = 0x4764EE;
        constexpr std::uintptr_t kGodraysConstructorIdentityRva = 0x35A07E;
        constexpr std::uintptr_t kBeginSunriseIdentityRva = 0x6446FA;
        constexpr std::uintptr_t kSunriseEndIdentityRva = 0x633FED;
        constexpr std::uintptr_t kSunsetBeginIdentityRva = 0x63403D;
        constexpr std::uintptr_t kEndSunsetIdentityRva = 0x64487B;

        constexpr std::uintptr_t kSkyCurrentWeatherOffset = 0x48;
        constexpr std::uintptr_t kSkyOutgoingWeatherOffset = 0x50;
        constexpr std::uintptr_t kSkyGameHourOffset = 0x348;
        constexpr std::uintptr_t kSkyWeatherBlendOffset = 0x350;
        constexpr std::uintptr_t kSkyModeOffset = 0x35C;
        constexpr std::uintptr_t kSkyFlagsOffset = 0x384;
        constexpr std::uintptr_t kWeatherGodraysSlotsOffset = 0xE38;
        constexpr std::size_t kWeatherGodraysSlotCount = 8;
        constexpr std::uint32_t kBoundaryCacheDirtyMask = 0x6C00;
        constexpr std::uint32_t kCellBypassSkyMode = 3;
        constexpr std::uint8_t kGodraysFormType = 0x9D;
        constexpr float kMaximumMagnitude = 1.0e8f;
        constexpr std::array<float, 11> kDefaultMedium{
            0.6f, 1.52f, 3.31f,
            2.0f, 2.0f, 2.0f,
            1.0f, 1.0f, 1.0f,
            0.75f, 0.0f
        };
        constexpr std::array<float, 3> kScatteringConversion{
            0.18f, 0.46f, 1.0f
        };

        constexpr std::array<std::uint8_t, 44> kStereoUploadIdentity{
            0x48, 0x8B, 0x05, 0x12, 0x21, 0x4A, 0x04,
            0x4C, 0x8B, 0x65, 0xB0,
            0x48, 0x8B, 0x0D, 0xFF, 0x20, 0x4A, 0x04,
            0x4C, 0x8D, 0xA8, 0xE0, 0x1E, 0x00, 0x00,
            0x48, 0x85, 0xC0, 0x75, 0x07,
            0x4C, 0x8D, 0xA9, 0xE0, 0x1E, 0x00, 0x00,
            0x4D, 0x8B, 0xAD, 0xF0, 0x06, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 18> kStereoPrimaryInverseIdentity{
            0x4D, 0x8D, 0x45, 0x50,
            0x48, 0x8D, 0x8D, 0xC0, 0x02, 0x00, 0x00,
            0x33, 0xD2,
            0xE8, 0x49, 0xD9, 0x3F, 0xFE
        };
        constexpr std::array<std::uint8_t, 14> kStereoMatrix90Identity{
            0x4D, 0x8D, 0xB5, 0x90, 0x00, 0x00, 0x00,
            0x4C, 0x8D, 0xBF, 0x90, 0x00, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 17> kStereoMatrixD0Identity{
            0x49, 0x8D, 0x9D, 0xD0, 0x00, 0x00, 0x00,
            0x0F, 0x28, 0x23,
            0x0F, 0x28, 0x53, 0x20,
            0x0F, 0x28, 0xDC
        };

        constexpr std::array<std::uint8_t, 20> kSkySingletonIdentity{
            0x48, 0x83, 0xEC, 0x28,
            0x48, 0x8B, 0x05, 0xA5, 0xD4, 0x90, 0x05,
            0x48, 0x85, 0xC0,
            0x0F, 0x85, 0x80, 0x00, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 21> kSkyConstructorIdentity{
            0x48, 0xC7, 0x87, 0x48, 0x03, 0x00, 0x00,
            0x00, 0x00, 0x20, 0x41,
            0xC7, 0x87, 0x5C, 0x03, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 18> kSkyTimeIdentity{
            0xF3, 0x0F, 0x10, 0x86, 0x48, 0x03, 0x00, 0x00,
            0x48, 0x89, 0x44, 0x24, 0x20,
            0xE8, 0xF6, 0x41, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 12> kSkyCurrentWeatherIdentity{
            0x48, 0x8B, 0x4E, 0x48,
            0x44, 0x8B, 0xAC, 0x24, 0xF8, 0x00, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 22> kSkyBlendIdentity{
            0x0F, 0x28, 0xD7,
            0x48, 0x8D, 0x53, 0x20,
            0x48, 0x8D, 0x0D, 0x7C, 0xC6, 0x3F, 0x05,
            0xF3, 0x0F, 0x59, 0x96, 0x50, 0x03, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 21> kSkyOutgoingWeatherIdentity{
            0x48, 0x8B, 0x4E, 0x50,
            0x48, 0x85, 0xC9,
            0x0F, 0x84, 0x78, 0x01, 0x00, 0x00,
            0x44, 0x0F, 0x2F, 0x8E, 0x50, 0x03, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 41> kGetCurrentCellIdentity{
            0x48, 0x8B, 0x41, 0x58,
            0x48, 0x85, 0xC0, 0x75, 0x1E,
            0x8B, 0x51, 0x48,
            0x81, 0xFA, 0xFF, 0xFF, 0xFF, 0x7F,
            0x74, 0x11, 0x44, 0x8B, 0x41, 0x4C,
            0x41, 0x81, 0xF8, 0xFF, 0xFF, 0xFF, 0x7F,
            0x0F, 0x85, 0x8B, 0x36, 0x00, 0x00,
            0x33, 0xC0, 0xF3, 0xC3
        };
        constexpr std::array<std::uint8_t, 17> kTesSingletonUseIdentity{
            0x48, 0x8B, 0x0D, 0x14, 0xA7, 0x4C, 0x05,
            0xE8, 0xAF, 0xB7, 0xAB, 0xFF,
            0x48, 0x85, 0xC0, 0x74, 0x14
        };
        constexpr std::array<std::uint8_t, 48> kCellGodraysResolverIdentity{
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
            0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x48,
            0xE8, 0xDE, 0xA9, 0xCF, 0xFF,
            0x48, 0x85, 0xC0, 0x75, 0x13,
            0x48, 0x8B, 0x8B, 0xD8, 0x00, 0x00, 0x00,
            0x48, 0x85, 0xC9, 0x74, 0x07,
            0x48, 0x8B, 0x81, 0xB0, 0x00, 0x00, 0x00,
            0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
        };
        constexpr std::array<std::uint8_t, 15> kWeatherSlotsIdentity{
            0x48, 0x8D, 0x99, 0x38, 0x0E, 0x00, 0x00,
            0xBE, 0x08, 0x00, 0x00, 0x00,
            0x45, 0x33, 0xFF
        };
        constexpr std::array<std::uint8_t, 93> kWeatherGodraysIdentity{
            0x8B, 0x03, 0x89, 0x45, 0x20, 0x85, 0xC0, 0x74, 0x50,
            0x83, 0xCA, 0xFF, 0x48, 0x8B, 0xCF,
            0xE8, 0xBE, 0x3D, 0xCE, 0xFF,
            0x48, 0x8D, 0x4D, 0x20, 0x48, 0x8B, 0xD0,
            0xE8, 0x12, 0x51, 0xCE, 0xFF,
            0x8B, 0x4D, 0x20,
            0xE8, 0xAA, 0x33, 0xCE, 0xFF,
            0x4C, 0x8D, 0x0D, 0x2B, 0x12, 0x28, 0x03,
            0x4C, 0x8D, 0x05, 0x1C, 0xAC, 0x27, 0x03,
            0x33, 0xD2, 0x48, 0x8B, 0xC8,
            0x44, 0x89, 0x7C, 0x24, 0x20,
            0xE8, 0xCF, 0xB0, 0x51, 0x02,
            0x48, 0x89, 0x03, 0x48, 0x85, 0xC0, 0x75, 0x0C,
            0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF,
            0xFF, 0x90, 0xC8, 0x01, 0x00, 0x00,
            0x48, 0x83, 0xC3, 0x08
        };
        constexpr std::array<std::uint8_t, 109> kGodraysConstructorIdentity{
            0x48, 0x8D, 0x05, 0x73, 0xDC, 0x95, 0x02,
            0x48, 0x89, 0x03,
            0xC7, 0x43, 0x20, 0xEC, 0x51, 0x38, 0x3E,
            0xC7, 0x43, 0x24, 0x1F, 0x85, 0xEB, 0x3E,
            0xC7, 0x43, 0x28, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x2C, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x30, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x34, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x38, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x3C, 0x00, 0x00, 0x00, 0x3F,
            0xC7, 0x43, 0x40, 0x00, 0x00, 0x80, 0x3E,
            0xC7, 0x43, 0x44, 0x00, 0x00, 0x80, 0x3F,
            0xC7, 0x43, 0x48, 0x00, 0x00, 0x40, 0x40,
            0xC7, 0x43, 0x4C, 0x00, 0x00, 0x00, 0x40,
            0x48, 0xC7, 0x43, 0x50, 0x00, 0x00, 0x80, 0x40,
            0xC7, 0x43, 0x58, 0x00, 0x00, 0x40, 0x3F
        };
        constexpr std::array<std::uint8_t, 9> kBeginSunriseIdentity{
            0xF3, 0x0F, 0x10, 0x05, 0x8E, 0x89, 0x3F, 0x05, 0xC3
        };
        constexpr std::array<std::uint8_t, 9> kSunriseEndIdentity{
            0xF3, 0x0F, 0x10, 0x05, 0x97, 0x8F, 0x40, 0x05, 0xC3
        };
        constexpr std::array<std::uint8_t, 9> kSunsetBeginIdentity{
            0xF3, 0x0F, 0x10, 0x05, 0x4B, 0x8F, 0x40, 0x05, 0xC3
        };
        constexpr std::array<std::uint8_t, 9> kEndSunsetIdentity{
            0xF3, 0x0F, 0x10, 0x05, 0x11, 0x88, 0x3F, 0x05, 0xC3
        };

        struct StereoRootFields final
        {
            float firstOrigin[4]{};
            float secondOrigin[4]{};
            std::byte reserved[0x20]{};
            std::uintptr_t records{};
        };

        struct SkyFields final
        {
            std::uintptr_t climate{};
            std::uintptr_t currentWeather{};
            std::uintptr_t outgoingWeather{};
            std::array<std::byte, 0x2F0> beforeGameHour{};
            float gameHour{};
            std::array<std::byte, 4> beforeWeatherBlend{};
            float weatherBlend{};
            std::array<std::byte, 8> beforeMode{};
            std::uint32_t mode{};
            std::array<std::byte, 0x24> beforeFlags{};
            std::uint32_t flags{};
        };

        struct GodraysFormFields final
        {
            std::uintptr_t vtable{};
            std::array<std::byte, 0x12> beforeFormType{};
            std::uint8_t formType{};
            std::array<std::byte, 5> beforeData{};
            std::array<float, 3> colorAir{};
            std::array<float, 3> extinction{};
            std::array<float, 3> emission{};
            float intensity{};
            float scatteringScale{};
            float extinctionMultiplier{};
            float emissionMultiplier{};
            float phaseBack{};
            float phaseForward{};
        };

        static_assert(offsetof(StereoRootFields, records) ==
            kRecordsPointerOffset - kFirstOriginOffset);
        static_assert(sizeof(StereoRootFields) == 0x48);
        static_assert(offsetof(SkyFields, currentWeather) ==
            kSkyCurrentWeatherOffset - 0x40);
        static_assert(offsetof(SkyFields, outgoingWeather) ==
            kSkyOutgoingWeatherOffset - 0x40);
        static_assert(offsetof(SkyFields, gameHour) ==
            kSkyGameHourOffset - 0x40);
        static_assert(offsetof(SkyFields, weatherBlend) ==
            kSkyWeatherBlendOffset - 0x40);
        static_assert(offsetof(SkyFields, mode) == kSkyModeOffset - 0x40);
        static_assert(offsetof(SkyFields, flags) == kSkyFlagsOffset - 0x40);
        static_assert(sizeof(GodraysFormFields) == 0x60);

        using GetCurrentCell = std::uintptr_t (*)(std::uintptr_t);
        using ResolveCellGodrays = std::uintptr_t (*)(std::uintptr_t);

        struct TimeBlend final
        {
            std::uint32_t firstSlot{ 3 };
            std::uint32_t secondSlot{ 3 };
            float firstWeight{ 1.0f };
            float secondWeight{};
        };

        std::mutex gInitializeMutex;
        std::atomic_bool gInitialized{};
        std::uintptr_t gModuleBase{};
        GetCurrentCell gGetCurrentCell{};
        ResolveCellGodrays gResolveCellGodrays{};

        [[nodiscard]] bool plausiblePointer(
            std::uintptr_t address,
            std::size_t size) noexcept
        {
            constexpr std::uintptr_t minimum = 0x10000;
            constexpr std::uintptr_t maximum = 0x00007FFFFFFFFFFF;
            return size != 0 && address >= minimum && address <= maximum &&
                address <= maximum - (size - 1);
        }

        [[nodiscard]] bool readMemory(
            std::uintptr_t address,
            void* destination,
            std::size_t size) noexcept
        {
            if (!destination || !plausiblePointer(address, size)) {
                return false;
            }
            SIZE_T bytesRead{};
            return ReadProcessMemory(
                       GetCurrentProcess(),
                       reinterpret_cast<const void*>(address),
                       destination,
                       size,
                       &bytesRead) != FALSE &&
                bytesRead == size;
        }

        template <std::size_t Size>
        [[nodiscard]] bool matches(
            std::uintptr_t address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            std::array<std::uint8_t, Size> observed{};
            return readMemory(address, observed.data(), observed.size()) &&
                observed == expected;
        }

        [[nodiscard]] bool finiteValues(
            const float* values,
            std::size_t count,
            bool requireNonzero = true) noexcept
        {
            bool nonzero{};
            if (!values) {
                return false;
            }
            for (std::size_t index = 0; index < count; ++index) {
                if (!std::isfinite(values[index]) ||
                    std::fabs(values[index]) > kMaximumMagnitude) {
                    return false;
                }
                nonzero = nonzero || std::fabs(values[index]) > 1.0e-7f;
            }
            return !requireNonzero || nonzero;
        }

        [[nodiscard]] bool verifyIdentities() noexcept
        {
            return
                matches(gModuleBase + kStereoUploadIdentityRva,
                    kStereoUploadIdentity) &&
                matches(gModuleBase + kStereoPrimaryInverseIdentityRva,
                    kStereoPrimaryInverseIdentity) &&
                matches(gModuleBase + kStereoMatrix90IdentityRva,
                    kStereoMatrix90Identity) &&
                matches(gModuleBase + kStereoMatrixD0IdentityRva,
                    kStereoMatrixD0Identity) &&
                matches(gModuleBase + kSkySingletonIdentityRva,
                    kSkySingletonIdentity) &&
                matches(gModuleBase + kSkyConstructorIdentityRva,
                    kSkyConstructorIdentity) &&
                matches(gModuleBase + kSkyTimeIdentityRva,
                    kSkyTimeIdentity) &&
                matches(gModuleBase + kSkyCurrentWeatherIdentityRva,
                    kSkyCurrentWeatherIdentity) &&
                matches(gModuleBase + kSkyBlendIdentityRva,
                    kSkyBlendIdentity) &&
                matches(gModuleBase + kSkyOutgoingWeatherIdentityRva,
                    kSkyOutgoingWeatherIdentity) &&
                matches(gModuleBase + kGetCurrentCellRva,
                    kGetCurrentCellIdentity) &&
                matches(gModuleBase + kTesSingletonUseIdentityRva,
                    kTesSingletonUseIdentity) &&
                matches(gModuleBase + kCellGodraysResolverRva,
                    kCellGodraysResolverIdentity) &&
                matches(gModuleBase + kWeatherSlotsIdentityRva,
                    kWeatherSlotsIdentity) &&
                matches(gModuleBase + kWeatherGodraysIdentityRva,
                    kWeatherGodraysIdentity) &&
                matches(gModuleBase + kGodraysConstructorIdentityRva,
                    kGodraysConstructorIdentity) &&
                matches(gModuleBase + kBeginSunriseIdentityRva,
                    kBeginSunriseIdentity) &&
                matches(gModuleBase + kSunriseEndIdentityRva,
                    kSunriseEndIdentity) &&
                matches(gModuleBase + kSunsetBeginIdentityRva,
                    kSunsetBeginIdentity) &&
                matches(gModuleBase + kEndSunsetIdentityRva,
                    kEndSunsetIdentity);
        }

        [[nodiscard]] bool readTimeBoundaries(
            std::array<float, 4>& values) noexcept
        {
            const std::array<std::uintptr_t, 4> addresses{
                gModuleBase + kBeginSunriseCacheRva,
                gModuleBase + kSunriseEndCacheRva,
                gModuleBase + kSunsetBeginCacheRva,
                gModuleBase + kEndSunsetCacheRva
            };
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (!readMemory(
                        addresses[index],
                        std::addressof(values[index]),
                        sizeof(values[index])) ||
                    !std::isfinite(values[index])) {
                    return false;
                }
            }
            return values[0] >= 0.0f && values[0] < values[1] &&
                values[1] <= values[2] && values[2] < values[3] &&
                values[3] <= 24.0f;
        }

        [[nodiscard]] TimeBlend selectTimeBlend(
            float hour,
            const std::array<float, 4>& boundaries) noexcept
        {
            const auto transition = [hour](
                                        float begin,
                                        float end,
                                        const std::array<std::uint32_t, 5>& slots) {
                TimeBlend result;
                const auto segment = (end - begin) * 0.25f;
                auto segmentEnd = begin + segment;
                for (std::size_t index = 0; index < 4; ++index) {
                    if (hour < segmentEnd || index == 3) {
                        result.firstSlot = slots[index];
                        result.secondSlot = slots[index + 1];
                        result.firstWeight = (segmentEnd - hour) / segment;
                        result.secondWeight = 1.0f - result.firstWeight;
                        return result;
                    }
                    segmentEnd += segment;
                }
                return result;
            };
            if (hour > boundaries[0] && hour < boundaries[1]) {
                return transition(
                    boundaries[0], boundaries[1], { 3, 4, 0, 5, 1 });
            }
            if (hour >= boundaries[1] && hour <= boundaries[2]) {
                return { 1, 1, 1.0f, 0.0f };
            }
            if (hour > boundaries[2] && hour < boundaries[3]) {
                return transition(
                    boundaries[2], boundaries[3], { 1, 6, 2, 7, 3 });
            }
            return { 3, 3, 1.0f, 0.0f };
        }

        [[nodiscard]] bool readGodraysForm(
            std::uintptr_t address,
            GodraysFormFields& form) noexcept
        {
            return readMemory(address, std::addressof(form), sizeof(form)) &&
                form.vtable == gModuleBase + kGodraysVtableRva &&
                form.formType == kGodraysFormType &&
                finiteValues(form.colorAir.data(), 15, false);
        }

        void addDefault(float weight, WeatherFrame& output) noexcept
        {
            for (std::size_t index = 0; index < output.medium.size(); ++index) {
                output.medium[index] += kDefaultMedium[index] * weight;
            }
            output.intensity += weight;
        }

        void addForm(
            const GodraysFormFields& form,
            float weight,
            WeatherFrame& output) noexcept
        {
            for (std::size_t index = 0; index < 3; ++index) {
                output.medium[index] += kScatteringConversion[index] *
                    form.scatteringScale * weight;
                output.medium[index + 3] += form.emission[index] *
                    form.emissionMultiplier * weight;
                output.medium[index + 6] += form.extinction[index] *
                    form.extinctionMultiplier * weight;
            }
            output.medium[9] += form.phaseForward * weight;
            output.medium[10] += form.phaseBack * weight;
            output.intensity += form.intensity * weight;
        }

        [[nodiscard]] bool addWeather(
            std::uintptr_t weather,
            std::uint32_t slot,
            float weight,
            WeatherFrame& output) noexcept
        {
            if (weight <= 0.0f) {
                return true;
            }
            if (slot >= kWeatherGodraysSlotCount) {
                return false;
            }
            if (weather == 0) {
                addDefault(weight, output);
                return true;
            }
            std::uintptr_t godrays{};
            if (!readMemory(
                    weather + kWeatherGodraysSlotsOffset +
                        slot * sizeof(std::uintptr_t),
                    std::addressof(godrays),
                    sizeof(godrays))) {
                return false;
            }
            if (godrays == 0) {
                addDefault(weight, output);
                return true;
            }
            GodraysFormFields form{};
            if (!readGodraysForm(godrays, form)) {
                return false;
            }
            addForm(form, weight, output);
            return true;
        }
    }

    bool initializeEngineData() noexcept
    {
        std::scoped_lock lock(gInitializeMutex);
        if (gInitialized.load(std::memory_order_acquire)) {
            return true;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            return false;
        }
        gModuleBase = REL::Module::get().base();
        if (!verifyIdentities()) {
            logging::critical(
                "Volumetric Lighting rejected the FO4VR stereo/weather layout because an exact executable identity did not match.");
            gModuleBase = 0;
            return false;
        }
        gGetCurrentCell = reinterpret_cast<GetCurrentCell>(
            gModuleBase + kGetCurrentCellRva);
        gResolveCellGodrays = reinterpret_cast<ResolveCellGodrays>(
            gModuleBase + kCellGodraysResolverRva);
        gInitialized.store(true, std::memory_order_release);
        logging::info(
            "Volumetric Lighting initialized exact FO4VR stereo-camera and weather/cell GDRY providers.");
        return true;
    }

    StereoFrame captureStereoFrame() noexcept
    {
        StereoFrame output;
        if (!gInitialized.load(std::memory_order_acquire)) {
            return output;
        }
        std::uintptr_t root{};
        StereoRootFields fields{};
        if (!readMemory(
                gModuleBase + kStereoRootCellRva,
                std::addressof(root),
                sizeof(root)) ||
            !plausiblePointer(root, kFirstOriginOffset + sizeof(fields)) ||
            !readMemory(
                root + kFirstOriginOffset,
                std::addressof(fields),
                sizeof(fields)) ||
            !finiteValues(fields.firstOrigin, 3, false) ||
            !finiteValues(fields.secondOrigin, 3, false) ||
            !plausiblePointer(fields.records, kRecordsReadSize)) {
            return output;
        }
        std::array<std::byte, kRecordsReadSize> records{};
        if (!readMemory(fields.records, records.data(), records.size())) {
            return output;
        }
        const std::array<const float*, 2> origins{
            fields.firstOrigin, fields.secondOrigin
        };
        for (std::size_t eye = 0; eye < 2; ++eye) {
            std::copy_n(origins[eye], 4, output.eyeOrigin[eye].begin());
            output.eyeOrigin[eye][3] = 0.0f;
            std::memcpy(
                output.viewProjection[eye].data(),
                records.data() + eye * kRecordStride + kMatrixD0Offset,
                sizeof(output.viewProjection[eye]));
            if (!finiteValues(
                    output.viewProjection[eye].data(),
                    output.viewProjection[eye].size())) {
                return {};
            }
        }
        output.valid = true;
        return output;
    }

    bool heightFogExterior() noexcept
    {
        if(!gInitialized.load(std::memory_order_acquire)) return false;
        std::uintptr_t tes{},interior{},world{};
        // 1400F5360 uses TES+58 for interiors; 1400F8A10 resolves +D8 world.
        return readMemory(gModuleBase+kTesSingletonCellRva,&tes,sizeof(tes)) && tes &&
            readMemory(tes+0x58,&interior,sizeof(interior)) && !interior &&
            readMemory(tes+0xd8,&world,sizeof(world)) && world;
    }

    WeatherFrame captureWeatherFrame() noexcept
    {
        WeatherFrame output;
        if (!gInitialized.load(std::memory_order_acquire)) {
            return output;
        }
        std::uintptr_t sky{};
        SkyFields skyFields{};
        if (!readMemory(
                gModuleBase + kSkySingletonCellRva,
                std::addressof(sky),
                sizeof(sky)) ||
            sky == 0 ||
            !readMemory(sky + 0x40, std::addressof(skyFields),
                sizeof(skyFields)) ||
            (skyFields.flags & kBoundaryCacheDirtyMask) != 0 ||
            !std::isfinite(skyFields.gameHour) ||
            skyFields.gameHour < 0.0f || skyFields.gameHour > 24.0f ||
            !std::isfinite(skyFields.weatherBlend) ||
            skyFields.weatherBlend < 0.0f || skyFields.weatherBlend > 1.0f ||
            skyFields.mode > 5) {
            return output;
        }
        std::uintptr_t tes{};
        if (!readMemory(
                gModuleBase + kTesSingletonCellRva,
                std::addressof(tes),
                sizeof(tes)) ||
            tes == 0) {
            return output;
        }
        const auto cell = gGetCurrentCell(tes);
        if (cell != 0 && skyFields.mode != kCellBypassSkyMode) {
            const auto godrays = gResolveCellGodrays(cell);
            output.cellOverride = godrays != 0;
            if (godrays == 0) {
                addDefault(1.0f, output);
            } else {
                GodraysFormFields form{};
                if (!readGodraysForm(godrays, form)) {
                    return {};
                }
                addForm(form, 1.0f, output);
            }
            output.valid = true;
            return output;
        }

        std::array<float, 4> boundaries{};
        if (!readTimeBoundaries(boundaries)) {
            return output;
        }
        const auto time = selectTimeBlend(skyFields.gameHour, boundaries);
        const std::array<std::uintptr_t, 2> weather{
            skyFields.currentWeather, skyFields.outgoingWeather
        };
        const std::array<float, 2> weatherWeights{
            skyFields.weatherBlend, 1.0f - skyFields.weatherBlend
        };
        const std::array<std::uint32_t, 2> slots{
            time.firstSlot, time.secondSlot
        };
        const std::array<float, 2> timeWeights{
            time.firstWeight, time.secondWeight
        };
        for (std::size_t weatherIndex = 0; weatherIndex < 2; ++weatherIndex) {
            for (std::size_t timeIndex = 0; timeIndex < 2; ++timeIndex) {
                if (!addWeather(
                        weather[weatherIndex],
                        slots[timeIndex],
                        weatherWeights[weatherIndex] * timeWeights[timeIndex],
                        output)) {
                    return {};
                }
            }
        }
        output.valid = true;
        return output;
    }
}
