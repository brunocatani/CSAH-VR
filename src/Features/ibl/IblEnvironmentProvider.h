#pragma once

#include "Features/ibl/IblProviderModel.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace csah::ibl
{
    enum class EnvironmentProviderState : std::uint8_t
    {
        uninitialized,
        ready,
        updating,
        disabledFailed,
    };

    struct EnvironmentProviderSnapshot
    {
        EnvironmentProviderState state{};
        std::uint32_t extent{};
        std::uint32_t mipCount{};
        std::uint64_t resourceGeneration{};
        std::uint64_t activeUpdateGeneration{};
        std::uint64_t publishedGeneration{};
        std::uint64_t completedSubresources{};
        std::uint64_t rebuildFailures{};
    };

    struct EnvironmentProbeOrigin
    {
        Float3 position{};
        bool valid{};
    };

    // Render-thread-only owner for the shared view-centred environment map.
    // Both cube chains are created transactionally. Updates always target the
    // private back chain and only a complete six-face/all-mip generation can
    // become the immutable published front chain.
    class EnvironmentProvider final
    {
    public:
        static constexpr DXGI_FORMAT kFormat =
            DXGI_FORMAT_R11G11B10_FLOAT;
        static constexpr DXGI_FORMAT kValidityFormat =
            DXGI_FORMAT_R32_FLOAT;
        static constexpr DXGI_FORMAT kPositionFormat =
            DXGI_FORMAT_R32G32B32A32_FLOAT;

        [[nodiscard]] bool initialize(
            ID3D11Device* device,
            std::uint32_t extent) noexcept;
        void reset() noexcept;

        [[nodiscard]] bool beginUpdate() noexcept;
        [[nodiscard]] bool markSubresourceComplete(
            EnvironmentCubeFace face,
            std::uint32_t mipLevel) noexcept;
        [[nodiscard]] bool publishUpdate(
            EnvironmentProbeOrigin probeOrigin) noexcept;
        void abortUpdate() noexcept;

        [[nodiscard]] ID3D11UnorderedAccessView* writableMip(
            std::uint32_t mipLevel) const noexcept;
        [[nodiscard]] ID3D11UnorderedAccessView* writableValidityMip(
            std::uint32_t mipLevel) const noexcept;
        [[nodiscard]] ID3D11Texture2D* writableTexture() const noexcept;
        [[nodiscard]] ID3D11Texture2D* writableValidityTexture()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* writableRadianceArray() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* writableValidityArray() const noexcept;
        [[nodiscard]] ID3D11UnorderedAccessView* writablePosition()
            const noexcept;
        [[nodiscard]] ID3D11Texture2D* writablePositionTexture()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* publishedEnvironment()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* publishedValidity()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* publishedPosition()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* previousEnvironment()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* previousValidity()
            const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* previousPosition()
            const noexcept;
        [[nodiscard]] EnvironmentProbeOrigin publishedProbeOrigin()
            const noexcept;
        [[nodiscard]] EnvironmentProbeOrigin previousProbeOrigin()
            const noexcept;
        [[nodiscard]] ID3D11Texture2D* publishedTexture() const noexcept;
        [[nodiscard]] ID3D11Texture2D* publishedValidityTexture()
            const noexcept;
        [[nodiscard]] ID3D11Texture2D* publishedPositionTexture()
            const noexcept;
        [[nodiscard]] EnvironmentProviderSnapshot snapshot() const noexcept;

    private:
        struct CubeTexture
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mipZeroArray;
            std::array<
                Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>,
                kEnvironmentMaximumMipCount>
                mipUnorderedAccess;
        };

        struct CubeChain
        {
            CubeTexture radiance;
            CubeTexture validity;
            // Position is shared temporal and material metadata. Only mip
            // zero is required because parallax correction uses the exact
            // captured hit shell rather than a filtered position field.
            CubeTexture position;
        };

        struct ResourceSet
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            std::array<CubeChain, 2> chains;
            std::uint32_t extent{};
            std::uint32_t mipCount{};
        };

        [[nodiscard]] static bool validExtent(
            std::uint32_t extent) noexcept;
        [[nodiscard]] static bool createCubeTexture(
            ID3D11Device* device,
            std::uint32_t extent,
            std::uint32_t mipCount,
            DXGI_FORMAT format,
            CubeTexture& texture) noexcept;
        [[nodiscard]] static bool createChain(
            ID3D11Device* device,
            std::uint32_t extent,
            std::uint32_t mipCount,
            CubeChain& chain) noexcept;

        ResourceSet resources_{};
        EnvironmentUpdateCoverage coverage_{};
        EnvironmentProviderState state_{
            EnvironmentProviderState::uninitialized
        };
        std::uint32_t frontChain_{};
        std::uint64_t resourceGeneration_{};
        std::uint64_t nextUpdateGeneration_{ 1 };
        std::uint64_t activeUpdateGeneration_{};
        std::uint64_t publishedGeneration_{};
        std::uint64_t previousPublishedGeneration_{};
        EnvironmentProbeOrigin publishedProbeOrigin_{};
        EnvironmentProbeOrigin previousProbeOrigin_{};
        std::uint64_t rebuildFailures_{};
    };
}
