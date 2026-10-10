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

    constexpr float kTolerance = 8.0e-5F;
    constexpr std::array<float, 4> kBaseColor{ 0.35F, 0.6F, 0.8F, 0.7F };
    constexpr std::array<float, 4> kBlendColor{ 0.9F, 0.2F, 0.4F, 0.5F };
    constexpr std::array<float, 4> kVertexColor{ 0.4F, 0.7F, 0.55F, 0.8F };
    constexpr float kNoise = 0.75F;
    constexpr float kBlend = 0.65F;
    constexpr float kScale = 1.25F;
    constexpr float kHorizonFade = 0.6F;
    constexpr float kSkyGamma = 1.8F;
    constexpr float kSkyProducerGamma = 2.2F;
    constexpr float kNativeSkyGamma = 2.2F;

    struct RenderTarget
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11Texture2D> staging;
    };

    using Pixel = std::array<float, 4>;
    using RenderResult = std::array<Pixel, 2>;

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

    ComPtr<ID3D11ShaderResourceView> createTexture(
        ID3D11Device* device,
        const Pixel& pixel)
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
        D3D11_SUBRESOURCE_DATA initial{ pixel.data(), sizeof(Pixel), 0 };
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

    std::string vertexShaderSource(std::uint32_t descriptor)
    {
        return "#define SKY_TECHNIQUE " + std::to_string(descriptor) + R"(
struct VSOutput
{
    float4 position : SV_POSITION;
#if SKY_TECHNIQUE != 0 && SKY_TECHNIQUE != 1
    float2 baseUv : TEXCOORD0;
#endif
#if SKY_TECHNIQUE == 6
    float2 blendUv : TEXCOORD1;
#endif
#if SKY_TECHNIQUE == 3
    float horizonFade : TEXCOORD2;
#endif
#if SKY_TECHNIQUE != 0
    float4 color : COLOR0;
    float4 currentPosition : POSITION0;
    float4 previousPosition : POSITION1;
    nointerpolation uint eyeIndex : POSITION2;
#endif
    float cullDistance : SV_CullDistance0;
    float clipDistance : SV_ClipDistance0;
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
#if SKY_TECHNIQUE != 0 && SKY_TECHNIQUE != 1
    output.baseUv = float2(0.5, 0.5);
#endif
#if SKY_TECHNIQUE == 6
    output.blendUv = float2(0.5, 0.5);
#endif
#if SKY_TECHNIQUE == 3
    output.horizonFade = 0.6;
#endif
#if SKY_TECHNIQUE != 0
    output.color = float4(0.4, 0.7, 0.55, 0.8);
    output.currentPosition = float4(0.2, -0.1, 0.3, 1.0);
    output.previousPosition = float4(0.1, -0.2, 0.25, 1.0);
    output.eyeIndex = 0;
#endif
    output.cullDistance = 1.0;
    output.clipDistance = 1.0;
    return output;
}
)";
    }

    ComPtr<ID3D11VertexShader> createVertexShader(
        ID3D11Device* device,
        std::uint32_t descriptor)
    {
        const auto source = vertexShaderSource(descriptor);
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const auto result = D3DCompile(
            source.data(),
            source.size(),
            "SkyLinearLightingParityVS",
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

    bool hasOutputRegister(
        const std::filesystem::path& path,
        UINT outputRegister)
    {
        const auto bytes = readFile(path);
        ComPtr<ID3D11ShaderReflection> reflection;
        require(
            D3DReflect(
                bytes.data(),
                bytes.size(),
                __uuidof(ID3D11ShaderReflection),
                reinterpret_cast<void**>(reflection.GetAddressOf())),
            "D3DReflect");
        D3D11_SHADER_DESC shaderDescription{};
        require(reflection->GetDesc(&shaderDescription), "GetDesc(reflection)");
        for (UINT index = 0; index < shaderDescription.OutputParameters;
             ++index) {
            D3D11_SIGNATURE_PARAMETER_DESC parameter{};
            require(
                reflection->GetOutputParameterDesc(index, &parameter),
                "GetOutputParameterDesc");
            if (parameter.Register == outputRegister) {
                return true;
            }
        }
        return false;
    }

    RenderResult render(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11VertexShader* vertexShader,
        ID3D11PixelShader* pixelShader,
        ID3D11Buffer* skyBuffer,
        ID3D11Buffer* frameBuffer,
        ID3D11Buffer* stereoBuffer,
        const std::array<ComPtr<ID3D11ShaderResourceView>, 3>& textures,
        ID3D11SamplerState* sampler)
    {
        std::array<RenderTarget, 2> targets{
            createRenderTarget(device),
            createRenderTarget(device),
        };
        std::array<ID3D11RenderTargetView*, 2> views{
            targets[0].view.Get(), targets[1].view.Get()
        };
        const float clear[4]{};
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
        context->PSSetConstantBuffers(2, 1, &skyBuffer);
        context->PSSetConstantBuffers(5, 1, &frameBuffer);
        context->PSSetConstantBuffers(12, 1, &stereoBuffer);
        std::array<ID3D11ShaderResourceView*, 3> textureViews{
            textures[0].Get(), textures[1].Get(), textures[2].Get()
        };
        context->PSSetShaderResources(
            0, static_cast<UINT>(textureViews.size()), textureViews.data());
        std::array<ID3D11SamplerState*, 3> samplers{ sampler, sampler, sampler };
        context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        context->Draw(3, 0);

        RenderResult result{};
        for (std::size_t index = 0; index < targets.size(); ++index) {
            context->CopyResource(
                targets[index].staging.Get(), targets[index].texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(
                context->Map(
                    targets[index].staging.Get(),
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped),
                "Map(render target)");
            std::memcpy(result[index].data(), mapped.pData, sizeof(Pixel));
            context->Unmap(targets[index].staging.Get(), 0);
        }

        std::array<ID3D11ShaderResourceView*, 3> nullTextures{};
        context->PSSetShaderResources(
            0, static_cast<UINT>(nullTextures.size()), nullTextures.data());
        std::array<ID3D11RenderTargetView*, 2> nullTargets{};
        context->OMSetRenderTargets(
            static_cast<UINT>(nullTargets.size()), nullTargets.data(), nullptr);
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

    float sky(float value, float gamma)
    {
        return std::pow(std::abs(value), gamma);
    }

    float skyProducerColor(float value, float gamma)
    {
        return std::pow(
            std::abs(value), gamma / kSkyProducerGamma);
    }

    std::array<float, 3> skyCloudColor(
        const std::array<float, 4>& color,
        float gamma)
    {
        constexpr std::array<float, 3> luminanceWeights{
            0.2126F, 0.7152F, 0.0722F
        };
        float luminance = 0.0F;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            luminance += std::abs(color[channel]) * luminanceWeights[channel];
        }
        const float scale = std::pow(luminance, gamma) /
            std::max(luminance, 1.0e-5F);
        return {
            std::abs(color[0]) * scale,
            std::abs(color[1]) * scale,
            std::abs(color[2]) * scale
        };
    }

    Pixel expectedEnabled(std::uint32_t descriptor, float gamma)
    {
        Pixel expected{};
        const auto cloudColor = skyCloudColor(kVertexColor, gamma);
        const auto scaledProduct = [gamma](float lhs, float rhs) {
            return sky(lhs, gamma) * skyProducerColor(rhs, gamma) * kScale;
        };
        const auto scaledCloudProduct = [gamma](
                                                  float texture,
                                                  float tint) {
            return sky(texture, gamma) * tint * kScale;
        };
        const float noise = kNoise * 0.0078125F - 0.001953125F;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            switch (descriptor) {
            case 1:
                expected[channel] =
                    skyProducerColor(kVertexColor[channel], gamma) * kScale + noise;
                break;
            case 2:
                expected[channel] = sky(kBaseColor[channel], gamma);
                break;
            case 3:
                expected[channel] =
                    scaledProduct(kBaseColor[channel], kVertexColor[channel]) * 1.5F;
                break;
            case 4:
            case 5:
                expected[channel] =
                    scaledCloudProduct(kBaseColor[channel], cloudColor[channel]);
                break;
            case 6:
                expected[channel] =
                    std::lerp(
                        sky(kBaseColor[channel], gamma),
                        sky(kBlendColor[channel], gamma),
                        kBlend) *
                    cloudColor[channel] * kScale;
                break;
            case 7:
                expected[channel] = scaledCloudProduct(
                    kBaseColor[channel], cloudColor[channel]);
                break;
            case 8:
                expected[channel] = scaledCloudProduct(
                    kBaseColor[channel], cloudColor[channel]) + noise;
                break;
            default:
                throw std::runtime_error("unexpected Sky descriptor");
            }
        }

        switch (descriptor) {
        case 1:
        case 8:
            expected[3] = 1.0F;
            break;
        case 2:
            expected[3] = std::pow(std::abs(kBaseColor[3]), 2.2F);
            break;
        case 3:
            expected[3] = kBaseColor[3] * kVertexColor[3] * kHorizonFade;
            break;
        case 4:
            expected[3] =
                std::pow(std::abs(kBaseColor[3]), 2.2F) * kVertexColor[3];
            break;
        case 5:
            expected[3] = kBaseColor[3] * kVertexColor[3];
            break;
        case 6:
            expected[3] =
                std::lerp(kBaseColor[3], kBlendColor[3], kBlend) *
                kVertexColor[3];
            break;
        case 7:
            expected[3] = std::clamp(kBlend - 0.4F, 0.0F, 1.0F) *
                kBaseColor[3] * kVertexColor[3] * (1.0F / 0.6F);
            break;
        }
        return expected;
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

        const std::array<float, 4> skyParameters{ kBlend, kScale, 0.0F, 0.0F };
        std::array<std::array<float, 4>, 71> stereo{};
        stereo[63] = { 1.0F, 0.0F, 0.0F, 0.0F };
        stereo[64] = { 0.0F, 1.0F, 0.0F, 0.0F };
        stereo[66] = { 0.0F, 0.0F, 0.0F, 1.0F };
        stereo[51] = { 1.0F, 0.0F, 0.0F, 0.0F };
        stereo[52] = { 0.0F, 1.0F, 0.0F, 0.0F };
        stereo[54] = { 0.0F, 0.0F, 0.0F, 1.0F };

        Settings disabledSettings{};
        disabledSettings.enabled = false;
        disabledSettings.skyGamma = kSkyGamma;
        Settings enabledSettings = disabledSettings;
        enabledSettings.enabled = true;
        enabledSettings.preserveNativeDarkness = false;
        const FrameData disabledFrame = makeFrameData(
            disabledSettings, true, false, 1.0F);
        const FrameData enabledFrame = makeFrameData(
            enabledSettings, true, false, 1.0F);
        Settings nativeNightSettings = enabledSettings;
        nativeNightSettings.preserveNativeDarkness = true;
        const FrameData nativeNightFrame = makeFrameData(
            nativeNightSettings, true, false, 1.0F);

        const auto skyBuffer = createConstantBuffer(device.Get(), skyParameters);
        const auto disabledFrameBuffer =
            createConstantBuffer(device.Get(), disabledFrame);
        const auto enabledFrameBuffer =
            createConstantBuffer(device.Get(), enabledFrame);
        const auto nativeNightFrameBuffer =
            createConstantBuffer(device.Get(), nativeNightFrame);
        const auto stereoBuffer = createConstantBuffer(device.Get(), stereo);
        const std::array<ComPtr<ID3D11ShaderResourceView>, 3> textures{
            createTexture(device.Get(), kBaseColor),
            createTexture(device.Get(), kBlendColor),
            createTexture(device.Get(), { kNoise, 0.0F, 0.0F, 1.0F }),
        };

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

        const auto verified = root /
            "package/Shaders/Community/VerifiedSkyLinearLighting";
        const auto replacement = root /
            "package/Shaders/Community/SkyLinearLighting";
        bool passed = true;
        for (std::uint32_t descriptor = 1; descriptor <= 8; ++descriptor) {
            const auto name = "SkyTechnique" + std::to_string(descriptor) +
                "_0000000" + std::to_string(descriptor) + ".dxbc";
            const auto vertexShader = createVertexShader(device.Get(), descriptor);
            const auto vanillaShader = createPixelShader(device.Get(), verified / name);
            const auto replacementShader =
                createPixelShader(device.Get(), replacement / name);
            const auto hasCloudTarget =
                hasOutputRegister(replacement / name, 3);
            const auto expectsCloudTarget = false;
            if (hasCloudTarget != expectsCloudTarget) {
                std::cerr << "Sky descriptor " << descriptor
                          << (expectsCloudTarget ? " lost" : " unexpectedly owns")
                          << " private cloud SV_Target3\n";
                passed = false;
            }
            const auto vanilla = render(
                device.Get(), context.Get(), vertexShader.Get(), vanillaShader.Get(),
                skyBuffer.Get(), disabledFrameBuffer.Get(), stereoBuffer.Get(),
                textures, sampler.Get());
            const auto disabled = render(
                device.Get(), context.Get(), vertexShader.Get(), replacementShader.Get(),
                skyBuffer.Get(), disabledFrameBuffer.Get(), stereoBuffer.Get(),
                textures, sampler.Get());
            const auto enabled = render(
                device.Get(), context.Get(), vertexShader.Get(), replacementShader.Get(),
                skyBuffer.Get(), enabledFrameBuffer.Get(), stereoBuffer.Get(),
                textures, sampler.Get());
            const auto nativeNight = render(
                device.Get(), context.Get(), vertexShader.Get(), replacementShader.Get(),
                skyBuffer.Get(), nativeNightFrameBuffer.Get(), stereoBuffer.Get(),
                textures, sampler.Get());

            const auto label = "Sky descriptor " + std::to_string(descriptor);
            passed &= compare(disabled[0], vanilla[0], label + " disabled color");
            passed &= compare(disabled[1], vanilla[1], label + " disabled motion");
            passed &= compare(
                enabled[0],
                expectedEnabled(descriptor, kSkyGamma),
                label + " enabled color");
            passed &= compare(enabled[1], vanilla[1], label + " enabled motion");
            passed &= compare(
                nativeNight[0],
                expectedEnabled(descriptor, kNativeSkyGamma),
                label + " native-night color");
            passed &= compare(
                nativeNight[1], vanilla[1], label + " native-night motion");
        }
        if (!passed) {
            return 1;
        }
        std::cout << "Sky Linear Lighting parity tests passed for descriptors 1-8.\n";
        return 0;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "usage: SkyLinearLightingShaderParityTests <repo-root>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path(arguments[1]));
    } catch (const std::exception& error) {
        std::cerr << "Sky Linear Lighting parity test failed: " << error.what()
                  << '\n';
        return 1;
    }
}
