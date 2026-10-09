#include "Features/skylighting/SkylightingRuntime.h"
#include "Features/skylighting/SkylightingNativeHooks.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

// This executable exercises resource ownership, not engine hooks. Reaching a
// native render transaction is a test failure, including in release builds.
namespace csah::skylighting
{
    ScopedOcclusionPassProduction::ScopedOcclusionPassProduction(std::uint32_t) noexcept { std::abort(); }
    ScopedOcclusionPassProduction::~ScopedOcclusionPassProduction() noexcept = default;
    bool ScopedOcclusionPassProduction::active() const noexcept { std::abort(); }
}

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void run()
    {
        using namespace csah::skylighting;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        constexpr D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP initialization failed");
        auto& runtime = Runtime::get();
        Settings settings{.enabled = false, .quality = Quality::low};
        runtime.applySettings(settings);
        runtime.onDeviceCreated(device.Get(), context.Get());
        require(!runtime.snapshot().gpuResourcesReady, "disabled startup allocated probe resources");
        runtime.onNativePrecipitationFrame(nullptr, nullptr, nullptr);
        require(!runtime.snapshot().gpuResourcesReady, "disabled callback allocated probes");
        settings.enabled = true;
        runtime.applySettings(settings);
        runtime.onNativePrecipitationFrame(nullptr, nullptr, nullptr);
        const auto enabled = runtime.snapshot();
        require(enabled.gpuResourcesReady, "first enabled render callback did not allocate probes");
        require(enabled.probeWidth == 64 && enabled.probeHeight == 64 && enabled.probeDepth == 32,
            "lazy allocation changed the selected resolution");
        require(!enabled.probeDataValid && !enabled.exteriorActive && enabled.probeDispatches == 0,
            "unobserved probes became eligible for shading");
        settings.enabled = false;
        runtime.applySettings(settings);
        runtime.onNativePrecipitationFrame(nullptr, nullptr, nullptr);
        require(runtime.snapshot().gpuResourcesReady, "disable discarded reusable resources");
        runtime.onDeviceCreated(device.Get(), context.Get());
        require(!runtime.snapshot().gpuResourcesReady, "device reset retained disabled probe resources");
        runtime.onDeviceCreated(nullptr, nullptr);
        settings.enabled = true;
        runtime.applySettings(settings);
        runtime.onNativePrecipitationFrame(nullptr, nullptr, nullptr);
        require(!runtime.snapshot().gpuResourcesReady, "missing device reused an old resource owner");
        runtime.onDeviceCreated(device.Get(), context.Get());
        require(runtime.snapshot().gpuResourcesReady, "enabled device initialization failed");
        runtime.onDeviceCreated(nullptr, nullptr);
        context->ClearState();
    }
}

int main()
{
    try {
        run();
        std::cout << "Skylighting disabled startup, first enable, disable and device replacement passed on WARP\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
