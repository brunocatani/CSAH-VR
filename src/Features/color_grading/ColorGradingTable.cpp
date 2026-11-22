#include "Features/color_grading/ColorGradingTable.h"

#include "support/Logger.h"

#include <combaseapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace csah::color_grading
{
    namespace
    {
        // 64 steps is the largest table ENB presets ship and is one megabyte
        // on the GPU; every installed look stays loaded.
        constexpr std::uint32_t kMaximumTableSize = 64;
        constexpr std::size_t kMaximumScannedFiles = 4096;
        constexpr std::size_t kDaySlot = 0;
        constexpr std::size_t kNightSlot = 1;
        constexpr std::size_t kInteriorSlot = 2;

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

        // How the slices of a table image are laid out: `columns` slices per
        // row, each `size` x `size`.
        struct Layout final
        {
            std::uint32_t size{};
            std::uint32_t columns{};
        };

        enum class Purpose
        {
            day,
            night,
            interior,
            any,
        };

        struct Candidate final
        {
            std::filesystem::path file{};
            std::string key{};
            Layout layout{};
            Purpose purpose{ Purpose::any };
            bool interiorNight{};
        };

        [[nodiscard]] std::string text(const std::filesystem::path& path)
        {
            const auto encoded = path.generic_u8string();
            return { encoded.begin(), encoded.end() };
        }

        [[nodiscard]] std::string lowered(std::string value)
        {
            for (auto& character : value) {
                if (character >= 'A' && character <= 'Z') {
                    character = static_cast<char>(character - 'A' + 'a');
                }
            }
            return value;
        }

        [[nodiscard]] bool contains(
            const std::string& value,
            std::string_view part) noexcept
        {
            return value.find(part) != std::string::npos;
        }

        [[nodiscard]] std::optional<Layout> layoutOf(
            UINT width,
            UINT height,
            bool namedTable) noexcept
        {
            if (height >= 2 && height <= kMaximumTableSize &&
                width == height * height) {
                return Layout{ height, height };
            }
            // A square image is a table only when its name says so: lens and
            // noise textures of a preset are square too.
            if (namedTable && width == height) {
                for (std::uint32_t columns = 4; columns <= 8; ++columns) {
                    const auto size = columns * columns;
                    if (size * columns == width) {
                        return Layout{ size, columns };
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool imageExtension(const std::filesystem::path& file)
        {
            const auto extension = lowered(text(file.extension()));
            return extension == ".png" || extension == ".bmp" ||
                extension == ".jpg" || extension == ".jpeg" ||
                extension == ".tif" || extension == ".tiff" ||
                extension == ".dds";
        }

        [[nodiscard]] HRESULT openFrame(
            IWICImagingFactory* factory,
            const std::filesystem::path& file,
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode>& frame,
            UINT& width,
            UINT& height)
        {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            auto result = factory->CreateDecoderFromFilename(
                file.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf());
            if (SUCCEEDED(result)) {
                result = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = frame->GetSize(&width, &height);
            }
            return result;
        }

        // Adds the file when it is a table image. Only its size is read here.
        void consider(
            IWICImagingFactory* factory,
            const std::filesystem::path& root,
            const std::filesystem::path& file,
            std::vector<Candidate>& candidates)
        {
            if (!imageExtension(file)) {
                return;
            }
            const auto stem = lowered(text(file.stem()));
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            UINT width{};
            UINT height{};
            if (FAILED(openFrame(factory, file, frame, width, height))) {
                return;
            }
            const auto layout = layoutOf(width, height, contains(stem, "lut"));
            if (!layout) {
                return;
            }
            Candidate candidate;
            candidate.file = file;
            candidate.key = lowered(text(file.lexically_relative(root)));
            candidate.layout = *layout;
            if (contains(stem, "interior") || contains(stem, "indoor")) {
                candidate.purpose = Purpose::interior;
                candidate.interiorNight = contains(stem, "night");
            } else if (contains(stem, "night")) {
                candidate.purpose = Purpose::night;
            } else if (contains(stem, "day")) {
                candidate.purpose = Purpose::day;
            }
            candidates.push_back(std::move(candidate));
        }

        [[nodiscard]] std::vector<Candidate> collect(
            IWICImagingFactory* factory,
            const std::filesystem::path& root,
            bool recursive)
        {
            std::vector<Candidate> candidates;
            std::error_code error;
            std::size_t scanned = 0;
            const auto visit = [&](const std::filesystem::directory_entry& entry) {
                std::error_code entryError;
                if (entry.is_regular_file(entryError)) {
                    consider(factory, root, entry.path(), candidates);
                }
            };
            if (recursive) {
                std::filesystem::recursive_directory_iterator iterator(
                    root,
                    std::filesystem::directory_options::skip_permission_denied,
                    error);
                const std::filesystem::recursive_directory_iterator end;
                for (; !error && iterator != end &&
                       scanned < kMaximumScannedFiles;
                     iterator.increment(error), ++scanned) {
                    visit(*iterator);
                }
            } else {
                std::filesystem::directory_iterator iterator(root, error);
                const std::filesystem::directory_iterator end;
                for (; !error && iterator != end &&
                       scanned < kMaximumScannedFiles;
                     iterator.increment(error), ++scanned) {
                    visit(*iterator);
                }
            }
            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const Candidate& left, const Candidate& right) {
                    return left.key < right.key;
                });
            return candidates;
        }

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

        [[nodiscard]] Table decode(
            ID3D11Device* device,
            IWICImagingFactory* factory,
            const Candidate& candidate)
        {
            const auto name = text(candidate.file.filename());
            const auto size = candidate.layout.size;
            const auto columns = candidate.layout.columns;
            const UINT expectedWidth = size * columns;
            const UINT expectedHeight = size * (size / columns);
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            UINT width{};
            UINT height{};
            auto result =
                openFrame(factory, candidate.file, frame, width, height);
            if (SUCCEEDED(result) &&
                (width != expectedWidth || height != expectedHeight)) {
                result = E_FAIL;
            }
            std::vector<std::uint8_t> image;
            if (SUCCEEDED(result)) {
                image.resize(static_cast<std::size_t>(width) * height * 4);
                result =
                    factory->CreateFormatConverter(converter.GetAddressOf());
            }
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
                    static_cast<UINT>(image.size()),
                    image.data());
            }
            if (FAILED(result)) {
                logging::error(
                    "Colour Grading could not decode '{}' (HRESULT=0x{:08X}); it is left out.",
                    name,
                    static_cast<std::uint32_t>(result));
                return {};
            }

            // Slice b sits at column b % columns, row b / columns of the
            // image; one row of a slice is one row of the cube at depth b.
            std::vector<std::uint8_t> cube(
                static_cast<std::size_t>(size) * size * size * 4);
            const auto rowBytes = static_cast<std::size_t>(size) * 4;
            for (std::uint32_t blue = 0; blue < size; ++blue) {
                const auto sliceColumn =
                    static_cast<std::size_t>(blue % columns) * size;
                const auto sliceRow =
                    static_cast<std::size_t>(blue / columns) * size;
                for (std::uint32_t green = 0; green < size; ++green) {
                    const auto* source = image.data() +
                        ((sliceRow + green) * width + sliceColumn) * 4;
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
            }
            return table;
        }

        [[nodiscard]] const Candidate* choose(
            const std::vector<Candidate>& candidates,
            Purpose purpose) noexcept
        {
            const Candidate* fallback{};
            const Candidate* any{};
            for (const auto& candidate : candidates) {
                if (candidate.purpose == purpose) {
                    // An interior table for the night is second choice: there
                    // is one interior table, and most interiors are lit.
                    if (!candidate.interiorNight) {
                        return &candidate;
                    }
                    if (!fallback) {
                        fallback = &candidate;
                    }
                } else if (candidate.purpose == Purpose::any && !any) {
                    any = &candidate;
                }
            }
            return fallback ? fallback : any;
        }

        // Builds one look from the table images found for it. Returns false
        // when none of them could be used.
        [[nodiscard]] bool buildLook(
            ID3D11Device* device,
            IWICImagingFactory* factory,
            const std::vector<Candidate>& candidates,
            const Table& identity,
            std::size_t number,
            const std::string& name,
            TableSet& look)
        {
            constexpr std::array<Purpose, kTableSlotCount> purposes{
                Purpose::day, Purpose::night, Purpose::interior
            };
            std::array<const Candidate*, kTableSlotCount> chosen{};
            std::array<std::string, kTableSlotCount> files{
                "none", "none", "none"
            };
            bool usable = false;
            for (std::size_t slot = 0; slot < kTableSlotCount; ++slot) {
                chosen[slot] = choose(candidates, purposes[slot]);
                look[slot] = identity;
                if (!chosen[slot]) {
                    continue;
                }
                // One file can serve several cases; decode it once.
                for (std::size_t earlier = 0; earlier < slot; ++earlier) {
                    if (chosen[earlier] == chosen[slot]) {
                        look[slot] = look[earlier];
                        files[slot] = files[earlier];
                        break;
                    }
                }
                if (files[slot] != "none") {
                    continue;
                }
                if (auto table = decode(device, factory, *chosen[slot])) {
                    look[slot] = std::move(table);
                    files[slot] = chosen[slot]->key;
                    usable = true;
                }
            }
            if (!usable) {
                return false;
            }
            logging::info(
                "Colour Grading look {}: '{}' (day: {}, night: {}, interior: {}).",
                number,
                name,
                files[kDaySlot],
                files[kNightSlot],
                files[kInteriorSlot]);
            return true;
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

    LookLibrary loadLooks(
        ID3D11Device* device,
        const std::filesystem::path& folder) noexcept
    {
        LookLibrary library;
        if (!device) {
            return library;
        }
        try {
            std::error_code error;
            if (!std::filesystem::is_directory(folder, error)) {
                return library;
            }
            const auto identity = createIdentityTable(device);
            const ComScope com;
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            if (!identity || !com.usable() ||
                FAILED(CoCreateInstance(
                    CLSID_WICImagingFactory,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(factory.GetAddressOf())))) {
                logging::error(
                    "Colour Grading could not start the image decoder; no look is loaded.");
                return library;
            }

            const auto add = [&](const std::vector<Candidate>& candidates,
                                 const std::string& name) {
                if (!candidates.empty() &&
                    buildLook(
                        device,
                        factory.Get(),
                        candidates,
                        identity,
                        library.count + 1,
                        name,
                        library.looks[library.count])) {
                    ++library.count;
                }
            };
            add(collect(factory.Get(), folder, false), "files in the folder");

            std::vector<std::pair<std::string, std::filesystem::path>> folders;
            std::filesystem::directory_iterator iterator(folder, error);
            const std::filesystem::directory_iterator end;
            for (; !error && iterator != end &&
                   folders.size() < kMaximumScannedFiles;
                 iterator.increment(error)) {
                std::error_code entryError;
                if (iterator->is_directory(entryError)) {
                    folders.emplace_back(
                        lowered(text(iterator->path().filename())),
                        iterator->path());
                }
            }
            std::sort(folders.begin(), folders.end());
            for (const auto& [key, path] : folders) {
                if (library.count == kMaximumLooks) {
                    logging::warn(
                        "Colour Grading holds {} looks; '{}' and any later folder are left out.",
                        kMaximumLooks,
                        text(path.filename()));
                    break;
                }
                add(collect(factory.Get(), path, true), text(path.filename()));
            }
        } catch (...) {
            logging::error(
                "Colour Grading failed while loading looks; the ones already loaded stay.");
        }
        return library;
    }
}
