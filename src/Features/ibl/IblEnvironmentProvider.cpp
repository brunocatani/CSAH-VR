#include "Features/ibl/IblEnvironmentProvider.h"
#include "render/DebugResourceName.h"

#include <bit>
#include <utility>

namespace csah::ibl
{
    bool EnvironmentProvider::validExtent(std::uint32_t extent) noexcept
    {
        return extent >= 16 && extent <= 512 && std::has_single_bit(extent);
    }

    bool EnvironmentProvider::createCubeTexture(
        ID3D11Device* device,
        std::uint32_t extent,
        std::uint32_t mipCount,
        DXGI_FORMAT format,
        CubeTexture& texture) noexcept
    {
        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = extent;
        textureDescription.Height = extent;
        textureDescription.MipLevels = mipCount;
        textureDescription.ArraySize = kEnvironmentCubeFaceCount;
        textureDescription.Format = format;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_UNORDERED_ACCESS;
        textureDescription.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (FAILED(device->CreateTexture2D(
                &textureDescription,
                nullptr,
                &texture.texture))) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceDescription{};
        shaderResourceDescription.Format = format;
        shaderResourceDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURECUBE;
        shaderResourceDescription.TextureCube.MostDetailedMip = 0;
        shaderResourceDescription.TextureCube.MipLevels = mipCount;
        if (FAILED(device->CreateShaderResourceView(
                texture.texture.Get(),
                &shaderResourceDescription,
                &texture.shaderResource))) {
            return false;
        }

        if (format != kPositionFormat) {
            shaderResourceDescription = {};
            shaderResourceDescription.Format = format;
            shaderResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            shaderResourceDescription.Texture2DArray.MipLevels = 1;
            shaderResourceDescription.Texture2DArray.ArraySize = kEnvironmentCubeFaceCount;
            if (FAILED(device->CreateShaderResourceView(texture.texture.Get(),
                    &shaderResourceDescription, &texture.mipZeroArray))) {
                return false;
            }
        }

        for (std::uint32_t mipLevel = 0; mipLevel < mipCount; ++mipLevel) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC unorderedAccessDescription{};
            unorderedAccessDescription.Format = format;
            unorderedAccessDescription.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            unorderedAccessDescription.Texture2DArray.MipSlice = mipLevel;
            unorderedAccessDescription.Texture2DArray.FirstArraySlice = 0;
            unorderedAccessDescription.Texture2DArray.ArraySize =
                kEnvironmentCubeFaceCount;
            if (FAILED(device->CreateUnorderedAccessView(
                    texture.texture.Get(),
                    &unorderedAccessDescription,
                    &texture.mipUnorderedAccess[mipLevel]))) {
                return false;
            }
        }
        return true;
    }

    bool EnvironmentProvider::createChain(
        ID3D11Device* device,
        std::uint32_t extent,
        std::uint32_t mipCount,
        CubeChain& chain) noexcept
    {
        return createCubeTexture(
                   device,
                   extent,
                   mipCount,
                   kFormat,
                   chain.radiance) &&
            createCubeTexture(
                   device,
                   extent,
                   mipCount,
                   kValidityFormat,
                   chain.validity) &&
            createCubeTexture(
                   device,
                   extent,
                   1,
                   kPositionFormat,
                   chain.position);
    }

    bool EnvironmentProvider::initialize(
        ID3D11Device* device,
        std::uint32_t extent) noexcept
    {
        if (!device || !validExtent(extent)) {
            ++rebuildFailures_;
            if (!resources_.device) {
                state_ = EnvironmentProviderState::disabledFailed;
            }
            return false;
        }
        if (resources_.device.Get() == device && resources_.extent == extent &&
            state_ != EnvironmentProviderState::disabledFailed) {
            return true;
        }

        UINT formatSupport{};
        UINT validityFormatSupport{};
        UINT positionFormatSupport{};
        constexpr UINT requiredFormatSupport =
            D3D11_FORMAT_SUPPORT_TEXTURE2D |
            D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
            D3D11_FORMAT_SUPPORT_MIP |
            D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
        if (FAILED(device->CheckFormatSupport(kFormat, &formatSupport)) ||
            (formatSupport & requiredFormatSupport) != requiredFormatSupport ||
            FAILED(device->CheckFormatSupport(
                kValidityFormat,
                &validityFormatSupport)) ||
            (validityFormatSupport & requiredFormatSupport) !=
                requiredFormatSupport ||
            FAILED(device->CheckFormatSupport(
                kPositionFormat,
                &positionFormatSupport)) ||
            (positionFormatSupport & requiredFormatSupport) !=
                requiredFormatSupport) {
            ++rebuildFailures_;
            if (!resources_.device) {
                state_ = EnvironmentProviderState::disabledFailed;
            }
            return false;
        }

        ResourceSet candidate{};
        candidate.device = device;
        candidate.extent = extent;
        candidate.mipCount = std::bit_width(extent);
        for (auto& chain : candidate.chains) {
            if (!createChain(
                    device,
                    candidate.extent,
                    candidate.mipCount,
                    chain)) {
                ++rebuildFailures_;
                if (!resources_.device) {
                    state_ = EnvironmentProviderState::disabledFailed;
                }
                return false;
            }
        }

        render::nameResource(candidate.chains[0].radiance.texture.Get(), "CSAH / Cubemaps / Radiance A");
        render::nameResource(candidate.chains[1].radiance.texture.Get(), "CSAH / Cubemaps / Radiance B");
        render::nameResource(candidate.chains[0].validity.texture.Get(), "CSAH / Cubemaps / Coverage A");
        render::nameResource(candidate.chains[1].validity.texture.Get(), "CSAH / Cubemaps / Coverage B");
        render::nameResource(candidate.chains[0].position.texture.Get(), "CSAH / Cubemaps / Position A");
        render::nameResource(candidate.chains[1].position.texture.Get(), "CSAH / Cubemaps / Position B");
        resources_ = std::move(candidate);
        coverage_.reset();
        state_ = EnvironmentProviderState::ready;
        frontChain_ = 0;
        ++resourceGeneration_;
        nextUpdateGeneration_ = 1;
        activeUpdateGeneration_ = 0;
        publishedGeneration_ = 0;
        previousPublishedGeneration_ = 0;
        publishedProbeOrigin_ = {};
        previousProbeOrigin_ = {};
        return true;
    }

    void EnvironmentProvider::reset() noexcept
    {
        resources_ = {};
        coverage_.reset();
        state_ = EnvironmentProviderState::uninitialized;
        frontChain_ = 0;
        resourceGeneration_ = 0;
        nextUpdateGeneration_ = 1;
        activeUpdateGeneration_ = 0;
        publishedGeneration_ = 0;
        previousPublishedGeneration_ = 0;
        publishedProbeOrigin_ = {};
        previousProbeOrigin_ = {};
        rebuildFailures_ = 0;
    }

    bool EnvironmentProvider::beginUpdate() noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            !resources_.device) {
            return false;
        }
        coverage_.reset();
        activeUpdateGeneration_ = nextUpdateGeneration_++;
        if (activeUpdateGeneration_ == 0) {
            activeUpdateGeneration_ = nextUpdateGeneration_++;
        }
        state_ = EnvironmentProviderState::updating;
        return true;
    }

    bool EnvironmentProvider::markSubresourceComplete(
        EnvironmentCubeFace face,
        std::uint32_t mipLevel) noexcept
    {
        return state_ == EnvironmentProviderState::updating &&
            coverage_.markComplete(face, mipLevel, resources_.mipCount);
    }

    bool EnvironmentProvider::publishUpdate(
        EnvironmentProbeOrigin probeOrigin) noexcept
    {
        if (state_ != EnvironmentProviderState::updating ||
            !coverage_.complete(resources_.mipCount)) {
            return false;
        }
        previousPublishedGeneration_ = publishedGeneration_;
        previousProbeOrigin_ = publishedProbeOrigin_;
        frontChain_ = 1 - frontChain_;
        publishedGeneration_ = activeUpdateGeneration_;
        publishedProbeOrigin_ = probeOrigin;
        activeUpdateGeneration_ = 0;
        coverage_.reset();
        state_ = EnvironmentProviderState::ready;
        return true;
    }

    void EnvironmentProvider::abortUpdate() noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return;
        }
        coverage_.reset();
        activeUpdateGeneration_ = 0;
        state_ = EnvironmentProviderState::ready;
    }

    ID3D11UnorderedAccessView* EnvironmentProvider::writableMip(
        std::uint32_t mipLevel) const noexcept
    {
        if (state_ != EnvironmentProviderState::updating ||
            mipLevel >= resources_.mipCount) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .radiance.mipUnorderedAccess[mipLevel]
            .Get();
    }

    ID3D11UnorderedAccessView* EnvironmentProvider::writableValidityMip(
        std::uint32_t mipLevel) const noexcept
    {
        if (state_ != EnvironmentProviderState::updating ||
            mipLevel >= resources_.mipCount) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .validity.mipUnorderedAccess[mipLevel]
            .Get();
    }

    ID3D11Texture2D* EnvironmentProvider::writableTexture() const noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_].radiance.texture.Get();
    }

    ID3D11Texture2D* EnvironmentProvider::writableValidityTexture()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_].validity.texture.Get();
    }

    ID3D11UnorderedAccessView* EnvironmentProvider::writablePosition()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .position.mipUnorderedAccess[0]
            .Get();
    }

    ID3D11Texture2D* EnvironmentProvider::writablePositionTexture()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::updating) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_].position.texture.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::writableRadianceArray() const noexcept
    {
        return state_ == EnvironmentProviderState::updating ?
            resources_.chains[1 - frontChain_].radiance.mipZeroArray.Get() : nullptr;
    }

    ID3D11ShaderResourceView* EnvironmentProvider::writableValidityArray() const noexcept
    {
        return state_ == EnvironmentProviderState::updating ?
            resources_.chains[1 - frontChain_].validity.mipZeroArray.Get() : nullptr;
    }

    ID3D11ShaderResourceView* EnvironmentProvider::publishedEnvironment()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].radiance.shaderResource.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::publishedValidity()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].validity.shaderResource.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::publishedPosition()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].position.shaderResource.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::previousEnvironment()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            previousPublishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .radiance.shaderResource.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::previousValidity()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            previousPublishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .validity.shaderResource.Get();
    }

    ID3D11ShaderResourceView* EnvironmentProvider::previousPosition()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            previousPublishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[1 - frontChain_]
            .position.shaderResource.Get();
    }

    EnvironmentProbeOrigin EnvironmentProvider::publishedProbeOrigin()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return {};
        }
        return publishedProbeOrigin_;
    }

    EnvironmentProbeOrigin EnvironmentProvider::previousProbeOrigin()
        const noexcept
    {
        if (state_ != EnvironmentProviderState::ready ||
            previousPublishedGeneration_ == 0) {
            return {};
        }
        return previousProbeOrigin_;
    }

    ID3D11Texture2D* EnvironmentProvider::publishedTexture() const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].radiance.texture.Get();
    }

    ID3D11Texture2D* EnvironmentProvider::publishedValidityTexture()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].validity.texture.Get();
    }

    ID3D11Texture2D* EnvironmentProvider::publishedPositionTexture()
        const noexcept
    {
        if (publishedGeneration_ == 0) {
            return nullptr;
        }
        return resources_.chains[frontChain_].position.texture.Get();
    }

    EnvironmentProviderSnapshot EnvironmentProvider::snapshot() const
        noexcept
    {
        return {
            .state = state_,
            .extent = resources_.extent,
            .mipCount = resources_.mipCount,
            .resourceGeneration = resourceGeneration_,
            .activeUpdateGeneration = activeUpdateGeneration_,
            .publishedGeneration = publishedGeneration_,
            .completedSubresources = coverage_.bits(),
            .rebuildFailures = rebuildFailures_,
        };
    }
}
