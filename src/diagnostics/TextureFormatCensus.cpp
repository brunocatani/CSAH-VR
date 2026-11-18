#include "diagnostics/TextureFormatCensus.h"

#include "support/Logger.h"

#include <wrl/client.h>

#include <array>
#include <string>

namespace csah::diagnostics
{
    namespace
    {
        constexpr std::size_t kFamilies = 9;
        constexpr std::size_t kSlots = 8;
        constexpr std::size_t kFormatsPerSlot = 6;
        constexpr std::uint32_t kDrawsPerFamily = 4000;

        struct FormatCount
        {
            DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
            std::uint32_t count{};
        };

        struct FamilyCensus
        {
            std::uint32_t draws{};
            bool logged{};
            std::array<std::array<FormatCount, kFormatsPerSlot>, kSlots> slots{};
        };

        std::array<FamilyCensus, kFamilies> census{};

        constexpr const char* familyName(std::size_t family) noexcept
        {
            constexpr std::array<const char*, kFamilies> names{
                "none", "material", "sky", "distantTree", "particle", "water", "vlsComposite", "effect", "dFLightAmbient"
            };
            return family < names.size() ? names[family] : "?";
        }

        void record(std::array<FormatCount, kFormatsPerSlot>& slot, DXGI_FORMAT format) noexcept
        {
            for (auto& entry : slot) {
                if (entry.count != 0 && entry.format == format) {
                    ++entry.count;
                    return;
                }
                if (entry.count == 0) {
                    entry = { format, 1 };
                    return;
                }
            }
        }

        void log(std::size_t family, const FamilyCensus& value)
        {
            std::string text;
            for (std::size_t slot = 0; slot < kSlots; ++slot) {
                text += " t" + std::to_string(slot) + "=[";
                for (const auto& entry : value.slots[slot]) {
                    if (entry.count == 0) break;
                    text += std::to_string(static_cast<int>(entry.format)) + "x" + std::to_string(entry.count) + " ";
                }
                text += "]";
            }
            logging::info("Texture format census family={} draws={}:{}", familyName(family), value.draws, text);
        }
    }

    void observeReplacementDraw(ID3D11DeviceContext* context, std::uint8_t family) noexcept
    {
        if (!context || family == 0 || family >= kFamilies) return;
        auto& value = census[family];
        if (value.logged) return;
        std::array<ID3D11ShaderResourceView*, kSlots> views{};
        context->PSGetShaderResources(0, kSlots, views.data());
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            view.Attach(views[slot]);
            if (!view) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            view->GetDesc(&desc);
            record(value.slots[slot], desc.Format);
        }
        if (++value.draws < kDrawsPerFamily) return;
        value.logged = true;
        try {
            log(family, value);
        } catch (...) {
        }
    }
}
