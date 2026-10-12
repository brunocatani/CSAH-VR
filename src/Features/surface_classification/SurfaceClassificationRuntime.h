#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace csah::surface_classification
{
    enum class SurfaceClassCode : std::uint32_t
    {
        ordinary = 0,
        grass = 1,
        hair = 2,
        skin = 3,
        terrain = 4,
        count = 5,
    };

    enum class Consumer : std::uint32_t
    {
        terrainBlending = 1u << 3,
        basicWetness = 1u << 4,
        pbr = 1u << 5,
    };

    enum class ProducerEvidence : std::uint32_t
    {
        materialIdentity,
        descriptor,
        grassVertexShaderIdentity,
    };

    struct GBufferBinding
    {
        std::array<ID3D11RenderTargetView*, 8> renderTargets{};
        UINT renderTargetCount{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return (renderTargetCount == 7 || renderTargetCount == 8) &&
                renderTargets[6] != nullptr &&
                (renderTargetCount == 7 || renderTargets[7] != nullptr);
        }
    };

    struct RuntimeSnapshot
    {
        std::uint32_t consumerMask{};
        bool targetReady{};
        UINT width{};
        UINT height{};
        std::uint64_t acceptedGBufferBinds{};
        std::uint64_t rejectedGBufferBinds{};
        std::uint64_t targetRebuilds{};
        std::uint64_t targetClears{};
        std::array<std::uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            producerSelections{};
        std::array<std::uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            descriptorObservations{};
        std::uint64_t descriptorObservationMisses{};
        std::uint64_t descriptorScopeMisses{};
        std::uint64_t descriptorContractMisses{};
        std::uint64_t failures{};
    };

    class Runtime final
    {
    public:
        static Runtime& get() noexcept;

        Runtime(const Runtime&) = delete;
        Runtime(Runtime&&) = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime& operator=(Runtime&&) = delete;

        void onDeviceCreated(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept;
        void setConsumerEnabled(Consumer consumer, bool enabled) noexcept;
        void setPbrMaterialTransportEnabled(bool enabled) noexcept;
        [[nodiscard]] bool required() const noexcept;
        [[nodiscard]] UINT requiredRenderTargetCount() const noexcept;

        // Called only from the immediate-context OMSetRenderTargets detour.
        // Exact FO4VR G-buffer identity is checked before a private seventh
        // target is appended. The engine-owned six views are never retained.
        [[nodiscard]] GBufferBinding prepareGBufferBinding(
            ID3D11DeviceContext* context,
            UINT renderTargetCount,
            ID3D11RenderTargetView* const* renderTargets) noexcept;
        // Called after the exact directional-light consumer and any dependent
        // post-light pass finish sampling the class texture. The next accepted
        // world G-buffer bind clears exactly once; unrelated capture-target
        // transitions do not affect this lifetime.
        void markWorldFrameConsumed() noexcept;

        // Render-thread only. The returned view remains owned by this
        // process-lifetime runtime and is used only inside one draw scope.
        [[nodiscard]] ID3D11ShaderResourceView* shaderResourceView() const
            noexcept;
        [[nodiscard]] ID3D11ShaderResourceView*
            pbrMaterialShaderResourceView() const noexcept;

        // Called only when a material replacement is actually selected. These
        // fixed counters prove which producer classes reach the G-buffer
        // without GPU readback or allocations in the draw hot path.
        void recordProducerSelection(
            std::uint32_t classCode,
            std::uint32_t contractIndex,
            ProducerEvidence evidence,
            std::uint32_t descriptor) noexcept;
        void recordDescriptorObservation(
            std::uint32_t classCode,
            std::uint32_t descriptor) noexcept;
        void recordDescriptorObservationMiss(
            std::uint32_t descriptor) noexcept;
        void recordDescriptorScopeMiss() noexcept;
        void recordDescriptorContractMiss(std::uint32_t descriptor) noexcept;

        [[nodiscard]] RuntimeSnapshot snapshot() const noexcept;

    private:
        Runtime() = default;

        [[nodiscard]] bool matchesGBuffer(
            UINT renderTargetCount,
            ID3D11RenderTargetView* const* renderTargets,
            D3D11_TEXTURE2D_DESC& sourceDescription) const noexcept;
        void logCandidateRejectionOnce(
            UINT renderTargetCount,
            ID3D11RenderTargetView* const* renderTargets) noexcept;
        [[nodiscard]] bool ensureTarget(
            const D3D11_TEXTURE2D_DESC& sourceDescription) noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pbrMaterialTexture_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>
            pbrMaterialRenderTargetView_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
            pbrMaterialShaderResourceView_;
        UINT width_{};
        UINT height_{};
        UINT sampleCount_{};
        bool worldFrameConsumed_{ true };
        std::atomic_uint32_t consumerMask_{};
        std::atomic_bool pbrMaterialTransportEnabled_{};
        std::atomic_uint64_t acceptedGBufferBinds_{};
        std::atomic_uint64_t rejectedGBufferBinds_{};
        std::atomic_uint64_t targetRebuilds_{};
        std::atomic_uint64_t targetClears_{};
        std::array<std::atomic_uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            producerSelections_{};
        std::array<std::atomic_uint64_t,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            descriptorObservations_{};
        std::atomic_uint64_t descriptorObservationMisses_{};
        std::atomic_uint64_t descriptorScopeMisses_{};
        std::atomic_uint64_t descriptorContractMisses_{};
        std::atomic_uint64_t failures_{};
        std::atomic_bool firstAcceptedBindingLogged_{};
        std::atomic_bool firstCandidateRejectionLogged_{};
        std::array<std::atomic_bool,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            firstProducerSelectionLogged_{};
        std::array<std::atomic_bool,
            static_cast<std::size_t>(SurfaceClassCode::count)>
            firstDescriptorObservationLogged_{};
        std::atomic_bool firstDescriptorObservationMissLogged_{};
        std::atomic_bool firstDescriptorScopeMissLogged_{};
        std::atomic_bool firstDescriptorContractMissLogged_{};
    };
}
