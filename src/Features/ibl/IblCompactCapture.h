#pragma once

#include "render/GpuTimingProfiler.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstddef>

namespace csah::ibl
{
    // Render-thread-owned native lighting evaluator. The native raster draw
    // supplies an R8 mask; all HDR storage scales with cubemap resolution.
    class CompactCapture final
    {
    public:
        struct Bytecode { const void* data{}; std::size_t size{}; };
        static constexpr std::size_t kShaderCount = 41;
        [[nodiscard]] bool initialize(ID3D11Device* device,
            const std::array<Bytecode, kShaderCount>& shaders, UINT extent) noexcept;
        void reset() noexcept;
        [[nodiscard]] bool dispatch(ID3D11DeviceContext* context,
            ID3D11ShaderResourceView* coverage, std::size_t shaderIndex) noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* radiance() const noexcept { return radianceView_.Get(); }
    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>, kShaderCount> shaders_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> radiance_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> radianceView_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> radianceOutput_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        UINT extent_{};
        render::GpuTimingProfiler timing_;
        bool timingAttempted_{};
    };
}
