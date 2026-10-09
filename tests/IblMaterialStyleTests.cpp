#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using Pixel = std::array<float, 4>;
    void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
    void ok(HRESULT result, const char* text) { check(SUCCEEDED(result), text); }
    ComPtr<ID3D11ShaderResourceView> cubes(ID3D11Device* device, UINT count, bool array, float referenceValue = 0.5F)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.ArraySize = count * 6;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        std::vector<Pixel> pixels(count * 6);
        std::vector<D3D11_SUBRESOURCE_DATA> data(count * 6);
        for (UINT cube = 0; cube < count; ++cube) for (UINT face = 0; face < 6; ++face) {
            Pixel p{ referenceValue, referenceValue, referenceValue, 1 };
            if (cube == 1) p = { 1, 0, 0, 1 };
            if (cube == 2) p = face % 2 ? Pixel{1,0,0,1} : Pixel{0,0,1,1};
            if (cube == 3) p = {};
            const UINT index = cube * 6 + face;
            pixels[index] = p;
            data[index] = {pixels[index].data(), sizeof(Pixel), 0};
        }
        ComPtr<ID3D11Texture2D> texture;
        ok(device->CreateTexture2D(&desc, data.data(), &texture), "cube texture creation failed");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = desc.Format;
        view.ViewDimension = array ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY : D3D11_SRV_DIMENSION_TEXTURECUBE;
        if (array) { view.TextureCubeArray.MipLevels = 1; view.TextureCubeArray.NumCubes = count; }
        else view.TextureCube.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> result;
        ok(device->CreateShaderResourceView(texture.Get(), &view, &result), "cube view creation failed");
        return result;
    }
}
int main(int argc, char** argv)
{
    try {
        check(argc == 2, "expected repo root");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
        ok(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &requested, 1,
            D3D11_SDK_VERSION, &device, &level, &context), "WARP device creation failed");
        std::ifstream stream(std::filesystem::path(argv[1]) / "package/Shaders/Community/IBL/MaterialStyleCS.dxbc", std::ios::binary);
        const std::vector<char> code{std::istreambuf_iterator<char>(stream), {}};
        check(!code.empty(), "style shader missing");
        ComPtr<ID3D11ComputeShader> shader;
        ok(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader), "style bytecode rejected");
        auto original = cubes(device.Get(), 4, true);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 2; desc.Height = 4;
        desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output, staging;
        ok(device->CreateTexture2D(&desc, nullptr, &output), "style output creation failed");
        desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ok(device->CreateTexture2D(&desc, nullptr, &staging), "style staging creation failed");
        ComPtr<ID3D11UnorderedAccessView> uav;
        ok(device->CreateUnorderedAccessView(output.Get(), nullptr, &uav), "style UAV creation failed");
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        ok(device->CreateSamplerState(&samplerDesc, &sampler), "sampler creation failed");
        context->CSSetShader(shader.Get(), nullptr, 0);
        auto* samplerPtr = sampler.Get();
        context->CSSetSamplers(0, 1, &samplerPtr);
        for (UINT mode : {0U, 1U, 2U, 3U}) {
            const bool hasReference = mode != 3;
            const float referenceValue = mode == 1 ? 0.735356983F : (mode == 2 ? 0.21404114F : 0.5F);
            auto reference = cubes(device.Get(), 1, false, referenceValue);
            const std::array<UINT, 4> encoding{ mode == 3 ? 0 : mode, 0, 0, 0 };
            D3D11_BUFFER_DESC bufferDesc{};
            bufferDesc.ByteWidth = sizeof(encoding);
            bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA initial{ encoding.data(), 0, 0 };
            ComPtr<ID3D11Buffer> constants;
            ok(device->CreateBuffer(&bufferDesc, &initial, &constants), "encoding constants creation failed");
            auto* constantPointer = constants.Get();
            context->CSSetConstantBuffers(0, 1, &constantPointer);
            ID3D11ShaderResourceView* views[]{ original.Get(), hasReference ? reference.Get() : nullptr };
            auto* out = uav.Get();
            context->CSSetShaderResources(0, 2, views);
            context->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
            context->Dispatch(4, 1, 1);
            out = nullptr;
            context->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
            context->CopyResource(staging.Get(), output.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            ok(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "style readback failed");
            std::array<std::array<Pixel, 2>, 4> result{};
            for (UINT cube = 0; cube < 4; ++cube) {
                const auto* row = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + cube * mapped.RowPitch);
                result[cube] = {row[0], row[1]};
            }
            context->Unmap(staging.Get(), 0);
            check(result[0][0][3] == (hasReference ? -1.0F : -2.0F), "exterior identity or missing-reference fallback failed");
            check(result[1][0][0] > 4.6F && result[1][0][1] == 0 && result[1][0][3] == 1, "red authored tint was not preserved");
            check(std::abs(result[1][1][0] - std::sqrt(0.2126F)) < 0.001F, "authored intensity policy changed");
            check(result[2][0][3] == -2, "inconsistent authored cube was replaced");
            check(result[3][0][3] == -2, "unavailable streamed cube was replaced");
        }
        std::cout << "GPU cubemap style: exterior reference, tint, intensity, inconsistent materials, missing data passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
