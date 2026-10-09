#include "Features/ibl/IblEnvironmentProvider.h"
#include "Features/ibl/IblEnvironmentUpdater.h"
#include "Features/ibl/IblSceneRadianceProbeModel.h"
#include "Features/ibl/IblReflectionFreeCapture.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::ibl::EnvironmentUpdateConsumeResult;
    using csah::ibl::EnvironmentProvider;
    using csah::ibl::EnvironmentProviderState;
    using csah::ibl::EnvironmentUpdater;

    struct Float4
    {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    constexpr std::uint32_t kR11Half = 15U << 6;
    constexpr std::uint32_t kHalfRed = kR11Half;
    constexpr std::uint32_t kHalfGreen = kR11Half << 11;
    constexpr std::uint32_t kHalfBlue = (15U << 5) << 22;

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            fail(message);
        }
    }

    void requireSucceeded(HRESULT result, const std::string& operation)
    {
        if (FAILED(result)) {
            fail(operation + " failed with HRESULT " +
                std::to_string(static_cast<unsigned>(result)));
        }
    }

    [[nodiscard]] std::vector<std::byte> readFile(
        const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        require(stream.is_open(), "could not open " + path.string());
        const auto size = stream.tellg();
        require(size > 0, "shader bytecode is empty");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "could not read shader bytecode");
        return bytes;
    }

    struct Device
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    [[nodiscard]] Device createDevice(D3D_DRIVER_TYPE driver = D3D_DRIVER_TYPE_WARP)
    {
        Device result;
        constexpr std::array levels{ D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selected{};
        requireSucceeded(
            D3D11CreateDevice(
                nullptr,
                driver,
                nullptr,
                0,
                levels.data(),
                static_cast<UINT>(levels.size()),
                D3D11_SDK_VERSION,
                &result.device,
                &selected,
                &result.context),
            "D3D11CreateDevice(WARP)");
        require(
            selected == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");
        return result;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createPackedRadiance(
        ID3D11Device& device,
        std::uint32_t leftPixel,
        std::uint32_t rightPixel)
    {
        constexpr UINT width = 32;
        constexpr UINT height = 16;
        std::array<std::uint32_t, width * height> pixels{};
        for (UINT y = 0; y < height; ++y) {
            for (UINT x = 0; x < width; ++x) {
                pixels[y * width + x] =
                    x < width / 2 ? leftPixel : rightPixel;
            }
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{
            pixels.data(),
            width * sizeof(std::uint32_t),
            0,
        };
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(radiance)");
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(texture.Get(), nullptr, &view),
            "CreateShaderResourceView(radiance)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createPackedDepth(
        ID3D11Device& device,
        std::uint32_t depthBits = 0x007FFFFFU)
    {
        constexpr UINT width = 32;
        constexpr UINT height = 16;
        const std::uint32_t halfDepth = depthBits;
        std::array<std::uint32_t, width * height> pixels{};
        pixels.fill(halfDepth);
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R24G8_TYPELESS;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{
            pixels.data(),
            width * sizeof(std::uint32_t),
            0,
        };
        ComPtr<ID3D11Texture2D> texture;
        requireSucceeded(
            device.CreateTexture2D(&description, &initial, &texture),
            "CreateTexture2D(depth)");
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        requireSucceeded(
            device.CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view),
            "CreateShaderResourceView(depth)");
        return view;
    }

    [[nodiscard]] ComPtr<ID3D11Buffer> createSceneConstants(
        ID3D11Device& device,
        float forwardSign = 1.0F, float originX = 0.0F)
    {
        std::array<Float4, 85> rows{};
        rows[59].x = rows[60].x = originX;
        // Match the updater's ordinary view/projection inputs. The retired
        // compressed projection rows at 63..70 are not consumed by the shader.
        rows[0] = { forwardSign, 0.0f, 0.0f, 0.0f };
        rows[1] = { 0.0f, 1.0f, 0.0f, 0.0f };
        rows[2] = { 0.0f, 0.0f, forwardSign, 0.0f };
        rows[20] = rows[0];
        rows[21] = rows[1];
        rows[22] = rows[2];
        // Half-depth samples must lie beyond the near-geometry exclusion.
        constexpr float nearPlane = 32.0f;
        for (std::size_t eye = 0; eye < 2; ++eye) {
            const auto base = 4 + eye * 4;
            rows[base] = { 1.0f, 0.0f, 0.0f, 0.0f };
            rows[base + 1] = { 0.0f, 1.0f, 0.0f, 0.0f };
            rows[base + 2] = { 0.0f, 0.0f, 1.0f, -nearPlane };
            rows[base + 3] = { 0.0f, 0.0f, 1.0f, 0.0f };
            const auto inverseBase = 32 + eye * 4;
            rows[inverseBase] = rows[base];
            rows[inverseBase + 1] = rows[base + 1];
            rows[inverseBase + 2] = { 0.0f, 0.0f, 0.0f, 1.0f };
            rows[inverseBase + 3] = {
                0.0f, 0.0f, -1.0f / nearPlane, 1.0f / nearPlane };
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(rows));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const D3D11_SUBRESOURCE_DATA initial{ rows.data(), 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        requireSucceeded(
            device.CreateBuffer(&description, &initial, &buffer),
            "CreateBuffer(scene constants)");
        return buffer;
    }

    [[nodiscard]] csah::ibl::Float3 readPublishedFaceCenter(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        ID3D11ShaderResourceView* environment,
        UINT face)
    {
        require(environment != nullptr, "published environment is null");
        ComPtr<ID3D11Resource> resource;
        environment->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        require(
            resource && SUCCEEDED(resource.As(&texture)) && texture,
            "published environment is not a texture2D");
        D3D11_TEXTURE2D_DESC sourceDescription{};
        texture->GetDesc(&sourceDescription);
        require(
            face < sourceDescription.ArraySize &&
                sourceDescription.Format == DXGI_FORMAT_R11G11B10_FLOAT,
            "published environment layout changed");

        D3D11_TEXTURE2D_DESC stagingDescription{};
        stagingDescription.Width = 1;
        stagingDescription.Height = 1;
        stagingDescription.MipLevels = 1;
        stagingDescription.ArraySize = 1;
        stagingDescription.Format = sourceDescription.Format;
        stagingDescription.SampleDesc.Count = 1;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        requireSucceeded(
            device.CreateTexture2D(
                &stagingDescription,
                nullptr,
                &staging),
            "CreateTexture2D(face center staging)");
        const auto center = sourceDescription.Width / 2;
        const D3D11_BOX sourceBox{
            center,
            center,
            0,
            center + 1,
            center + 1,
            1,
        };
        context.CopySubresourceRegion(
            staging.Get(),
            0,
            0,
            0,
            0,
            texture.Get(),
            D3D11CalcSubresource(0, face, sourceDescription.MipLevels),
            &sourceBox);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        requireSucceeded(
            context.Map(
                staging.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped),
            "Map(face center staging)");
        require(mapped.pData != nullptr, "face center staging map is null");
        std::uint32_t packed{};
        std::memcpy(&packed, mapped.pData, sizeof(packed));
        context.Unmap(staging.Get(), 0);
        const auto decoded =
            csah::ibl::decodeR11G11B10Float(packed);
        return { decoded.red, decoded.green, decoded.blue };
    }

    void finish(Device& d3d, EnvironmentUpdater& updater, EnvironmentProvider& provider)
    {
        d3d.context->Flush();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            const auto result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::completed) return;
            require(result == EnvironmentUpdateConsumeResult::pending, "GPU history update failed");
            require(std::chrono::steady_clock::now() < deadline, "GPU history update timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void seedCollision(Device& d3d, EnvironmentProvider& provider, bool tied)
    {
        require(provider.beginUpdate(), "could not seed geometry history");
        const auto shape = provider.snapshot();
        const std::array<float, 4> zero{};
        for (UINT mip = 0; mip < shape.mipCount; ++mip) {
            d3d.context->ClearUnorderedAccessViewFloat(provider.writableMip(mip), zero.data());
            d3d.context->ClearUnorderedAccessViewFloat(provider.writableValidityMip(mip), zero.data());
            for (UINT face = 0; face < 6; ++face)
                require(provider.markSubresourceComplete(static_cast<csah::ibl::EnvironmentCubeFace>(face), mip), "seed coverage failed");
        }
        d3d.context->ClearUnorderedAccessViewFloat(provider.writablePosition(), zero.data());
        const auto count = shape.extent * shape.extent;
        std::vector<std::uint32_t> colors(count);
        std::vector<float> validity(count);
        std::vector<Float4> positions(count);
        // Two source texels move onto the same destination. Far blue must not
        // overwrite near red; a depth tie must always choose the smaller index.
        colors[0] = kHalfRed; colors[1] = kHalfBlue;
        validity[0] = validity[1] = 1;
        positions[0] = {16, 0, 64, 1};
        positions[1] = {16, 0, tied ? 64.0F : 128.0F, 1};
        d3d.context->UpdateSubresource(provider.writableTexture(), 0, nullptr, colors.data(), shape.extent * 4, 0);
        d3d.context->UpdateSubresource(provider.writableValidityTexture(), 0, nullptr, validity.data(), shape.extent * 4, 0);
        d3d.context->UpdateSubresource(provider.writablePositionTexture(), 0, nullptr, positions.data(), shape.extent * sizeof(Float4), 0);
        require(provider.publishUpdate({{}, true}), "could not publish seed");
    }

    void run(const std::filesystem::path& root, D3D_DRIVER_TYPE driver)
    {
        const auto bytecode = readFile(
            root / "package" / "Shaders" / "Community" / "IBL" /
            "UpdateEnvironmentCS.dxbc");
        require(
            bytecode.size() >= 20 &&
                std::memcmp(bytecode.data(), "DXBC", 4) == 0,
            "environment update asset is not DXBC");
        const auto filterBytecode = readFile(
            root / "package" / "Shaders" / "Community" / "IBL" /
            "FilterEnvironmentCS.dxbc");
        require(
            filterBytecode.size() >= 20 &&
                std::memcmp(filterBytecode.data(), "DXBC", 4) == 0,
            "environment filter asset is not DXBC");

        auto d3d = createDevice(driver);
        ComPtr<IDXGIDevice> graphics;
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(d3d.device.As(&graphics)) && SUCCEEDED(graphics->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC description{};
            if (SUCCEEDED(adapter->GetDesc(&description)))
                std::wcout << L"GPU regression adapter: " << description.Description << L'\n';
        }
        EnvironmentProvider provider;
        require(
            provider.initialize(d3d.device.Get(), 16),
            "provider initialization failed");
        EnvironmentUpdater updater;
        require(
            updater.initialize(
                d3d.device.Get(),
                bytecode.data(),
                bytecode.size(),
                filterBytecode.data(),
                filterBytecode.size(),
                16),
            "updater initialization failed");

        const auto radiance = createPackedRadiance(
            *d3d.device.Get(),
            kHalfRed,
            kHalfBlue);
        const auto depth = createPackedDepth(*d3d.device.Get());
        const auto constants = createSceneConstants(*d3d.device.Get());
        require(
            updater.dispatchUpdate(
                d3d.context.Get(),
                provider,
                radiance.Get(),
                depth.Get(),
                constants.Get(),
                false),
            "environment update dispatch failed");
        const auto providerAfterDispatch = provider.snapshot();
        require(
            providerAfterDispatch.state == EnvironmentProviderState::updating,
            "private generation was not retained pending validation");
        require(
            providerAfterDispatch.publishedGeneration == 0 &&
                provider.publishedEnvironment() == nullptr &&
                provider.publishedValidity() == nullptr,
            "unvalidated generation escaped into publication");

        d3d.context->Flush();
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        EnvironmentUpdateConsumeResult result{};
        do {
            result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::pending) {
                require(
                    std::chrono::steady_clock::now() < deadline,
                    "timed out waiting for environment validation");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (result == EnvironmentUpdateConsumeResult::pending);
        require(
            result == EnvironmentUpdateConsumeResult::completed,
            "environment validation did not complete");

        const auto summary = updater.snapshot();
        require(!summary.pending, "published update remained pending");
        require(
            summary.dispatches == 1 && summary.publishedUpdates == 1 &&
                summary.completedReadbacks == 1 &&
                summary.failedUpdates == 0,
            "environment update counters changed");
        require(
            summary.sampleCount == 16 * 16 * 6,
            "update did not stage every mip-zero cube face");
        require(
            summary.coveredSamples > 0 && summary.averageValidity > 0.0f,
            "published pair contained no directional validity");
        require(
            summary.nonBlackSamples > 0,
            "projected cube contained no stereo radiance");
        require(
            summary.diffuseSHCoverage > 0.0f &&
                csah::ibl::validDiffuseSH(summary.diffuseSH),
            "validated environment did not produce a bounded diffuse SH fit");
        const auto meanFaceValidity = std::accumulate(
            summary.faceAverageValidity.begin(),
            summary.faceAverageValidity.end(),
            0.0f) /
            summary.faceAverageValidity.size();
        require(
            std::abs(meanFaceValidity - summary.diffuseSHCoverage) < 1.0e-3f,
            "face fallback confidence disagreed with SH solid-angle coverage");
        require(
            summary.peak > 0.49f && summary.peak < 0.51f,
            "stereo merge no longer averages both synthetic eyes");
        require(
            summary.faceAverageLuminance[4] >
                summary.faceAverageLuminance[5] + 0.1f,
            "world-space +Z/-Z cube orientation changed");
        require(
            std::abs(summary.average.x - summary.average.z) < 0.01f,
            "left/right packed eyes were not merged symmetrically");
        require(
            provider.snapshot().state == EnvironmentProviderState::ready &&
            provider.snapshot().publishedGeneration == 1 &&
                provider.publishedEnvironment() != nullptr &&
                provider.publishedValidity() != nullptr &&
                provider.publishedPosition() != nullptr,
            "validated radiance/validity/position generation was not atomically published");
        require(
            updater.consumeUpdate(d3d.context.Get(), provider) ==
                EnvironmentUpdateConsumeResult::idle,
            "completed readback was consumed twice");

        const auto firstCoveredSamples = summary.coveredSamples;
        const auto oppositeConstants = createSceneConstants(
            *d3d.device.Get(),
            -1.0F);
        require(
            updater.dispatchUpdate(
                d3d.context.Get(),
                provider,
                radiance.Get(),
                depth.Get(),
                oppositeConstants.Get(),
                true),
            "history-backed environment update dispatch failed");
        d3d.context->Flush();
        const auto secondDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::pending) {
                require(
                    std::chrono::steady_clock::now() < secondDeadline,
                    "timed out waiting for history validation");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (result == EnvironmentUpdateConsumeResult::pending);
        require(
            result == EnvironmentUpdateConsumeResult::completed,
            "history validation did not complete");

        const auto accumulated = updater.snapshot();
        require(accumulated.historyUsed, "published history was not consumed");
        require(
            accumulated.dispatches == 2 &&
                accumulated.publishedUpdates == 2 &&
                accumulated.completedReadbacks == 2 &&
                accumulated.failedUpdates == 0,
            "history update counters changed");
        require(
            accumulated.coveredSamples > firstCoveredSamples * 3 / 2,
            "opposite view did not accumulate directional coverage");
        require(
            accumulated.faceAverageLuminance[4] > 0.01F &&
                accumulated.faceAverageLuminance[5] > 0.01F,
            "temporal history did not retain both opposite cube faces");
        require(
            accumulated.diffuseSHState ==
                    csah::ibl::DiffuseSHState::usable &&
                accumulated.diffuseSHCoverage > summary.diffuseSHCoverage,
            "temporal coverage did not produce a usable diffuse SH fit");
        require(
            provider.snapshot().publishedGeneration == 2,
            "history-backed pair was not atomically published");

        const auto greenRadiance = createPackedRadiance(
            *d3d.device.Get(),
            kHalfGreen,
            kHalfGreen);
        require(
            updater.dispatchUpdate(
                d3d.context.Get(),
                provider,
                greenRadiance.Get(),
                depth.Get(),
                constants.Get(),
                true),
            "visible-direction stabilization update dispatch failed");
        d3d.context->Flush();
        const auto thirdDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            result = updater.consumeUpdate(d3d.context.Get(), provider);
            if (result == EnvironmentUpdateConsumeResult::pending) {
                require(
                    std::chrono::steady_clock::now() < thirdDeadline,
                    "timed out waiting for visible-direction stabilization");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (result == EnvironmentUpdateConsumeResult::pending);
        require(
            result == EnvironmentUpdateConsumeResult::completed,
            "visible-direction stabilization did not complete");

        const auto stabilized = updater.snapshot();
        require(
            stabilized.dispatches == 3 &&
                stabilized.publishedUpdates == 3 &&
                stabilized.completedReadbacks == 3 &&
                stabilized.failedUpdates == 0,
            "visible-direction stabilization counters changed");
        require(
            stabilized.average.y > 0.01F,
            "new visible radiance did not enter temporal history");
        const auto stabilizedPositiveZ = readPublishedFaceCenter(
            *d3d.device.Get(),
            *d3d.context.Get(),
            provider.publishedEnvironment(),
            4);
        require(
            stabilizedPositiveZ.x > 0.32F &&
                stabilizedPositiveZ.x < 0.47F &&
                stabilizedPositiveZ.y > 0.12F &&
                stabilizedPositiveZ.y < 0.3F &&
                stabilizedPositiveZ.z > 0.32F &&
                stabilizedPositiveZ.z < 0.47F,
            "visible direction snapped to the latest capture instead of blending its prior radiance: (" +
                std::to_string(stabilizedPositiveZ.x) + ", " +
                std::to_string(stabilizedPositiveZ.y) + ", " +
                std::to_string(stabilizedPositiveZ.z) + ")");
        require(
            provider.snapshot().publishedGeneration == 3,
            "stabilized radiance/validity pair was not atomically published");

        const auto skyDepth = createPackedDepth(*d3d.device.Get(), 0x00FFFFFFU);
        const auto emptyRadiance = createPackedRadiance(*d3d.device.Get(), 0, 0);
        const auto translated = createSceneConstants(*d3d.device.Get(), 1, 16);
        for (bool tied : {false, true}) {
            seedCollision(d3d, provider, tied);
            require(updater.dispatchUpdate(d3d.context.Get(), provider,
                emptyRadiance.Get(), skyDepth.Get(), translated.Get(), true), "collision reprojection failed");
            finish(d3d, updater, provider);
            const auto center = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
            require(center.x > 0.9F && center.z < 0.01F,
                tied ? "depth tie did not choose deterministic owner" : "far surface overwrote nearest surface");
        }

        // A separately retained sky survives temporary geometry and fills a
        // disocclusion after movement. A zero unresolved sky must not erase it.
        const auto redRadiance = createPackedRadiance(*d3d.device.Get(), kHalfRed, kHalfRed);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, redRadiance.Get(), skyDepth.Get(), constants.Get(), false), "sky capture failed");
        finish(d3d, updater, provider);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, greenRadiance.Get(), depth.Get(), constants.Get(), true), "foreground capture failed");
        finish(d3d, updater, provider);
        const auto moved = createSceneConstants(*d3d.device.Get(), 1, 1000);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, emptyRadiance.Get(), skyDepth.Get(), moved.Get(), true), "sky disocclusion failed");
        finish(d3d, updater, provider);
        const auto retainedSky = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
        require(retainedSky.x > 0.9F && retainedSky.y < 0.01F, "geometry destroyed separate sky history");
        // No observations while turning away must not age out valid sky.
        for (int frame = 0; frame < 12; ++frame) {
            require(updater.dispatchUpdate(d3d.context.Get(), provider, emptyRadiance.Get(), skyDepth.Get(), moved.Get(), true), "unseen history update failed");
            finish(d3d, updater, provider);
        }
        const auto persistentSky = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
        require(persistentSky.x == retainedSky.x, "unseen sky history decayed");

        constexpr std::uint32_t infinityPixel = (31U << 6) | ((31U << 6) << 11) | ((31U << 5) << 22);
        const auto unwritten = createPackedRadiance(*d3d.device.Get(), infinityPixel, infinityPixel);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, unwritten.Get(), depth.Get(), moved.Get(), true), "unwritten-mask dispatch failed");
        finish(d3d, updater, provider);
        const auto masked = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
        require(masked.x == persistentSky.x, "unwritten pixels contaminated history");
        require(updater.dispatchUpdate(d3d.context.Get(), provider, emptyRadiance.Get(), depth.Get(), moved.Get(), true), "real black-surface dispatch failed");
        finish(d3d, updater, provider);
        const auto darkSurface = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
        require(darkSurface.x == 0 && updater.snapshot().coveredSamples > 0, "real black geometry was discarded");

        const auto teleported = createSceneConstants(*d3d.device.Get(), 1, 5000);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, greenRadiance.Get(), skyDepth.Get(), teleported.Get(), true), "teleport update failed");
        finish(d3d, updater, provider);
        const auto resetSky = readPublishedFaceCenter(*d3d.device.Get(), *d3d.context.Get(), provider.publishedEnvironment(), 4);
        require(resetSky.y > 0.9F && resetSky.x < 0.01F, "teleport retained the old location's sky");
        const auto emptyLocation = createSceneConstants(*d3d.device.Get(), 1, 9000);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, emptyRadiance.Get(), skyDepth.Get(), emptyLocation.Get(), true), "empty teleport dispatch failed");
        finish(d3d, updater, provider);
        require(updater.snapshot().coveredSamples == 0, "empty new location retained old coverage");

        // Exercise the actual production scratch allocation and ClearRenderTargetView,
        // not a hand-encoded infinity fixture. Packed HDR clear conversion caused
        // the observed ~65000-radiance exterior whiteout.
        csah::ibl::ReflectionFreeCaptureResources capture;
        require(capture.initialize(d3d.device.Get()), "capture resources unavailable");
        D3D11_TEXTURE2D_DESC nativeDescription{};
        nativeDescription.Width = 32;
        nativeDescription.Height = 16;
        nativeDescription.MipLevels = nativeDescription.ArraySize = nativeDescription.SampleDesc.Count = 1;
        nativeDescription.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        nativeDescription.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> nativeTarget;
        ComPtr<ID3D11RenderTargetView> nativeView;
        requireSucceeded(d3d.device->CreateTexture2D(&nativeDescription, nullptr, &nativeTarget), "native target allocation");
        requireSucceeded(d3d.device->CreateRenderTargetView(nativeTarget.Get(), nullptr, &nativeView), "native view allocation");
        require(capture.prepareScratch(nativeView.Get()), "signed scratch allocation failed");
        require(capture.scratchDescription().Format == DXGI_FORMAT_R16G16B16A16_FLOAT,
            "HDR capture used an unsigned scratch format");
        require(capture.scratchMatches(nativeView.Get()), "signed scratch is not compatible with the native draw");
        capture.clearScratch(d3d.context.Get());
        require(updater.dispatchUpdate(d3d.context.Get(), provider, capture.scratchShaderResource(), depth.Get(), constants.Get(), false), "cleared capture dispatch failed");
        finish(d3d, updater, provider);
        require(updater.snapshot().coveredSamples == 0 && updater.snapshot().peak == 0,
            "actual target clear was admitted as light");

        std::array<std::array<std::uint16_t, 4>, 32 * 16> partial{};
        for (UINT y = 0; y < 16; ++y) for (UINT x = 0; x < 32; ++x) {
            // Alternate small observed islands with unobserved pixels. Edge
            // interpolation must not turn a missing -1 pixel into valid light.
            const std::uint16_t value = (x % 16 >= 5 && x % 16 <= 11 && y >= 4 && y <= 12) ? 0x3C00 : 0xBC00;
            partial[y * 32 + x] = {value, value, value, 0};
        }
        d3d.context->UpdateSubresource(capture.scratchTexture(), 0, nullptr, partial.data(), 32 * 8, 0);
        require(updater.dispatchUpdate(d3d.context.Get(), provider, capture.scratchShaderResource(), depth.Get(), constants.Get(), false), "partial capture dispatch failed");
        finish(d3d, updater, provider);
        const auto partialSummary = updater.snapshot();
        require(partialSummary.coveredSamples > 0 && partialSummary.peak < 1.02F,
            "partial capture failed or amplified radiance");
        require(std::abs(partialSummary.average.x * partialSummary.sampleCount - partialSummary.coveredSamples) < 0.25F,
            "unwritten texels leaked into valid bilinear samples");

        constexpr std::array<float, 4> blackClear{};
        d3d.context->ClearRenderTargetView(capture.scratchRenderTarget(), blackClear.data());
        require(updater.dispatchUpdate(d3d.context.Get(), provider, capture.scratchShaderResource(), depth.Get(), constants.Get(), false), "real black clear dispatch failed");
        finish(d3d, updater, provider);
        require(updater.snapshot().coveredSamples > 0 && updater.snapshot().peak == 0,
            "legitimate black surfaces were treated as missing");
        std::cout << "signed render-target clear, partial coverage, bilinear validity and true black radiance passed\n";
        std::cout << "nearest-depth reprojection, owner ties, sky disocclusion, persistent unseen history and teleport reset passed\n";

    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2 && argumentCount != 3) {
        std::cerr << "Usage: IblEnvironmentUpdaterTests <repo-root> [--hardware]\n";
        return EXIT_FAILURE;
    }
    try {
        const bool hardware = argumentCount == 3 && std::string(arguments[2]) == "--hardware";
        run(std::filesystem::absolute(arguments[1]), hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP);
        std::cout <<
            "FO4VR stereo environment updater verified on D3D11\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "IBL environment updater tests failed: " <<
            exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
