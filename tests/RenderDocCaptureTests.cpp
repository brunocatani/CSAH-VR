#include "diagnostics/RenderDocCapture.h"
#include "renderdoc_app.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    }
}

int main(int count, char** args)
{
    if (count != 3) return 2;
    try {
        const auto directory = std::filesystem::absolute(args[1]);
        const auto dll = std::filesystem::absolute(args[2]);
        HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(module != nullptr, "could not load installed RenderDoc for the standalone test");
        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        RENDERDOC_API_1_1_2* api{};
        require(getApi && getApi(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&api)) == 1 && api,
            "RenderDoc API unavailable");
        api->SetCaptureKeys(nullptr, 0);
        api->SetFocusToggleKeys(nullptr, 0);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CSAHRenderDocTest";
        require(RegisterClassW(&wc) != 0, "window class registration failed");
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"CSAH RenderDoc Test", WS_POPUP,
            0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(window != nullptr, "hidden window creation failed");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = desc.BufferDesc.Height = 64;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, &context)), "test D3D11 device failed");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> output;
        require(SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&texture))) &&
            SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &output)), "test output failed");
        api->SetActiveWindow(device.Get(), window);
        const auto initial = api->GetNumCaptures();
        const auto frame = [&] {
            constexpr float color[]{0.1F, 0.3F, 0.7F, 1.0F};
            context->ClearRenderTargetView(output.Get(), color);
            require(SUCCEEDED(swapChain->Present(0, 0)), "test present failed");
            csah::diagnostics::processRenderDocCapture();
        };
        require(!csah::diagnostics::requestRenderDocCapture(L""), "empty output path accepted");
        require(!csah::diagnostics::requestRenderDocCapture(L"relative"), "relative output path accepted");
        for (int i = 0; i < 3; ++i) frame();
        require(api->GetNumCaptures() == initial, "idle processing triggered a capture");
        require(csah::diagnostics::requestRenderDocCapture(directory.c_str()), "manual request failed");
        require(!csah::diagnostics::requestRenderDocCapture(directory.c_str()), "duplicate pending request accepted");
        csah::diagnostics::processRenderDocCapture();
        for (int i = 0; i < 6; ++i) frame();
        require(api->GetNumCaptures() == initial + 1, "manual request did not capture exactly one frame");
        std::uint32_t length{};
        require(api->GetCapture(initial, nullptr, &length, nullptr) == 1 && length < 4096, "capture path unavailable");
        std::string capturePath(length + 1, '\0');
        require(api->GetCapture(initial, capturePath.data(), nullptr, nullptr) == 1, "capture path read failed");
        const std::filesystem::path capture(capturePath.c_str());
        require(capture.parent_path() == directory && std::filesystem::file_size(capture) > 0,
            "capture was not saved to the explicit test directory");
        for (int i = 0; i < 6; ++i) frame();
        require(api->GetNumCaptures() == initial + 1, "capture repeated without a request");
        output.Reset(); texture.Reset(); swapChain.Reset(); context.Reset(); device.Reset();
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        FreeLibrary(module);
        std::cout << "Manual RenderDoc capture saved: " << capture.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
