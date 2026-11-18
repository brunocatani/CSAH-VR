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
    constexpr Pixel kSourcePower{ -0.35F, 0.0F, 0.0F, 1.0F };
    constexpr Pixel kCompositeColor{ 0.8F, 0.45F, 0.2F, 0.0F };

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
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.5, 1.0);
    output.texCoord = float2(0.5, 0.5);
    return output;
}
)";
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto result = D3DCompile(
            source,
            sizeof(source) - 1,
            "VLSCompositeLinearLightingParityVS",
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

    ComPtr<ID3D11PixelShader> createPixelShader(
        ID3D11Device* device,
        const std::filesystem::path& path)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11PixelShader> shader;
        require(
            device->CreatePixelShader(
                bytes.data(), bytes.size(), nullptr, shader.GetAddressOf()),
            "CreatePixelShader");
        return shader;
    }

    ComPtr<ID3D11ShaderResourceView> createTexture(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{ kSourcePower.data(), sizeof(Pixel), 0 };
        ComPtr<ID3D11Texture2D> texture;
        require(
            device->CreateTexture2D(
                &description, &initial, texture.GetAddressOf()),
            "CreateTexture2D(shader resource)");
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            device->CreateShaderResourceView(
                texture.Get(), nullptr, view.GetAddressOf()),
            "CreateShaderResourceView");
        return view;
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

    Pixel render(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* compositeBuffer,
        ID3D11Buffer* frameBuffer,
        ID3D11ShaderResourceView* texture,
        ID3D11SamplerState* sampler)
    {
        auto target = createRenderTarget(device);
        const float clear[4]{};
        context->ClearRenderTargetView(target.view.Get(), clear);
        ID3D11RenderTargetView* renderTarget = target.view.Get();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        const D3D11_VIEWPORT viewport{ 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(2, 1, &compositeBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->PSSetShaderResources(0, 1, &texture);
        context->PSSetSamplers(0, 1, &sampler);
        context->Draw(3, 0);

        context->CopyResource(target.staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(
            context->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "Map(render target)");
        Pixel result{};
        std::memcpy(result.data(), mapped.pData, sizeof(result));
        context->Unmap(target.staging.Get(), 0);

        ID3D11ShaderResourceView* nullTexture{};
        context->PSSetShaderResources(0, 1, &nullTexture);
        ID3D11RenderTargetView* nullTarget{};
        context->OMSetRenderTargets(1, &nullTarget, nullptr);
        return result;
    }

    bool nearValue(float actual, float expected)
    {
        return std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <=
                kTolerance * std::max(1.0F, std::abs(expected));
    }

    bool compare(const Pixel& actual, const Pixel& expected, const char* label)
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

    Pixel expectedEnabled(const Settings& settings)
    {
        const auto power =
            std::pow(std::abs(kSourcePower[0]), settings.volumetricLightingGamma / 2.2F);
        return {
            power * kCompositeColor[0],
            power * kCompositeColor[1],
            power * kCompositeColor[2],
            1.0F,
        };
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

        std::array<Pixel, 2> compositeConstants{};
        compositeConstants[1] = kCompositeColor;
        const auto compositeBuffer =
            createConstantBuffer(device.Get(), compositeConstants);

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        enabledSettings.volumetricLightingGamma = 1.65F;
        const FrameData disabledFrame =
            makeFrameData(disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame =
            makeFrameData(enabledSettings, true, false, 1.0F);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);

        const auto texture = createTexture(device.Get());
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        require(
            device->CreateSamplerState(
                &samplerDescription, sampler.GetAddressOf()),
            "CreateSamplerState");

        const auto vertexShader = createVertexShader(device.Get());
        const auto vanillaShader = createPixelShader(
            device.Get(),
            root /
                "package/Shaders/Community/VerifiedVLSCompositeLinearLighting/"
                "VLSComposite_ImageSpace098.dxbc");
        const auto replacementShader = createPixelShader(
            device.Get(),
            root /
                "package/Shaders/Community/VLSCompositeLinearLighting/"
                "VLSComposite_ImageSpace098.dxbc");

        const auto vanilla = render(
            device.Get(), context.Get(), vertexShader.Get(), vanillaShader.Get(),
            compositeBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
            sampler.Get());
        const auto disabled = render(
            device.Get(), context.Get(), vertexShader.Get(), replacementShader.Get(),
            compositeBuffer.Get(), disabledFrameBuffer.Get(), texture.Get(),
            sampler.Get());
        const auto enabled = render(
            device.Get(), context.Get(), vertexShader.Get(), replacementShader.Get(),
            compositeBuffer.Get(), enabledFrameBuffer.Get(), texture.Get(),
            sampler.Get());

        bool passed = true;
        passed &= compare(disabled, vanilla, "disabled parity");
        passed &= compare(enabled, expectedEnabled(enabledSettings), "enabled model");
        if (!passed) {
            return 1;
        }
        std::cout <<
            "VLS composite Linear Lighting disabled parity and enabled model tests passed.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr <<
            "usage: VLSCompositeLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "VLS composite Linear Lighting parity test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
