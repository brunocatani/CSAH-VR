#pragma once

#include "render/Fo4VrCamera.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace csah::render
{
    // Render-thread-only staging. A map is merely a candidate: only a draw
    // using that buffer in the current world frame can publish the value.
    // Later UI/offscreen maps cannot alter the already copied world snapshot.
    class WorldCameraCapture final
    {
    public:
        void observe(ID3D11Buffer* buffer, const Fo4VrFrameBuffer& value, std::uint64_t frame) noexcept
        {
            if (buffer_.Get() != buffer) buffer_ = buffer;
            candidate_ = value;
            frame_ = frame;
        }

        [[nodiscard]] bool confirm(ID3D11Buffer* bound, std::uint64_t frame, Fo4VrFrameBuffer& output) const noexcept
        {
            if (!bound || bound != buffer_.Get() || frame != frame_) return false;
            output = candidate_;
            return true;
        }

        void reset() noexcept
        {
            buffer_.Reset();
            candidate_ = {};
            frame_ = 0;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
        Fo4VrFrameBuffer candidate_{};
        std::uint64_t frame_{};
    };
}
