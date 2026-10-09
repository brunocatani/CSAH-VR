#pragma once

#include "Features/dlaa/DlaaSettings.h"

#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstdint>

namespace csah::dlaa
{
    struct StreamlineSnapshot
    {
        bool initialized{};
        bool deviceBound{};
        bool swapChainUpgraded{};
        bool featureLoaded{};
        bool featureSupported{};
        bool featureFunctionsBound{};
        std::uint32_t initializationResult{};
        std::uint32_t deviceResult{};
        std::uint32_t supportResult{};
    };

    struct StreamlineOptimalSettings
    {
        bool valid{};
        std::uint32_t renderWidth{};
        std::uint32_t renderHeight{};
        std::uint32_t minimumWidth{};
        std::uint32_t minimumHeight{};
        std::uint32_t maximumWidth{};
        std::uint32_t maximumHeight{};
    };

    struct StreamlineMatrix
    {
        std::array<float, 16> values{};
    };

    struct StreamlineEyeConstants
    {
        StreamlineMatrix cameraViewToClip{};
        StreamlineMatrix currentViewProjection{};
        StreamlineMatrix previousViewProjection{};
        std::array<float, 3> cameraRight{};
        std::array<float, 3> cameraUp{};
        std::array<float, 3> cameraForward{};
        std::array<float, 3> cameraPosition{};
        float cameraNear{};
        float cameraFar{};
        float cameraFovRadians{};
        float cameraAspectRatio{};
        float jitterPixelsX{};
        float jitterPixelsY{};
        float motionVectorScaleX{ 1.0f };
        float motionVectorScaleY{ 1.0f };
        bool depthInverted{};
        bool reset{};
    };

    struct StreamlineEyeResources
    {
        ID3D11Texture2D* colorInput{};
        ID3D11Texture2D* colorOutput{};
        ID3D11Texture2D* depth{};
        ID3D11Texture2D* motionVectors{};
        ID3D11Texture2D* biasCurrentColor{};
        std::uint32_t inputLeft{};
        std::uint32_t inputTop{};
    };

    struct StreamlineDlaaFrame
    {
        ID3D11DeviceContext* context{};
        std::array<ID3D11Query*, 2> timingAfterEye{};
        std::uint32_t frameIndex{};
        std::uint32_t inputWidth{};
        std::uint32_t inputHeight{};
        std::uint32_t outputWidth{};
        std::uint32_t outputHeight{};
        Mode mode{ Mode::dlaa };
        ModelPreset modelPreset{ ModelPreset::qualityK };
        std::array<StreamlineEyeConstants, 2> constants{};
        std::array<StreamlineEyeResources, 2> resources{};
    };

    // Runs from the verified renderer interception boundary immediately before
    // Fallout4VR invokes D3D11CreateDeviceAndSwapChain. It must not run from
    // F4SEPlugin_Load because production Streamline performs nested authenticated
    // DLL loading during slInit.
    [[nodiscard]] bool initializeStreamlineBeforeDevice() noexcept;

    // Runs immediately after native D3D11 creation and before the returned
    // swapchain is exposed to Fallout4VR. Failure disables DLAA only.
    [[nodiscard]] bool bindStreamlineDeviceAndSwapChain(
        IDXGIAdapter* adapter,
        ID3D11Device* device,
        IDXGISwapChain** swapChain) noexcept;

    // Evaluates both stereo viewports from qualified packed input subrects
    // into private per-eye outputs using one frame token. The caller owns
    // publication and must commit neither eye if this transaction returns
    // false.
    [[nodiscard]] bool evaluateStreamlineDlaa(
        const StreamlineDlaaFrame& frame) noexcept;

    // Queries the DLSS plugin's authoritative input size for one output eye.
    // DLAA and center-DLAA are native resolution and return the output size.
    [[nodiscard]] StreamlineOptimalSettings queryStreamlineOptimalSettings(
        Mode mode,
        std::uint32_t outputWidth,
        std::uint32_t outputHeight) noexcept;

    // Drops NGX temporal/resource state for both fixed FO4VR eye viewports.
    void releaseStreamlineDlaaResources() noexcept;

    [[nodiscard]] StreamlineSnapshot streamlineSnapshot() noexcept;
}
