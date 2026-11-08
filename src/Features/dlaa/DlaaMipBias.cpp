#include "PCH.h"
#include "Features/dlaa/DlaaMipBias.h"

#include "support/Logger.h"

#include <d3d11.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace csah::dlaa::mip_bias
{
    namespace
    {
        constexpr std::size_t kFilterCount = 5;
        constexpr std::size_t kAddressModeCount = 4;
        constexpr std::size_t kMipCount = 16;
        constexpr std::size_t kEntryCount =
            kFilterCount * kAddressModeCount * kMipCount;
        static_assert(kEntryCount == 320);

        constexpr std::uintptr_t kTableRva = 0x062350A0;
        constexpr std::uintptr_t kCreateAnchorRva = 0x01DBF0B8;  // LEA R12,[rip]
        constexpr std::uintptr_t kRecordAnchorRva = 0x0292F944;  // LEA RCX,[rip]
        constexpr std::uintptr_t kBindSiteRva = 0x01D9BBB0;      // LEA R9,[disp32+R8*8]

        // D3D11_FILTER per filter index of the creation loop.
        constexpr std::array<D3D11_FILTER, kFilterCount> kExpectedFilters{
            D3D11_FILTER_MIN_MAG_MIP_POINT,
            D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
            D3D11_FILTER_MIN_MAG_MIP_LINEAR,
            D3D11_FILTER_ANISOTROPIC,
            D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        };
        constexpr std::uint32_t kExpectedPerFilter =
            static_cast<std::uint32_t>(kAddressModeCount * kMipCount);

        // A handful of distinct biases exist (one per upscaling mode), so the
        // retained duplicates stay bounded.
        constexpr std::size_t kMaximumBuilds = 8;

        struct Build
        {
            std::uint32_t biasBits{};
            std::array<ID3D11SamplerState*, kEntryCount> image{};
        };

        struct State
        {
            ID3D11SamplerState** table{};
            std::array<ID3D11SamplerState*, kEntryCount> originals{};
            std::array<bool, kEntryCount> marked{};
            bool originalsCached{};
            std::array<Build, kMaximumBuilds> builds{};
            std::size_t buildCount{};
            const Build* active{};
            bool applied{};
            bool failureLogged{};
        };

        State& state() noexcept
        {
            static State value;
            return value;
        }

        [[nodiscard]] std::uintptr_t ripTarget(
            std::uintptr_t base,
            std::uintptr_t rva,
            std::array<std::uint8_t, 3> opcode) noexcept
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(base + rva);
            if (std::memcmp(bytes, opcode.data(), opcode.size()) != 0) {
                return 0;
            }
            std::int32_t displacement{};
            std::memcpy(&displacement, bytes + 3, sizeof(displacement));
            return base + rva + 7 + displacement;
        }

        void logOnce(const char* message) noexcept
        {
            auto& current = state();
            if (!current.failureLogged) {
                current.failureLogged = true;
                logging::warn("{}", message);
            }
        }

        [[nodiscard]] bool cacheOriginals() noexcept
        {
            auto& current = state();
            if (current.originalsCached) {
                return true;
            }
            std::array<std::uint32_t, kFilterCount> histogram{};
            std::uint32_t other{};
            for (std::size_t index = 0; index < kEntryCount; ++index) {
                auto* sampler = current.table[index];
                if (!sampler) {
                    return false;
                }
                D3D11_SAMPLER_DESC description{};
                sampler->GetDesc(&description);
                bool known = false;
                for (std::size_t filter = 0; filter < kFilterCount; ++filter) {
                    if (description.Filter == kExpectedFilters[filter]) {
                        ++histogram[filter];
                        known = true;
                    }
                }
                other += known ? 0u : 1u;
                current.originals[index] = sampler;
                current.marked[index] =
                    description.Filter == D3D11_FILTER_ANISOTROPIC;
            }
            for (const auto count : histogram) {
                if (count != kExpectedPerFilter) {
                    logging::warn(
                        "DLAA mip bias rejected the sampler table: filter histogram {}/{}/{}/{}/{} with {} unknown, expected 64 each.",
                        histogram[0], histogram[1], histogram[2], histogram[3],
                        histogram[4], other);
                    return false;
                }
            }
            current.originalsCached = other == 0;
            return current.originalsCached;
        }
    }

    bool install() noexcept
    {
        try {
            const auto base = REL::Module::get().base();
            const auto create = ripTarget(base, kCreateAnchorRva, { 0x4C, 0x8D, 0x25 });
            const auto record = ripTarget(base, kRecordAnchorRva, { 0x48, 0x8D, 0x0D });
            const auto* bind = reinterpret_cast<const std::uint8_t*>(base + kBindSiteRva);
            constexpr std::array<std::uint8_t, 4> bindPrefix{ 0x4E, 0x8D, 0x0C, 0xC5 };
            std::uint32_t bindRva{};
            std::memcpy(&bindRva, bind + bindPrefix.size(), sizeof(bindRva));
            const auto agreed = create != 0 && create == record &&
                std::memcmp(bind, bindPrefix.data(), bindPrefix.size()) == 0 &&
                bindRva == kTableRva && create == base + kTableRva;
            if (!agreed) {
                logging::warn(
                    "DLAA mip bias left inert: the FO4VR sampler-table anchors disagree (creation 0x{:X}, record 0x{:X}, bind 0x{:X}).",
                    create ? create - base : 0,
                    record ? record - base : 0,
                    bindRva);
                return false;
            }
            state().table = reinterpret_cast<ID3D11SamplerState**>(create);
            logging::info(
                "DLAA mip bias verified the FO4VR sampler table at RVA 0x{:X} from three agreeing anchors.",
                kTableRva);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool build(ID3D11Device* device, float bias) noexcept
    {
        auto& current = state();
        if (!current.table || !device || current.applied) {
            return false;
        }
        const auto bits = std::bit_cast<std::uint32_t>(bias);
        if (current.active && current.active->biasBits == bits) {
            return true;
        }
        for (std::size_t index = 0; index < current.buildCount; ++index) {
            if (current.builds[index].biasBits == bits) {
                current.active = &current.builds[index];
                return true;
            }
        }
        if (!cacheOriginals()) {
            logOnce("DLAA mip bias left inert: the sampler table is not in its native state.");
            return false;
        }
        if (current.buildCount >= kMaximumBuilds) {
            logOnce("DLAA mip bias reached its retained build limit; the current bias stays.");
            return current.active != nullptr;
        }
        Build next{ .biasBits = bits, .image = current.originals };
        std::uint32_t created{};
        for (std::size_t index = 0; index < kEntryCount; ++index) {
            if (!current.marked[index]) {
                continue;
            }
            D3D11_SAMPLER_DESC description{};
            current.originals[index]->GetDesc(&description);
            description.MipLODBias = bias;
            ID3D11SamplerState* duplicate{};
            if (FAILED(device->CreateSamplerState(&description, &duplicate)) ||
                !duplicate) {
                // Created duplicates of this incomplete build stay referenced
                // through the process like every other duplicate.
                logOnce("DLAA mip bias could not create a biased sampler state; the game's samplers stay in use.");
                return false;
            }
            next.image[index] = duplicate;
            ++created;
        }
        current.builds[current.buildCount] = next;
        current.active = &current.builds[current.buildCount];
        ++current.buildCount;
        logging::info(
            "DLAA mip bias built {} anisotropic sampler states with MipLODBias {:.4f}; comparison, point and trilinear samplers keep the game's bias.",
            created,
            bias);
        return true;
    }

    void apply() noexcept
    {
        auto& current = state();
        if (!current.table || !current.active || current.applied) {
            return;
        }
        for (std::size_t index = 0; index < kEntryCount; ++index) {
            if (current.marked[index] &&
                current.table[index] != current.originals[index]) {
                logOnce("DLAA mip bias declined: another owner changed the sampler table.");
                return;
            }
        }
        for (std::size_t index = 0; index < kEntryCount; ++index) {
            if (current.marked[index]) {
                current.table[index] = current.active->image[index];
            }
        }
        current.applied = true;
    }

    void restore() noexcept
    {
        auto& current = state();
        if (!current.applied || !current.table) {
            return;
        }
        for (std::size_t index = 0; index < kEntryCount; ++index) {
            if (current.marked[index]) {
                current.table[index] = current.originals[index];
            }
        }
        current.applied = false;
    }

    bool applied() noexcept
    {
        return state().applied;
    }
}
