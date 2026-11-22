#include "Features/color_grading/ColorGradingTable.h"

#include "support/Logger.h"

#include <combaseapi.h>
#include <wincodec.h>

#include <array>
#include <cstring>
#include <system_error>
#include <vector>

namespace csah::color_grading
{
    namespace
    {
        constexpr std::uint32_t kMaximumTableSize = 256;

        // WIC needs COM on the calling thread. A thread that already runs COM
        // in the other mode reports RPC_E_CHANGED_MODE and still works; only
        // an initialization this scope made is undone.
        class ComScope final
        {
        public:
            ComScope() noexcept :
                result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
            {
            }
            ~ComScope() noexcept
            {
                if (SUCCEEDED(result_)) {
                    CoUninitialize();
                }
            }
            ComScope(const ComScope&) = delete;
            ComScope& operator=(const ComScope&) = delete;

            [[nodiscard]] bool usable() const noexcept
            {
                return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
            }

        private:
            HRESULT result_{};
        };

        [[nodiscard]] Table createTable(
            ID3D11Device* device,
            std::uint32_t size,
            const std::uint8_t* texels,
            bool fromFile) noexcept
        {
            D3D11_TEXTURE3D_DESC description{};
            description.Width = size;
            description.Height = size;
            description.Depth = size;
            description.MipLevels = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = texels;
            data.SysMemPitch = size * 4;
            data.SysMemSlicePitch = size * size * 4;
            Microsoft::WRL::ComPtr<ID3D11Texture3D> texture;
            Table table;
            if (FAILED(device->CreateTexture3D(
                    &description, &data, texture.GetAddressOf())) ||
                FAILED(device->CreateShaderResourceView(
                    texture.Get(), nullptr, table.view.GetAddressOf()))) {
                return {};
            }
            table.size = size;
            table.fromFile = fromFile;
            return table;
        }

        [[nodiscard]] Table decodeTable(
            ID3D11Device* device,
            const std::filesystem::path& file)
        {
            const auto name = file.filename().string();
            const ComScope com;
            if (!com.usable()) {
                logging::error(
                    "Colour Grading could not start the image decoder for '{}'.",
                    name);
                return {};
            }
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            UINT width{};
            UINT height{};
            auto result = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()));
            if (SUCCEEDED(result)) {
                result = factory->CreateDecoderFromFilename(
                    file.c_str(),
                    nullptr,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand,
                    decoder.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = decoder->GetFrame(0, frame.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = frame->GetSize(&width, &height);
            }
            if (FAILED(result)) {
                logging::error(
                    "Colour Grading could not read '{}' (HRESULT=0x{:08X}); it is treated as absent.",
                    name,
                    static_cast<std::uint32_t>(result));
                return {};
            }
            if (height < 2 || height > kMaximumTableSize ||
                width != height * height) {
                logging::error(
                    "Colour Grading rejected '{}': {} x {} is not an unwrapped table (N * N wide, N high, N up to {}).",
                    name,
                    width,
                    height,
                    kMaximumTableSize);
                return {};
            }
            const auto size = static_cast<std::uint32_t>(height);
            std::vector<std::uint8_t> strip(
                static_cast<std::size_t>(width) * height * 4);
            result = factory->CreateFormatConverter(converter.GetAddressOf());
            if (SUCCEEDED(result)) {
                result = converter->Initialize(
                    frame.Get(),
                    GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeCustom);
            }
            if (SUCCEEDED(result)) {
                result = converter->CopyPixels(
                    nullptr,
                    width * 4,
                    static_cast<UINT>(strip.size()),
                    strip.data());
            }
            if (FAILED(result)) {
                logging::error(
                    "Colour Grading could not decode '{}' (HRESULT=0x{:08X}); it is treated as absent.",
                    name,
                    static_cast<std::uint32_t>(result));
                return {};
            }

            // Slice b of the strip starts at column b * N: one row of slice b
            // is one row of the cube at depth b.
            std::vector<std::uint8_t> cube(
                static_cast<std::size_t>(size) * size * size * 4);
            const auto rowBytes = static_cast<std::size_t>(size) * 4;
            for (std::uint32_t blue = 0; blue < size; ++blue) {
                for (std::uint32_t green = 0; green < size; ++green) {
                    const auto* source = strip.data() +
                        (static_cast<std::size_t>(green) * width +
                            static_cast<std::size_t>(blue) * size) * 4;
                    auto* target = cube.data() +
                        (static_cast<std::size_t>(blue) * size + green) *
                            rowBytes;
                    std::memcpy(target, source, rowBytes);
                }
            }
            auto table = createTable(device, size, cube.data(), true);
            if (!table) {
                logging::error(
                    "Colour Grading could not create the GPU table for '{}'.",
                    name);
                return {};
            }
            logging::info(
                "Colour Grading loaded '{}' as a {}-step table.", name, size);
            return table;
        }
    }

    Table createIdentityTable(ID3D11Device* device) noexcept
    {
        if (!device) {
            return {};
        }
        // Two steps per axis are enough: interpolation between the corners of
        // the colour cube is the identity.
        std::array<std::uint8_t, 2 * 2 * 2 * 4> texels{};
        for (std::uint32_t index = 0; index < 8; ++index) {
            texels[index * 4 + 0] = (index & 1u) ? 255 : 0;
            texels[index * 4 + 1] = (index & 2u) ? 255 : 0;
            texels[index * 4 + 2] = (index & 4u) ? 255 : 0;
            texels[index * 4 + 3] = 255;
        }
        return createTable(device, 2, texels.data(), false);
    }

    Table loadTable(
        ID3D11Device* device,
        const std::filesystem::path& file) noexcept
    {
        if (!device) {
            return {};
        }
        try {
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error)) {
                return {};
            }
            return decodeTable(device, file);
        } catch (...) {
            logging::error(
                "Colour Grading failed while loading a table file; it is treated as absent.");
            return {};
        }
    }
}
