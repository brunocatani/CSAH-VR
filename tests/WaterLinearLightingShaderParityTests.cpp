#include "Features/linear_lighting/LinearLightingSettings.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::linear_lighting::FrameData;
    using csah::linear_lighting::Settings;
    using csah::linear_lighting::makeFrameData;

    using Pixel = std::array<float, 4>;

    constexpr float kTolerance = 8.0e-5F;
    constexpr Pixel kShallowColor{ 0.42F, -0.63F, 0.31F, 0.77F };
    constexpr Pixel kDeepColor{ 0.18F, 0.36F, -0.72F, 0.54F };
    constexpr Pixel kSunColor{ 0.74F, 0.28F, -0.46F, 1.35F };
    constexpr Pixel kFogNearColor{ 0.31F, -0.52F, 0.68F, 0.23F };
    constexpr Pixel kFogFarColor{ 0.77F, 0.44F, -0.19F, 0.81F };
    constexpr Pixel kPointLightColor{ 0.61F, -0.37F, 0.24F, 0.93F };
    constexpr Pixel kFogAlphaSource{ 0.41F, 0.0F, 0.0F, 0.0F };
    constexpr std::array<const char*, 31> kContracts{
        "WaterColor_00000000",
        "WaterColor_0000001C",
        "WaterColor_0000003C",
        "WaterColor_0000005E",
        "WaterColor_0000007E",
        "WaterColor_0000009F",
        "WaterColor_000000BF",
        "WaterColor_0000021C",
        "WaterColor_0000023C",
        "WaterColor_0000025E",
        "WaterColor_0000027E",
        "WaterColor_0000029F",
        "WaterColor_000002BF",
        "WaterColor_00001002",
        "WaterColor_0000105E",
        "WaterColor_0000109F",
        "WaterColor_0000121E",
        "WaterColor_0000125E",
        "WaterColor_00001A8F",
        "WaterColor_00002002",
        "WaterColor_00003002",
        "WaterColor_00004002",
        "WaterColor_00006002",
        "WaterColor_00008002",
        "WaterColor_00009000",
        "WaterColor_0000A002",
        "WaterColor_0000C002",
        "WaterColor_00010000",
        "WaterColor_00010204",
        "WaterColor_00018010",
        "WaterColor_00020204",
    };

    struct alignas(16) WaterPerFrame
    {
        Pixel padding0{};
        Pixel padding1{};
        Pixel sun{};
    };

    struct alignas(16) WaterPerMaterial
    {
        Pixel shallow{};
        Pixel deep{};
        std::array<Pixel, 4> padding{};
        Pixel fogNear{};
        Pixel fogFar{};
    };

    struct alignas(16) WaterPerLights
    {
        std::array<Pixel, 20> padding{};
        Pixel pointLight{};
    };

    struct RenderTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11Texture2D> staging;
    };

    void require(HRESULT result, const char* operation)
    {
        if (FAILED(result)) {
            throw std::runtime_error(
                std::string(operation) + " failed with HRESULT " +
                std::to_string(static_cast<std::uint32_t>(result)));
        }
    }

    std::vector<std::byte> readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("could not open " + path.string());
        }
        const auto size = stream.tellg();
        if (size <= 0) {
            throw std::runtime_error("empty shader " + path.string());
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("could not read " + path.string());
        }
        return bytes;
    }

    template <class T>
    ComPtr<ID3D11Buffer> createConstantBuffer(
        ID3D11Device* device,
        const T& value)
    {
        static_assert(sizeof(T) % 16 == 0);
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(sizeof(T));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{ &value, 0, 0 };
        ComPtr<ID3D11Buffer> buffer;
        require(
            device->CreateBuffer(&description, &initial, buffer.GetAddressOf()),
            "CreateBuffer");
        return buffer;
    }

    ComPtr<ID3D11VertexShader> createVertexShader(ID3D11Device* device)
    {
        constexpr char source[] = R"(
float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION0
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    return float4(positions[vertexId], 0.5, 1.0);
}
)";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto result = D3DCompile(
            source,
            sizeof(source) - 1,
            "WaterLinearLightingParityVS",
            nullptr,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(result)) {
            const std::string detail = errors ?
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()) :
                "no compiler diagnostics";
            throw std::runtime_error("vertex shader compile failed: " + detail);
        }
        ComPtr<ID3D11VertexShader> shader;
        require(
            device->CreateVertexShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                shader.GetAddressOf()),
            "CreateVertexShader");
        return shader;
    }

    ComPtr<ID3D11PixelShader> compileTransformShader(
        ID3D11Device* device,
        const std::filesystem::path& path,
        const char* entryPoint)
    {
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto result = D3DCompileFromFile(
            path.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 |
                D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(result)) {
            const std::string detail = errors ?
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()) :
                "no compiler diagnostics";
            throw std::runtime_error(
                std::string("Water transform shader compile failed for ") +
                entryPoint + ": " + detail);
        }
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                shader.GetAddressOf()),
            "CreatePixelShader(transform)");
        return shader;
    }

    ComPtr<ID3D11PixelShader> createPixelShader(
        ID3D11Device* device,
        const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytes.data(), bytes.size(), nullptr, shader.GetAddressOf()),
            "CreatePixelShader(packaged Water)");
        return shader;
    }

    RenderTarget createRenderTarget(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;

        RenderTarget target;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.texture.GetAddressOf()),
            "CreateTexture2D(render target)");
        require(
            device->CreateRenderTargetView(
                target.texture.Get(), nullptr, target.view.GetAddressOf()),
            "CreateRenderTargetView");
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        require(
            device->CreateTexture2D(
                &description, nullptr, target.staging.GetAddressOf()),
            "CreateTexture2D(staging)");
        return target;
    }

    Pixel readTarget(ID3D11DeviceContext* context, RenderTarget& target)
    {
        context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(
            context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(render target)");
        Pixel result{};
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(target.staging.Get(), 0);
        return result;
    }

    std::array<Pixel, 2> renderTransform(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* waterFrameBuffer,
        ID3D11Buffer* materialBuffer,
        ID3D11Buffer* lightBuffer,
        ID3D11Buffer* fogAlphaBuffer,
        ID3D11Buffer* frameBuffer)
    {
        std::array<RenderTarget, 2> targets{
            createRenderTarget(device), createRenderTarget(device)
        };
        const float clear[4]{};
        std::array<ID3D11RenderTargetView*, 2> views{
            targets[0].view.Get(), targets[1].view.Get()
        };
        for (auto* view : views) {
            context->ClearRenderTargetView(view, clear);
        }
        context->OMSetRenderTargets(
            static_cast<UINT>(views.size()), views.data(), nullptr);
        const D3D11_VIEWPORT viewport{ 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &waterFrameBuffer);
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        context->PSSetConstantBuffers(2, 1, &lightBuffer);
        context->PSSetConstantBuffers(4, 1, &fogAlphaBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->Draw(3, 0);

        std::array<Pixel, 2> result{
            readTarget(context, targets[0]), readTarget(context, targets[1])
        };
        const std::array<ID3D11RenderTargetView*, 2> nullViews{};
        context->OMSetRenderTargets(
            static_cast<UINT>(nullViews.size()), nullViews.data(), nullptr);
        return result;
    }

    bool nearValue(float actual, float expected)
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <=
                kTolerance * std::max(1.0F, std::abs(expected));
    }

    bool compare(
        const Pixel& actual,
        const Pixel& expected,
        const std::string& label)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel) {
            if (!nearValue(actual[channel], expected[channel])) {
                std::cerr << label << " channel " << channel << " expected "
                          << expected[channel] << " but got " << actual[channel]
                          << '\n';
                return false;
            }
        }
        return true;
    }

    Pixel expectedEnabled(
        const Pixel& color,
        float gamma,
        float multiplier = 1.0F)
    {
        Pixel result = color;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            result[channel] =
                std::pow(std::abs(color[channel]), gamma) * multiplier;
        }
        return result;
    }

    int run(const std::filesystem::path& root)
    {
        D3D_FEATURE_LEVEL featureLevel{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &featureLevel,
                context.GetAddressOf()),
            "D3D11CreateDevice(WARP)");
        if (featureLevel < D3D_FEATURE_LEVEL_11_0) {
            throw std::runtime_error("WARP did not provide feature level 11");
        }

        for (const auto* contract : kContracts) {
            const auto original = createPixelShader(
                device.Get(),
                root / "package/Shaders/Community/VerifiedWaterLinearLighting" /
                    (std::string(contract) + ".dxbc"));
            const auto replacement = createPixelShader(
                device.Get(),
                root / "package/Shaders/Community/WaterLinearLighting" /
                    (std::string(contract) + ".dxbc"));
            if (!original || !replacement) {
                throw std::runtime_error(
                    std::string(contract) + " did not create both shaders");
            }
        }

        const auto vertexShader = createVertexShader(device.Get());
        const auto transformSource = root /
            "package/Shaders/Community/WaterLinearLighting/WaterColorTransformTemplate.hlsl";
        const auto shallowDeepShader = compileTransformShader(
            device.Get(), transformSource, "PSMain");
        const auto sunShader = compileTransformShader(
            device.Get(), transformSource, "PSSunMain");
        const auto fogShader = compileTransformShader(
            device.Get(), transformSource, "PSFogMain");
        const auto pointLightShader = compileTransformShader(
            device.Get(), transformSource, "PSPointMain");
        const auto fogAlphaShader = compileTransformShader(
            device.Get(), transformSource, "PSFogAlphaMain");
        const WaterPerFrame waterFrame{ {}, {}, kSunColor };
        const WaterPerMaterial material{
            kShallowColor,
            kDeepColor,
            {},
            kFogNearColor,
            kFogFarColor,
        };
        const WaterPerLights lights{ {}, kPointLightColor };
        const auto waterFrameBuffer =
            createConstantBuffer(device.Get(), waterFrame);
        const auto materialBuffer = createConstantBuffer(device.Get(), material);
        const auto lightBuffer = createConstantBuffer(device.Get(), lights);
        const auto fogAlphaBuffer =
            createConstantBuffer(device.Get(), kFogAlphaSource);

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        enabledSettings.lightGamma = 1.65F;
        enabledSettings.fogGamma = 1.93F;
        enabledSettings.fogAlphaGamma = 1.47F;
        enabledSettings.waterGamma = 1.75F;
        enabledSettings.directionalLightMultiplier = 1.17F;
        enabledSettings.pointLightMultiplier = 0.83F;
        const FrameData disabledFrame =
            makeFrameData(disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame =
            makeFrameData(enabledSettings, true, false, 1.0F);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);

        const auto renderWith = [&](
                                    ID3D11PixelShader* shader,
                                    ID3D11Buffer* frameBuffer) {
            return renderTransform(
                device.Get(),
                context.Get(),
                vertexShader.Get(),
                shader,
                waterFrameBuffer.Get(),
                materialBuffer.Get(),
                lightBuffer.Get(),
                fogAlphaBuffer.Get(),
                frameBuffer);
        };
        const auto disabledShallowDeep = renderWith(
            shallowDeepShader.Get(), disabledFrameBuffer.Get());
        const auto enabledShallowDeep = renderWith(
            shallowDeepShader.Get(), enabledFrameBuffer.Get());
        const auto disabledSun = renderWith(
            sunShader.Get(), disabledFrameBuffer.Get());
        const auto enabledSun = renderWith(
            sunShader.Get(), enabledFrameBuffer.Get());
        const auto disabledFog = renderWith(
            fogShader.Get(), disabledFrameBuffer.Get());
        const auto enabledFog = renderWith(
            fogShader.Get(), enabledFrameBuffer.Get());
        const auto disabledPointLight = renderWith(
            pointLightShader.Get(), disabledFrameBuffer.Get());
        const auto enabledPointLight = renderWith(
            pointLightShader.Get(), enabledFrameBuffer.Get());
        const auto disabledFogAlpha = renderWith(
            fogAlphaShader.Get(), disabledFrameBuffer.Get());
        const auto enabledFogAlpha = renderWith(
            fogAlphaShader.Get(), enabledFrameBuffer.Get());

        bool passed = true;
        passed &= compare(
            disabledShallowDeep[0], kShallowColor, "disabled shallow parity");
        passed &= compare(
            disabledShallowDeep[1], kDeepColor, "disabled deep parity");
        passed &= compare(
            enabledShallowDeep[0],
            expectedEnabled(kShallowColor, enabledSettings.waterGamma / 2.2F),
            "enabled shallow model");
        passed &= compare(
            enabledShallowDeep[1],
            expectedEnabled(kDeepColor, enabledSettings.waterGamma / 2.2F),
            "enabled deep model");
        passed &= compare(disabledSun[0], kSunColor, "disabled sun parity");
        passed &= compare(
            enabledSun[0],
            expectedEnabled(
                kSunColor,
                enabledSettings.lightGamma / 2.2F,
                enabledSettings.directionalLightMultiplier),
            "enabled sun residual model");
        passed &= compare(
            disabledFog[0], kFogNearColor, "disabled near fog parity");
        passed &= compare(
            disabledFog[1], kFogFarColor, "disabled far fog parity");
        passed &= compare(
            enabledFog[0],
            expectedEnabled(kFogNearColor, enabledSettings.fogGamma / 2.2F),
            "enabled near fog model");
        passed &= compare(
            enabledFog[1],
            expectedEnabled(kFogFarColor, enabledSettings.fogGamma / 2.2F),
            "enabled far fog model");
        passed &= compare(
            disabledPointLight[0],
            kPointLightColor,
            "disabled point-light parity");
        passed &= compare(
            enabledPointLight[0],
            expectedEnabled(
                kPointLightColor,
                enabledSettings.lightGamma / 2.2F,
                enabledSettings.pointLightMultiplier),
            "enabled point-light residual model");
        const Pixel disabledFogAlphaExpected{
            kFogAlphaSource[0],
            kFogAlphaSource[0],
            kFogAlphaSource[0],
            kFogAlphaSource[0],
        };
        const float transformedFogAlpha = std::pow(
            std::abs(kFogAlphaSource[0]), enabledSettings.fogAlphaGamma / 2.2F);
        const Pixel enabledFogAlphaExpected{
            transformedFogAlpha,
            transformedFogAlpha,
            transformedFogAlpha,
            transformedFogAlpha,
        };
        passed &= compare(
            disabledFogAlpha[0],
            disabledFogAlphaExpected,
            "disabled atmospheric-fog coverage parity");
        passed &= compare(
            enabledFogAlpha[0],
            enabledFogAlphaExpected,
            "enabled atmospheric-fog coverage model");
        if (!passed) {
            return 1;
        }
        std::cout <<
            "Water Linear Lighting created all 31 transformed FO4VR shaders and passed shallow/deep, sun, fog RGB, atmospheric-fog coverage, and point-light transform parity.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr <<
            "usage: WaterLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "Water Linear Lighting parity test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
