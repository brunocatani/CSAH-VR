#include "render/Fo4VrDepth.h"
#include "render/WorldCameraCapture.h"
#include "Features/dlaa/DlaaCameraValidation.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::render::CameraMatrix;
    using csah::render::CameraVector;
    using csah::render::DepthPreparationConstants;

    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    CameraMatrix projection(float nearPlane, float farPlane, bool inverted = false)
    {
        const auto a = inverted ? -nearPlane / (farPlane - nearPlane) : farPlane / (farPlane - nearPlane);
        const auto b = inverted ? nearPlane * farPlane / (farPlane - nearPlane) : -nearPlane * farPlane / (farPlane - nearPlane);
        return {CameraVector{1.3F, 0, -0.07F, 0}, CameraVector{0, 0.9F, 0.03F, 0},
            CameraVector{0, 0, a, b}, CameraVector{0, 0, 1, 0}};
    }
    CameraMatrix inverseProjection(const CameraMatrix& p)
    {
        return {CameraVector{1 / p[0].x, 0, 0, -p[0].z / p[0].x},
            CameraVector{0, 1 / p[1].y, 0, -p[1].z / p[1].y},
            CameraVector{0, 0, 0, 1}, CameraVector{0, 0, 1 / p[2].w, -p[2].z / p[2].w}};
    }

    void temporalCameraTests(ID3D11Device* device)
    {
        csah::render::Fo4VrFrameBuffer validFrame{};
        for (UINT eye = 0; eye < 2; ++eye) {
            validFrame.projection[eye] = projection(1, 50000);
            validFrame.inverseProjection[eye] = inverseProjection(validFrame.projection[eye]);
            validFrame.inverseFirstPersonProjection[eye] = inverseProjection(projection(0.1F, 1000));
            validFrame.viewProjectionUnjittered[eye] = validFrame.projection[eye];
            validFrame.previousViewProjectionUnjittered[eye] = validFrame.projection[eye];
            // Native origin writes fill xyz only. Padding must not reject a camera.
            validFrame.currentEyePosition[eye].w = std::numeric_limits<float>::quiet_NaN();
        }
        require(csah::dlaa::inspectCamera(validFrame).valid(), "valid native perspective rejected by camera diagnostics");
        auto malformed = validFrame;
        malformed.inverseFirstPersonProjection[1][2].x = std::numeric_limits<float>::quiet_NaN();
        auto report = csah::dlaa::inspectCamera(malformed);
        require(report.eyeFailures[0] == 0 && report.eyeFailures[1] == csah::dlaa::firstPersonInverseNonfinite,
            "first-person inverse failure was not distinguished from world projection");
        malformed = validFrame;
        malformed.projection[0][3].z = 0;
        report = csah::dlaa::inspectCamera(malformed);
        require(report.eyeFailures[0] == csah::dlaa::projectionPlanesInvalid && report.eyeFailures[1] == 0,
            "finite but invalid perspective was not distinguished from nonfinite data");
        // Independently project the four rays through an off-center crop.
        // Both asymmetric eyes must reach exactly the new clip boundaries,
        // while depth and homogeneous w remain unchanged.
        for (UINT eye = 0; eye < 2; ++eye) {
            auto p = projection(1, 50000);
            p[0].z = eye ? 0.11F : -0.07F;
            constexpr float left = 0.2F, top = 0.15F, width = 0.55F, height = 0.65F;
            const auto cropped = csah::render::cropClipMatrix(p, left, top, width, height);
            for (UINT corner = 0; corner < 4; ++corner) {
                const auto u = left + (corner & 1u) * width;
                const auto v = top + (corner >> 1u) * height;
                constexpr float z = 40;
                const auto x = ((2 * u - 1) - p[0].z) * z / p[0].x;
                const auto y = ((1 - 2 * v) - p[1].z) * z / p[1].y;
                const auto dot = [&](const CameraVector& row) { return row.x * x + row.y * y + row.z * z + row.w; };
                const auto clipW = dot(cropped[3]);
                require(std::abs(dot(cropped[0]) / clipW - ((corner & 1u) ? 1.0F : -1.0F)) < 1e-5F &&
                    std::abs(dot(cropped[1]) / clipW - ((corner >> 1u) ? -1.0F : 1.0F)) < 1e-5F,
                    "cropped asymmetric camera rays missed clip corners");
                require(dot(cropped[2]) == dot(p[2]) && clipW == dot(p[3]), "crop changed depth convention");
            }
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(csah::render::Fo4VrFrameBuffer);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> world, ui;
        require(SUCCEEDED(device->CreateBuffer(&description, nullptr, &world)) &&
            SUCCEEDED(device->CreateBuffer(&description, nullptr, &ui)), "camera identity buffers failed");
        csah::render::WorldCameraCapture capture;
        csah::render::Fo4VrFrameBuffer candidate{}, published{};
        candidate.currentEyePosition[0].x = 123;
        capture.observe(world.Get(), candidate, 10);
        require(!capture.confirm(ui.Get(), 10, published) && !capture.confirm(world.Get(), 11, published),
            "unrelated or stale camera was published");
        require(capture.confirm(world.Get(), 10, published) && published.currentEyePosition[0].x == 123,
            "world draw failed to publish its current camera");
        candidate.currentEyePosition[0].x = 456;
        capture.observe(ui.Get(), candidate, 10);
        require(published.currentEyePosition[0].x == 123 && !capture.confirm(world.Get(), 10, published),
            "later UI camera altered the world snapshot");
        capture.observe(world.Get(), candidate, 11);
        require(capture.confirm(world.Get(), 11, published) && published.currentEyePosition[0].x == 456,
            "next world frame did not replace its camera");
        capture.observe(world.Get(), malformed, 12);
        require(capture.confirm(world.Get(), 12, published) && !csah::dlaa::inspectCamera(published).valid(),
            "raw rejected upload was unavailable for world-boundary diagnostics");
        capture.reset();
        require(!capture.confirm(world.Get(), 11, published), "session reset retained a camera candidate");
    }

    void benchmark(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ComputeShader* shader)
    {
        // Match the user's observed packed stereo extent. Compare the old
        // depth-copy traffic with the replacement normalization dispatch.
        constexpr UINT width = 2688, height = 2880;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width * 2; td.Height = height; td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> source;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &source)), "benchmark source failed");
        D3D11_DEPTH_STENCIL_VIEW_DESC ds{};
        ds.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; ds.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11DepthStencilView> depth;
        require(SUCCEEDED(device->CreateDepthStencilView(source.Get(), &ds, &depth)), "benchmark DSV failed");
        context->ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.8F, 0);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> input;
        require(SUCCEEDED(device->CreateShaderResourceView(source.Get(), &sv, &input)), "benchmark SRV failed");
        std::array<ComPtr<ID3D11Texture2D>, 2> copies, normalized;
        std::array<ComPtr<ID3D11UnorderedAccessView>, 2> outputs;
        td.Width = width; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        for (auto& copy : copies) require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &copy)), "benchmark copy target failed");
        td.Format = DXGI_FORMAT_R32_FLOAT; td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        for (UINT eye = 0; eye < 2; ++eye)
            require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &normalized[eye])) &&
                SUCCEEDED(device->CreateUnorderedAccessView(normalized[eye].Get(), nullptr, &outputs[eye])), "benchmark output failed");
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(DepthPreparationConstants); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;
        require(SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants)), "benchmark constants failed");
        ComPtr<ID3D11Query> disjoint, start, end;
        D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        require(SUCCEEDED(device->CreateQuery(&q, &disjoint)), "benchmark disjoint failed");
        q.Query = D3D11_QUERY_TIMESTAMP;
        require(SUCCEEDED(device->CreateQuery(&q, &start)) && SUCCEEDED(device->CreateQuery(&q, &end)), "benchmark timestamps failed");
        auto* srv = input.Get(); auto* buffer = constants.Get();
        context->CSSetShader(shader, nullptr, 0);
        context->CSSetShaderResources(0, 1, &srv);
        context->CSSetConstantBuffers(0, 1, &buffer);
        std::array<std::vector<double>, 2> times;
        ComPtr<ID3D11InfoQueue> messages;
        require(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&messages))), "D3D11 debug messages unavailable");
        bool legacyCopyRejected{};
        for (UINT round = 0; round < 24; ++round) for (UINT order = 0; order < 2; ++order) {
            const auto mode = (round + order) % 2;
            if (mode == 0 && legacyCopyRejected) continue;
            messages->ClearStoredMessages();
            context->Begin(disjoint.Get()); context->End(start.Get());
            for (UINT eye = 0; eye < 2; ++eye) {
                if (mode == 0) {
                    const D3D11_BOX box{eye * width, 0, 0, (eye + 1) * width, height, 1};
                    context->CopySubresourceRegion(copies[eye].Get(), 0, 0, 0, 0, source.Get(), 0, &box);
                } else {
                    const DepthPreparationConstants cb{width, height, eye * width, 0,
                        {1.0F / width, 1.0F / height, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, 1, {}};
                    context->UpdateSubresource(constants.Get(), 0, nullptr, &cb, 0, 0);
                    auto* uav = outputs[eye].Get();
                    context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
                    context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                    uav = nullptr; context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
                }
            }
            context->End(end.Get()); context->End(disjoint.Get()); context->Flush();
            for (UINT64 index = 0; index < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
                SIZE_T bytes{};
                require(SUCCEEDED(messages->GetMessage(index, nullptr, &bytes)), "debug message size failed");
                std::vector<std::byte> storage(bytes);
                auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                require(SUCCEEDED(messages->GetMessage(index, message, &bytes)), "debug message read failed");
                if (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR || message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION) {
                    if (mode != 0) throw std::runtime_error(message->pDescription);
                    if (!legacyCopyRejected) std::cout << "Legacy native depth copy rejected: " << message->pDescription << '\n';
                    legacyCopyRejected = true;
                }
            }
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
            auto result = S_FALSE;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while ((result = context->GetData(disjoint.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
                require(std::chrono::steady_clock::now() < deadline, "depth benchmark timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            UINT64 beginTicks{}, endTicks{};
            require(SUCCEEDED(result) && context->GetData(start.Get(), &beginTicks, sizeof(beginTicks), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                context->GetData(end.Get(), &endTicks, sizeof(endTicks), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK, "depth benchmark query failed");
            if (!data.Disjoint && data.Frequency != 0 && round >= 4)
                times[mode].push_back(double(endTicks - beginTicks) * 1000 / data.Frequency);
        }
        for (UINT mode = 0; mode < 2; ++mode) {
            if (mode == 0 && legacyCopyRejected) continue;
            require(!times[mode].empty(), "depth benchmark has no valid timestamps");
            std::sort(times[mode].begin(), times[mode].end());
            std::cout << "5376x2880 stereo " << (mode == 0 ? "native depth copy" : "canonical depth preparation") <<
                " median GPU=" << times[mode][times[mode].size() / 2] << " ms\n";
        }
        ds.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
        ComPtr<ID3D11DepthStencilView> readOnlyDepth;
        require(SUCCEEDED(device->CreateDepthStencilView(source.Get(), &ds, &readOnlyDepth)), "read-only depth view failed");
        context->OMSetRenderTargets(0, nullptr, readOnlyDepth.Get());
        DepthPreparationConstants raw{};
        raw.width = 16; raw.height = 1; raw.rawProbe = 1;
        context->UpdateSubresource(constants.Get(), 0, nullptr, &raw, 0, 0);
        auto* rawOutput = outputs[0].Get();
        context->CSSetUnorderedAccessViews(0, 1, &rawOutput, nullptr);
        context->Dispatch(2, 1, 1);
        rawOutput = nullptr; context->CSSetUnorderedAccessViews(0, 1, &rawOutput, nullptr);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        td.Width = td.Height = 1; td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &readback)), "probe result allocation failed");
        const D3D11_BOX pixel{0, 0, 0, 1, 1, 1};
        context->CopySubresourceRegion(readback.Get(), 0, 0, 0, 0, normalized[0].Get(), 0, &pixel);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "probe result read failed");
        require(std::abs(*static_cast<const float*>(mapped.pData) - 0.8F) < 1e-6F,
            "raw probe failed with the native read-only DSV still bound");
        context->Unmap(readback.Get(), 0);
        context->ClearState();
    }

    void run(const std::filesystem::path& root, bool hardware)
    {
        std::array<CameraVector, 85> rows{};
        rows[59].x = 123; rows[60].x = 456; rows[61].x = 789; rows[62].x = 1011;
        for (std::size_t i = 77; i < rows.size(); ++i) rows[i].x = std::numeric_limits<float>::quiet_NaN();
        csah::render::Fo4VrFrameBuffer camera{};
        std::memcpy(&camera, rows.data(), sizeof(camera));
        require(camera.currentEyePosition[0].x == 123 && camera.currentEyePosition[1].x == 456 &&
            camera.previousEyePosition[0].x == 789 && camera.previousEyePosition[1].x == 1011,
            "native current/previous eye order changed");
        for (const auto inverted : {false, true}) {
            const auto planes = csah::render::projectionPlanes(projection(1, 50000, inverted));
            require(planes.valid && planes.inverted == inverted && std::abs(planes.nearPlane - 1) < 1e-5F &&
                std::abs(planes.farPlane - 50000) < 100, "native projection endpoints decoded incorrectly");
        }
        require(!csah::render::projectionPlanes({}).valid, "zero projection accepted");
        auto invalidProjection = projection(1, 1000);
        invalidProjection[2].x = std::numeric_limits<float>::quiet_NaN();
        require(!csah::render::projectionPlanes(invalidProjection).valid, "nonfinite projection accepted");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
            nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "D3D11 initialization failed");
        temporalCameraTests(device.Get());
        ComPtr<ID3DBlob> bytecode, errors;
        const auto shaderPath = root / "package/Shaders/Community/Upscaling/PrepareDepthCS.hlsl";
        const auto compiled = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0, &bytecode, &errors);
        if (FAILED(compiled) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        require(SUCCEEDED(compiled), "depth shader compilation failed");
        ComPtr<ID3D11ComputeShader> shader;
        require(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)), "depth shader creation failed");
        constexpr UINT eyeWidth = 14, sourceHeight = 11, width = 11, height = 7;
        constexpr std::array<float, 8> pattern{0, 0.005F, 0.0098F, 0.0101F, 0.2F, 0.75F, 0.999F, 1};
        std::vector<std::uint32_t> packed(eyeWidth * 2 * sourceHeight);
        for (UINT y = 0; y < sourceHeight; ++y) for (UINT x = 0; x < eyeWidth * 2; ++x)
            packed[y * eyeWidth * 2 + x] = static_cast<std::uint32_t>(pattern[(x + 3 * y) % pattern.size()] * 16777215.0F);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = eyeWidth * 2; td.Height = sourceHeight; td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA sourceData{packed.data(), eyeWidth * 2 * 4, 0};
        ComPtr<ID3D11Texture2D> source;
        ComPtr<ID3D11ShaderResourceView> sourceView;
        require(SUCCEEDED(device->CreateTexture2D(&td, &sourceData, &source)), "native depth texture failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        require(SUCCEEDED(device->CreateShaderResourceView(source.Get(), &sv, &sourceView)), "native depth view failed");
        td.Width = width; td.Height = height; td.Format = DXGI_FORMAT_R32_FLOAT;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output, staging;
        ComPtr<ID3D11UnorderedAccessView> outputView;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &output)) &&
            SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &outputView)), "canonical depth output failed");
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &staging)), "depth readback failed");
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(DepthPreparationConstants); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;
        require(SUCCEEDED(device->CreateBuffer(&bd, nullptr, &constants)), "depth constants failed");

        for (UINT eye = 0; eye < 2; ++eye) for (const auto inverted : {false, true}) {
            const auto world = projection(1.0F + eye, 1000.0F + eye * 500, inverted);
            const auto firstPerson = projection(4.0F + eye * 4, 2000);
            const auto inverse = inverseProjection(firstPerson);
            const DepthPreparationConstants cb{width, height, eye * eyeWidth + 1, 2,
                {1.0F / eyeWidth, 1.0F / sourceHeight, 1.0F / eyeWidth, 2.0F / sourceHeight},
                csah::render::rowThroughMatrix(world[2], inverse), csah::render::rowThroughMatrix(world[3], inverse),
                inverted ? 0.0F : 1.0F, {}};
            context->UpdateSubresource(constants.Get(), 0, nullptr, &cb, 0, 0);
            auto* input = sourceView.Get(); auto* destination = outputView.Get(); auto* buffer = constants.Get();
            context->CSSetShader(shader.Get(), nullptr, 0);
            context->CSSetConstantBuffers(0, 1, &buffer);
            context->CSSetShaderResources(0, 1, &input);
            context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
            context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
            destination = nullptr;
            context->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
            context->CopyResource(staging.Get(), output.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "depth result map failed");
            for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
                const float depth = static_cast<float>(packed[(y + cb.sourceTop) * eyeWidth * 2 + x + cb.sourceLeft]) / 16777215.0F;
                double expected = cb.farDepth;
                if (depth > 0) {
                    if (depth <= 0.01F) {
                        const double z = firstPerson[2].w / (double(depth) * 100 - firstPerson[2].z);
                        expected = world[2].z + world[2].w / z;
                    } else expected = double(depth) * 1.01 - 0.01;
                }
                expected = std::clamp(expected, 0.0, 1.0);
                const auto value = reinterpret_cast<const float*>(static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch)[x];
                require(std::isfinite(value) && std::abs(value - expected) < 3e-6,
                    "canonical depth differs from independent world/first-person reconstruction");
            }
            context->Unmap(staging.Get(), 0);
        }
        // Qualification must gather real D24 values, not attempt cropped
        // copies of a depth-stencil resource into a differently sized texture.
        td.Width = 16; td.Height = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS; td.CPUAccessFlags = 0;
        outputView.Reset(); output.Reset(); staging.Reset();
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &output)) &&
            SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &outputView)), "raw probe output failed");
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &staging)), "raw probe staging failed");
        DepthPreparationConstants raw{};
        raw.width = 16; raw.height = 1; raw.rawProbe = 1;
        context->UpdateSubresource(constants.Get(), 0, nullptr, &raw, 0, 0);
        auto* probeOutput = outputView.Get();
        context->CSSetUnorderedAccessViews(0, 1, &probeOutput, nullptr);
        context->Dispatch(2, 1, 1);
        probeOutput = nullptr; context->CSSetUnorderedAccessViews(0, 1, &probeOutput, nullptr);
        context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE rawMapped{};
        require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &rawMapped)), "raw probe map failed");
        for (UINT sample = 0; sample < 16; ++sample) {
            const auto local = sample % 8;
            const auto x = (sample / 8) * eyeWidth + ((2 * (local % 4) + 1) * eyeWidth) / 8;
            const auto y = ((2 * (local / 4) + 1) * sourceHeight) / 4;
            const auto expected = static_cast<float>(packed[y * eyeWidth * 2 + x]) / 16777215.0F;
            require(std::abs(static_cast<const float*>(rawMapped.pData)[sample] - expected) < 1e-6F,
                "raw depth probe selected the wrong stereo sample");
        }
        context->Unmap(staging.Get(), 0);
        context->ClearState();
        if (hardware) benchmark(device.Get(), context.Get(), shader.Get());
        std::cout << "Native camera layout, projection-derived planes, stereo crops and world/first-person GPU depth passed on " <<
            (hardware ? "hardware" : "WARP") << '\n';
    }
}

int main(int count, char** arguments)
{
    if (count < 2 || count > 3) return 2;
    try { run(std::filesystem::absolute(arguments[1]), count == 3); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
