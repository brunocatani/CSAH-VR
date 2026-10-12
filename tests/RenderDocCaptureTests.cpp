#include "diagnostics/RenderDocCapture.h"
#include "renderdoc_app.h"
#include "render/D3D11MethodOwner.h"
#include <MinHook.h>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    }

    template<unsigned Id, class R, class... Args>
    struct MethodProbe
    {
        using Function = R(STDMETHODCALLTYPE*)(Args...);
        static inline Function original{};
        static inline unsigned calls{};
        static R STDMETHODCALLTYPE hook(Args... args)
        {
            ++calls;
            return original(args...);
        }
        void* target{};
        explicit MethodProbe(void* method) : target(method)
        {
            require(csah::render::isSupportedD3D11Method(target), "supported graphics method rejected");
            calls = 0;
            void* trampoline{};
            require(MH_CreateHook(target, reinterpret_cast<void*>(&hook), &trampoline) == MH_OK,
                "graphics method detour creation failed");
            original = reinterpret_cast<Function>(trampoline);
            require(MH_EnableHook(target) == MH_OK, "graphics method detour activation failed");
        }
        ~MethodProbe() { MH_DisableHook(target); MH_RemoveHook(target); }
    };

    void verifyGraphicsMethods(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        using Microsoft::WRL::ComPtr;
        auto** devices = *reinterpret_cast<void***>(device);
        auto** contexts = *reinterpret_cast<void***>(context);
        for (const auto slot : {12, 15, 18})
            require(csah::render::isSupportedD3D11Method(devices[slot]), "shader creation identity rejected");
        for (const auto slot : {9, 11, 12, 13, 14, 15, 20, 21, 33, 34, 41, 42})
            require(csah::render::isSupportedD3D11Method(contexts[slot]), "context method identity rejected");
        require(!csah::render::isSupportedD3D11Method(nullptr) &&
            !csah::render::isSupportedD3D11Method(&device) &&
            !csah::render::isSupportedD3D11Method(reinterpret_cast<void*>(&GetCurrentProcessId)) &&
            !csah::render::isSupportedD3D11Method(reinterpret_cast<void*>(&verifyGraphicsMethods)),
            "unknown or non-executable method accepted");
        require(MH_Initialize() == MH_OK, "MinHook initialization failed");
        {
            MethodProbe<0, HRESULT, ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**> create(devices[18]);
            MethodProbe<1, void, ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT> bind(contexts[9]);
            MethodProbe<2, HRESULT, ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*> map(contexts[14]);
            MethodProbe<3, void, ID3D11DeviceContext*, ID3D11Resource*, UINT> unmap(contexts[15]);
            MethodProbe<4, void, ID3D11DeviceContext*, UINT, UINT, UINT> dispatch(contexts[41]);
            MethodProbe<5, HRESULT, ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**> createVertex(devices[12]);
            MethodProbe<6, HRESULT, ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**> createPixel(devices[15]);
            MethodProbe<7, void, ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT> bindVertex(contexts[11]);
            MethodProbe<8, void, ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*> targets(contexts[33]);
            MethodProbe<9, void, ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*> targetsAndUavs(contexts[34]);
            MethodProbe<10, void, ID3D11DeviceContext*, UINT, UINT, INT> drawIndexed(contexts[12]);
            MethodProbe<11, void, ID3D11DeviceContext*, UINT, UINT> draw(contexts[13]);
            MethodProbe<12, void, ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT> drawIndexedInstanced(contexts[20]);
            MethodProbe<13, void, ID3D11DeviceContext*, UINT, UINT, UINT, UINT> drawInstanced(contexts[21]);
            MethodProbe<14, void, ID3D11DeviceContext*, ID3D11Buffer*, UINT> dispatchIndirect(contexts[42]);
            constexpr char shader[] = "RWStructuredBuffer<uint> result:register(u0); [numthreads(1,1,1)] void main(){result[0]=73;}";
            ComPtr<ID3DBlob> code, errors;
            require(SUCCEEDED(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)), "fixture compilation failed");
            ComPtr<ID3D11ComputeShader> compute;
            require(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &compute)), "wrapped shader creation failed");
            constexpr char vertexCode[] = "float4 main(uint id:SV_VertexID):SV_Position{return float4(0,0,0,1);}";
            require(SUCCEEDED(D3DCompile(vertexCode, sizeof(vertexCode)-1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &errors)), "vertex fixture compilation failed");
            ComPtr<ID3D11VertexShader> vertex;
            require(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex)), "wrapped vertex creation failed");
            constexpr char pixelCode[] = "float4 main():SV_Target{return 1;}";
            require(SUCCEEDED(D3DCompile(pixelCode, sizeof(pixelCode)-1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &code, &errors)), "pixel fixture compilation failed");
            ComPtr<ID3D11PixelShader> pixel;
            require(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel)), "wrapped pixel creation failed");
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = 4; desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = 4;
            ComPtr<ID3D11Buffer> buffer, staging;
            require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)), "fixture buffer failed");
            ComPtr<ID3D11UnorderedAccessView> output;
            require(SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(), nullptr, &output)), "fixture output failed");
            desc.BindFlags = 0; desc.MiscFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &staging)), "fixture staging failed");
            context->PSSetShader(pixel.Get(), nullptr, 0);
            context->VSSetShader(vertex.Get(), nullptr, 0);
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(0, 0); context->DrawIndexed(0, 0, 0);
            context->DrawInstanced(0, 0, 0, 0); context->DrawIndexedInstanced(0, 0, 0, 0, 0);
            context->CSSetShader(compute.Get(), nullptr, 0);
            auto* view = output.Get(); context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
            context->Dispatch(1, 1, 1);
            D3D11_BUFFER_DESC indirectDesc{};
            indirectDesc.ByteWidth = 12; indirectDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            const UINT arguments[]{1, 1, 1}; const D3D11_SUBRESOURCE_DATA initial{arguments};
            ComPtr<ID3D11Buffer> indirect;
            require(SUCCEEDED(device->CreateBuffer(&indirectDesc, &initial, &indirect)), "indirect fixture failed");
            context->DispatchIndirect(indirect.Get(), 0);
            view = nullptr; context->CSSetUnorderedAccessViews(0, 1, &view, nullptr);
            context->CopyResource(staging.Get(), buffer.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "wrapped readback failed");
            const auto result = *static_cast<const UINT*>(mapped.pData);
            context->Unmap(staging.Get(), 0);
            require(result == 73, "detoured graphics methods changed GPU output");
            require(create.calls && bind.calls && map.calls && unmap.calls && dispatch.calls, "graphics calls bypassed installed detours");
            require(createVertex.calls && createPixel.calls && bindVertex.calls && targets.calls && targetsAndUavs.calls &&
                draw.calls && drawIndexed.calls && drawInstanced.calls && drawIndexedInstanced.calls && dispatchIndirect.calls,
                "draw or resource calls bypassed installed detours");
            context->ClearState();
        }
        require(MH_Uninitialize() == MH_OK, "MinHook cleanup failed");
    }
    const void* creationImport()
    {
        const auto* base=reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const auto* imports=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
        for(UINT i=0;i<directory.Size/sizeof(*imports) && imports[i].Name;++i) {
            if(_stricmp(reinterpret_cast<const char*>(base+imports[i].Name),"d3d11.dll")!=0)continue;
            const auto* names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+imports[i].OriginalFirstThunk);
            const auto* slots=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+imports[i].FirstThunk);
            for(UINT n=0;names[n].u1.AddressOfData;++n) {
                if(IMAGE_SNAP_BY_ORDINAL64(names[n].u1.Ordinal))continue;
                const auto* name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names[n].u1.AddressOfData);
                if(std::strcmp(name->Name,"D3D11CreateDeviceAndSwapChain")==0)
                    return reinterpret_cast<const void*>(slots[n].u1.Function);
            }
        }
        return nullptr;
    }
}

