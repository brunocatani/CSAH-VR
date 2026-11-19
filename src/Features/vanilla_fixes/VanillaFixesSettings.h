#pragma once

#include <cstdint>

namespace csah::vanilla_fixes
{
    enum class DirectionalLightDiagnosticMode : std::uint8_t
    {
        off = 0,
        normalLightDot = 1,
        normalViewDot = 2,
        shadowVisibility = 3,
        suppliedLightVector = 4,
        viewLightVector = 5,
        correctedNormalLightDot = 6,
        coverageNoDepthStencil = 7,
        coverageFullRaster = 8,
        coverageSyntheticProducer = 9,
        coverageSyntheticComposite = 10,
        finalOutputPresentation = 11,
        directDiffuseOnly = 12,
        directSpecularOnly = 13,
        screenSpaceReflectionOnly = 14,
        cubemapLookupOnly = 15,
    };

    [[nodiscard]] constexpr bool isLightingOwnershipDiagnostic(
        const DirectionalLightDiagnosticMode mode) noexcept
    {
        return mode >= DirectionalLightDiagnosticMode::directDiffuseOnly &&
            mode <= DirectionalLightDiagnosticMode::cubemapLookupOnly;
    }

    struct Settings final
    {
        bool enabled{ true };
        bool precipitationOcclusion{ true };
        bool imageSpaceModifiers{ true };
        // Not a setting: the game's own ambient occlusion runs exactly while
        // Screen Space GI, which replaces it, is off (loadSettings).
        bool sao{ true };
        bool screenSpaceReflections{ true };
        bool nativeScreenSpaceMaterialPipeline{ true };
        bool lensFlare{ true };
        bool focusShadows{ true };
        bool stereoSunOcclusion{ true };
        DirectionalLightDiagnosticMode directionalLightDiagnosticMode{
            DirectionalLightDiagnosticMode::off
        };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
}
