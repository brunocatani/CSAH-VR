#include "Features/ibl/IblMaterialBindingScope.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;
    using csah::ibl::MaterialBindingRejection;
    using csah::ibl::ScopedMaterialBindings;

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    struct DeviceSet
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    DeviceSet createDevice()
    {
        DeviceSet result;
        D3D_FEATURE_LEVEL selected{};
        constexpr std::array levels{ D3D_FEATURE_LEVEL_11_0 };
        require(
            SUCCEEDED(D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                levels.data(),
                static_cast<UINT>(levels.size()),
                D3D11_SDK_VERSION,
                &result.device,
                &selected,
                &result.context)),
            "D3D11CreateDevice(WARP)");
        require(
            selected == D3D_FEATURE_LEVEL_11_0,
            "WARP did not provide feature level 11_0");
        return result;
    }

    ComPtr<ID3D11ShaderResourceView> createCube(ID3D11Device* device)
    {
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = 4;
        textureDescription.Height = 4;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 6;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        textureDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        ComPtr<ID3D11Texture2D> texture;
        require(
            SUCCEEDED(device->CreateTexture2D(
                &textureDescription,
                nullptr,
                &texture)),
            "CreateTexture2D(cube)");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = textureDescription.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        viewDescription.TextureCube.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        require(
            SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(),
                &viewDescription,
                &view)),
            "CreateShaderResourceView(cube)");
        return view;
    }

    ComPtr<ID3D11Buffer> createConstants(
        ID3D11Device* device,
        float value)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 48;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        std::array<float, 12> values{};
        values[0] = value;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = values.data();
        ComPtr<ID3D11Buffer> buffer;
        require(
            SUCCEEDED(device->CreateBuffer(&description, &data, &buffer)),
            "CreateBuffer(constants)");
        return buffer;
    }

    void requireBindings(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* albedo,
        ID3D11ShaderResourceView* radiance,
        ID3D11ShaderResourceView* validity,
        ID3D11ShaderResourceView* previousRadiance,
        ID3D11ShaderResourceView* previousValidity,
        ID3D11ShaderResourceView* position,
        ID3D11ShaderResourceView* previousPosition,
        ID3D11ShaderResourceView* materialProperties,
        ID3D11ShaderResourceView* pbrMaterial,
        ID3D11ShaderResourceView* surfaceClass,
        ID3D11Buffer* constants,
        const std::string& label)
    {
        std::array<ID3D11ShaderResourceView*, 8> resources{};
        context->PSGetShaderResources(
            ScopedMaterialBindings::kAlbedoSlot,
            static_cast<UINT>(resources.size()),
            resources.data());
        ID3D11Buffer* actualConstants{};
        context->PSGetConstantBuffers(
            ScopedMaterialBindings::kConstantSlot,
            1,
            &actualConstants);
        const auto matches = resources[0] == albedo &&
            resources[1] == radiance && resources[2] == validity &&
            resources[3] == previousRadiance &&
            resources[4] == previousValidity &&
            resources[5] == position &&
            resources[6] == previousPosition &&
            resources[7] == materialProperties &&
            actualConstants == constants;
        ID3D11ShaderResourceView* actualPbrMaterial{};
        ID3D11ShaderResourceView* actualSurfaceClass{};
        context->PSGetShaderResources(
            ScopedMaterialBindings::kPbrMaterialSlot,
            1,
            &actualPbrMaterial);
        context->PSGetShaderResources(
            ScopedMaterialBindings::kSurfaceClassSlot,
            1,
            &actualSurfaceClass);
        for (auto* resource : resources) {
            if (resource) {
                resource->Release();
            }
        }
        if (actualConstants) {
            actualConstants->Release();
        }
        const auto pbrMatches = actualPbrMaterial == pbrMaterial &&
            actualSurfaceClass == surfaceClass;
        if (actualPbrMaterial) {
            actualPbrMaterial->Release();
        }
        if (actualSurfaceClass) {
            actualSurfaceClass->Release();
        }
        require(matches && pbrMatches, label);
    }
}