int main(int count, char** args)
{
    if (count != 3) return 2;
    try {
        const auto directory = std::filesystem::absolute(args[1]);
        const auto dll = std::filesystem::absolute(args[2]);
        {
            Microsoft::WRL::ComPtr<ID3D11Device> native;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> nativeContext;
            require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, &native, nullptr, &nativeContext)), "native fixture device failed");
            verifyGraphicsMethods(native.Get(), nativeContext.Get());
        }
        require(!csah::diagnostics::initializeRenderDocCapture(L"relative/renderdoc.dll"), "relative backend path accepted");
        require(!csah::diagnostics::isRenderDocCreationHook(reinterpret_cast<void*>(&GetCurrentProcessId)), "unrelated module accepted as capture hook");
        require(csah::diagnostics::initializeRenderDocCapture(dll.c_str()), "startup backend failed");
        HMODULE module = GetModuleHandleW(L"renderdoc.dll");
        require(module != nullptr, "startup backend was not loaded");
        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        RENDERDOC_API_1_1_2* api{};
        require(getApi && getApi(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&api)) == 1 && api,
            "RenderDoc API unavailable");
        require(csah::diagnostics::isRenderDocCreationHook(reinterpret_cast<void*>(getApi)), "capture module identity failed");
        const auto creation=creationImport();
        const auto nativeCreation=reinterpret_cast<const void*>(GetProcAddress(
            GetModuleHandleW(L"d3d11.dll"),"D3D11CreateDeviceAndSwapChain"));
        // RenderDoc may detour the native export in place or replace the IAT.
        require(creation==nativeCreation || csah::diagnostics::isRenderDocCreationHook(creation),
            "startup backend did not preserve an accepted D3D creation predecessor");
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
        require(csah::diagnostics::isRenderDocCreationHook((*reinterpret_cast<void***>(device.Get()))[18]),
            "fixture did not return a RenderDoc-wrapped device");
        verifyGraphicsMethods(device.Get(), context.Get());
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
        std::filesystem::create_directories(directory);
        const auto settings=directory/"fixture.ini";
        {std::ofstream out(settings);out<<"[Skylighting]\nbEnabled=1\n";}
        require(csah::diagnostics::requestRenderDocCapture(directory.c_str(),settings.c_str()), "manual request failed");
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
        bool matchingSettings=false;
        for(const auto& entry:std::filesystem::directory_iterator(directory)) {
            const auto name=entry.path().filename().string();
            const std::string suffix=".settings.ini";
            if(name.ends_with(suffix) && capture.filename().string().starts_with(name.substr(0,name.size()-suffix.size()))) {
                std::ifstream input(entry.path());const std::string contents((std::istreambuf_iterator<char>(input)),{});
                matchingSettings=contents=="[Skylighting]\nbEnabled=1\n";
            }
        }
        require(matchingSettings,"captured frame has no matching settings sidecar");
        for (int i = 0; i < 6; ++i) frame();
        require(api->GetNumCaptures() == initial + 1, "capture repeated without a request");
        output.Reset(); texture.Reset(); swapChain.Reset(); context.Reset(); device.Reset();
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        std::cout << "Manual RenderDoc capture saved: " << capture.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