int main()
{
    try {
        auto primary = createDevice();
        auto foreign = createDevice();
        auto previousAlbedo = createCube(primary.device.Get());
        auto previousRadiance = createCube(primary.device.Get());
        auto previousValidity = createCube(primary.device.Get());
        auto previousTransitionRadiance = createCube(primary.device.Get());
        auto previousTransitionValidity = createCube(primary.device.Get());
        auto previousPosition = createCube(primary.device.Get());
        auto previousTransitionPosition = createCube(primary.device.Get());
        auto previousMaterialProperties = createCube(primary.device.Get());
        auto previousPbrMaterial = createCube(primary.device.Get());
        auto previousSurfaceClass = createCube(primary.device.Get());
        auto publishedAlbedo = createCube(primary.device.Get());
        auto publishedRadiance = createCube(primary.device.Get());
        auto publishedValidity = createCube(primary.device.Get());
        auto publishedPreviousRadiance = createCube(primary.device.Get());
        auto publishedPreviousValidity = createCube(primary.device.Get());
        auto publishedPosition = createCube(primary.device.Get());
        auto publishedPreviousPosition = createCube(primary.device.Get());
        auto publishedMaterialProperties = createCube(primary.device.Get());
        auto publishedPbrMaterial = createCube(primary.device.Get());
        auto publishedSurfaceClass = createCube(primary.device.Get());
        auto previousConstants = createConstants(primary.device.Get(), 0.25F);
        auto enabledConstants = createConstants(primary.device.Get(), 1.0F);
        auto disabledConstants = createConstants(primary.device.Get(), 0.0F);
        auto foreignRadiance = createCube(foreign.device.Get());
        auto previousStyle = createCube(primary.device.Get());
        auto publishedStyle = createCube(primary.device.Get());
        auto* stylePointer = previousStyle.Get();
        primary.context->PSSetShaderResources(37, 1, &stylePointer);
        const auto requireStyle = [&](ID3D11ShaderResourceView* expected) {
            ComPtr<ID3D11ShaderResourceView> actual;
            primary.context->PSGetShaderResources(37, 1, &actual);
            require(actual.Get() == expected, "cubemap style slot was not restored exactly");
        };

        std::array<ID3D11ShaderResourceView*, 8> previousResources{
            previousAlbedo.Get(),
            previousRadiance.Get(),
            previousValidity.Get(),
            previousTransitionRadiance.Get(),
            previousTransitionValidity.Get(),
            previousPosition.Get(),
            previousTransitionPosition.Get(),
            previousMaterialProperties.Get(),
        };
        auto* previousBuffer = previousConstants.Get();
        primary.context->PSSetShaderResources(
            ScopedMaterialBindings::kAlbedoSlot,
            static_cast<UINT>(previousResources.size()),
            previousResources.data());
        primary.context->PSSetConstantBuffers(
            ScopedMaterialBindings::kConstantSlot,
            1,
            &previousBuffer);
        auto* previousPbr = previousPbrMaterial.Get();
        auto* previousClass = previousSurfaceClass.Get();
        primary.context->PSSetShaderResources(
            ScopedMaterialBindings::kPbrMaterialSlot,
            1,
            &previousPbr);
        primary.context->PSSetShaderResources(
            ScopedMaterialBindings::kSurfaceClassSlot,
            1,
            &previousClass);

        {
            ScopedMaterialBindings scope(
                primary.context.Get(),
                publishedAlbedo.Get(),
                publishedRadiance.Get(),
                publishedValidity.Get(),
                publishedPreviousRadiance.Get(),
                publishedPreviousValidity.Get(),
                publishedPosition.Get(),
                publishedPreviousPosition.Get(),
                publishedMaterialProperties.Get(),
                publishedPbrMaterial.Get(),
                publishedSurfaceClass.Get(),
                enabledConstants.Get(), publishedStyle.Get());
            require(scope.active(), "published material scope was rejected");
            requireStyle(publishedStyle.Get());
            requireBindings(
                primary.context.Get(),
                publishedAlbedo.Get(),
                publishedRadiance.Get(),
                publishedValidity.Get(),
                publishedPreviousRadiance.Get(),
                publishedPreviousValidity.Get(),
                publishedPosition.Get(),
                publishedPreviousPosition.Get(),
                publishedMaterialProperties.Get(),
                publishedPbrMaterial.Get(),
                publishedSurfaceClass.Get(),
                enabledConstants.Get(),
                "published material bindings were not applied exactly");
            require(scope.restore(), "published material scope did not restore");
            requireStyle(previousStyle.Get());
        }
        requireBindings(
            primary.context.Get(),
            previousAlbedo.Get(),
            previousRadiance.Get(),
            previousValidity.Get(),
            previousTransitionRadiance.Get(),
            previousTransitionValidity.Get(),
            previousPosition.Get(),
            previousTransitionPosition.Get(),
            previousMaterialProperties.Get(),
            previousPbrMaterial.Get(),
            previousSurfaceClass.Get(),
            previousConstants.Get(),
            "published material scope did not restore previous bindings");

        {
            ScopedMaterialBindings scope(
                primary.context.Get(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                disabledConstants.Get());
            require(scope.active(), "disabled material scope was rejected");
            requireBindings(
                primary.context.Get(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                disabledConstants.Get(),
                "disabled material bindings were not applied exactly");
        }
        requireBindings(
            primary.context.Get(),
            previousAlbedo.Get(),
            previousRadiance.Get(),
            previousValidity.Get(),
            previousTransitionRadiance.Get(),
            previousTransitionValidity.Get(),
            previousPosition.Get(),
            previousTransitionPosition.Get(),
            previousMaterialProperties.Get(),
            previousPbrMaterial.Get(),
            previousSurfaceClass.Get(),
            previousConstants.Get(),
            "disabled material scope did not restore previous bindings");

        {
            ScopedMaterialBindings rejected(
                primary.context.Get(),
                foreignRadiance.Get(),
                publishedRadiance.Get(),
                publishedValidity.Get(),
                publishedPreviousRadiance.Get(),
                publishedPreviousValidity.Get(),
                publishedPosition.Get(),
                publishedPreviousPosition.Get(),
                publishedMaterialProperties.Get(),
                publishedPbrMaterial.Get(),
                publishedSurfaceClass.Get(),
                enabledConstants.Get());
            require(!rejected.active(), "foreign-device scope was accepted");
            require(
                rejected.rejection() == MaterialBindingRejection::deviceMismatch,
                "foreign-device rejection reason changed");
        }
        requireBindings(
            primary.context.Get(),
            previousAlbedo.Get(),
            previousRadiance.Get(),
            previousValidity.Get(),
            previousTransitionRadiance.Get(),
            previousTransitionValidity.Get(),
            previousPosition.Get(),
            previousTransitionPosition.Get(),
            previousMaterialProperties.Get(),
            previousPbrMaterial.Get(),
            previousSurfaceClass.Get(),
            previousConstants.Get(),
            "rejected scope changed existing bindings");

        std::cout
            << "FO4VR IBL t29..t36/t45/t47/b5 material transaction verified on D3D11 WARP\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
