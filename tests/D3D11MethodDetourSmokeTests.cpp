#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace
{
    using CreatePixelShaderFunction = HRESULT(STDMETHODCALLTYPE*)(
        ID3D11Device*,
        const void*,
        SIZE_T,
        ID3D11ClassLinkage*,
        ID3D11PixelShader**);
    using PSSetShaderFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*,
        ID3D11PixelShader*,
        ID3D11ClassInstance* const*,
        UINT);
    using OMSetRenderTargetsFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*,
        UINT,
        ID3D11RenderTargetView* const*,
        ID3D11DepthStencilView*);
    using OMSetRenderTargetsAndUnorderedAccessViewsFunction =
        void(STDMETHODCALLTYPE*)(
            ID3D11DeviceContext*,
            UINT,
            ID3D11RenderTargetView* const*,
            ID3D11DepthStencilView*,
            UINT,
            UINT,
            ID3D11UnorderedAccessView* const*,
            const UINT*);
    using DispatchFunction=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);
    using DispatchIndirectFunction=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
    using DrawIndexedFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*, UINT, UINT, INT);
    using DrawFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*, UINT, UINT);
    using DrawIndexedInstancedFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
    using DrawInstancedFunction = void(STDMETHODCALLTYPE*)(
        ID3D11DeviceContext*, UINT, UINT, UINT, UINT);

    constexpr std::size_t kCreatePixelShaderVtableIndex = 15;
    constexpr std::size_t kPSSetShaderVtableIndex = 9;
    constexpr std::size_t kOMSetRenderTargetsVtableIndex = 33;
    constexpr std::size_t
        kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex = 34;
    constexpr std::size_t kDrawIndexedVtableIndex = 12;
    constexpr std::size_t kDrawVtableIndex = 13;
    constexpr std::size_t kDrawIndexedInstancedVtableIndex = 20;
    constexpr std::size_t kDrawInstancedVtableIndex = 21;

    CreatePixelShaderFunction originalCreatePixelShader{};
    PSSetShaderFunction originalPSSetShader{};
    OMSetRenderTargetsFunction originalOMSetRenderTargets{};
    OMSetRenderTargetsAndUnorderedAccessViewsFunction
        originalOMSetRenderTargetsAndUnorderedAccessViews{};
    DispatchFunction originalDispatch{};
    DispatchIndirectFunction originalDispatchIndirect{};
    std::atomic_uint64_t dispatchCalls{},dispatchIndirectCalls{};
    DrawIndexedFunction originalDrawIndexed{};
    DrawFunction originalDraw{};
    DrawIndexedInstancedFunction originalDrawIndexedInstanced{};
    DrawInstancedFunction originalDrawInstanced{};
    std::atomic_uint64_t createCalls{};
    std::atomic_uint64_t bindCalls{};
    std::atomic_uint64_t renderTargetCalls{};
    std::atomic_uint64_t renderTargetAndUnorderedAccessCalls{};
    std::atomic_uint64_t drawIndexedCalls{};
    std::atomic_uint64_t drawCalls{};
    std::atomic_uint64_t drawIndexedInstancedCalls{};
    std::atomic_uint64_t drawInstancedCalls{};

    HRESULT STDMETHODCALLTYPE hookCreatePixelShader(
        ID3D11Device* device,
        const void* bytecode,
        SIZE_T bytecodeLength,
        ID3D11ClassLinkage* classLinkage,
        ID3D11PixelShader** shader) noexcept
    {
        createCalls.fetch_add(1, std::memory_order_relaxed);
        return originalCreatePixelShader(
            device,
            bytecode,
            bytecodeLength,
            classLinkage,
            shader);
    }

    void STDMETHODCALLTYPE hookPSSetShader(
        ID3D11DeviceContext* context,
        ID3D11PixelShader* shader,
        ID3D11ClassInstance* const* classInstances,
        UINT classInstanceCount) noexcept
    {
        bindCalls.fetch_add(1, std::memory_order_relaxed);
        originalPSSetShader(
            context,
            shader,
            classInstances,
            classInstanceCount);
    }

    void STDMETHODCALLTYPE hookOMSetRenderTargets(
        ID3D11DeviceContext* context,
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthStencil) noexcept
    {
        renderTargetCalls.fetch_add(1, std::memory_order_relaxed);
        originalOMSetRenderTargets(
            context,
            renderTargetCount,
            renderTargets,
            depthStencil);
    }

    void STDMETHODCALLTYPE hookOMSetRenderTargetsAndUnorderedAccessViews(
        ID3D11DeviceContext* context,
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthStencil,
        UINT unorderedAccessStartSlot,
        UINT unorderedAccessViewCount,
        ID3D11UnorderedAccessView* const* unorderedAccessViews,
        const UINT* initialCounts) noexcept
    {
        renderTargetAndUnorderedAccessCalls.fetch_add(
            1,
            std::memory_order_relaxed);
        originalOMSetRenderTargetsAndUnorderedAccessViews(
            context,
            renderTargetCount,
            renderTargets,
            depthStencil,
            unorderedAccessStartSlot,
            unorderedAccessViewCount,
            unorderedAccessViews,
            initialCounts);
    }

    void STDMETHODCALLTYPE hookDrawIndexed(
        ID3D11DeviceContext* context,
        UINT indexCount,
        UINT startIndexLocation,
        INT baseVertexLocation) noexcept
    {
        drawIndexedCalls.fetch_add(1, std::memory_order_relaxed);
        originalDrawIndexed(
            context,
            indexCount,
            startIndexLocation,
            baseVertexLocation);
    }

    void STDMETHODCALLTYPE hookDraw(
        ID3D11DeviceContext* context,
        UINT vertexCount,
        UINT startVertexLocation) noexcept
    {
        drawCalls.fetch_add(1, std::memory_order_relaxed);
        originalDraw(context, vertexCount, startVertexLocation);
    }

    void STDMETHODCALLTYPE hookDrawIndexedInstanced(
        ID3D11DeviceContext* context,
        UINT indexCountPerInstance,
        UINT instanceCount,
        UINT startIndexLocation,
        INT baseVertexLocation,
        UINT startInstanceLocation) noexcept
    {
        drawIndexedInstancedCalls.fetch_add(1, std::memory_order_relaxed);
        originalDrawIndexedInstanced(
            context,
            indexCountPerInstance,
            instanceCount,
            startIndexLocation,
            baseVertexLocation,
            startInstanceLocation);
    }

    void STDMETHODCALLTYPE hookDrawInstanced(
        ID3D11DeviceContext* context,
        UINT vertexCountPerInstance,
        UINT instanceCount,
        UINT startVertexLocation,
        UINT startInstanceLocation) noexcept
    {
        drawInstancedCalls.fetch_add(1, std::memory_order_relaxed);
        originalDrawInstanced(
            context,
            vertexCountPerInstance,
            instanceCount,
            startVertexLocation,
            startInstanceLocation);
    }

    void STDMETHODCALLTYPE hookDispatch(ID3D11DeviceContext* context,UINT x,UINT y,UINT z) noexcept
    {++dispatchCalls;originalDispatch(context,x,y,z);}
    void STDMETHODCALLTYPE hookDispatchIndirect(ID3D11DeviceContext* context,ID3D11Buffer* args,UINT offset) noexcept
    {++dispatchIndirectCalls;originalDispatchIndirect(context,args,offset);}

    [[nodiscard]] bool belongsToD3D11(const void* address) noexcept
    {
        HMODULE owner{};
        return address && GetModuleHandleExA(
                              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(address),
                              &owner) &&
            owner == GetModuleHandleW(L"d3d11.dll");
    }

    void reportFailure(const char* message)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
}

int run(D3D_DRIVER_TYPE driver)
{
    createCalls=bindCalls=renderTargetCalls=renderTargetAndUnorderedAccessCalls=drawIndexedCalls=drawCalls=drawIndexedInstancedCalls=drawInstancedCalls=dispatchCalls=dispatchIndirectCalls=0;
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    D3D_FEATURE_LEVEL featureLevel{};
    const auto createDeviceResult = D3D11CreateDevice(
        nullptr,
        driver,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);
    if (FAILED(createDeviceResult) || !device || !context) {
        reportFailure("D3D11 WARP device creation failed");
        return EXIT_FAILURE;
    }

    auto** deviceVtable = *reinterpret_cast<void***>(device);
    auto** contextVtable = *reinterpret_cast<void***>(context);
    auto* createTarget = deviceVtable[kCreatePixelShaderVtableIndex];
    auto* bindTarget = contextVtable[kPSSetShaderVtableIndex];
    auto* renderTargetTarget =
        contextVtable[kOMSetRenderTargetsVtableIndex];
    auto* renderTargetAndUnorderedAccessTarget = contextVtable[
        kOMSetRenderTargetsAndUnorderedAccessViewsVtableIndex];
    auto* drawIndexedTarget = contextVtable[kDrawIndexedVtableIndex];
    auto* drawTarget = contextVtable[kDrawVtableIndex];
    auto* drawIndexedInstancedTarget =
        contextVtable[kDrawIndexedInstancedVtableIndex];
    auto* drawInstancedTarget = contextVtable[kDrawInstancedVtableIndex];
    const std::array<void*, 10> targets{
        createTarget,
        bindTarget,
        renderTargetTarget,
        renderTargetAndUnorderedAccessTarget,
        drawIndexedTarget,
        drawTarget,
        drawIndexedInstancedTarget,
        drawInstancedTarget,
        contextVtable[41],contextVtable[42],
    };
    for (const auto target : targets) {
        if (belongsToD3D11(target)) {
            continue;
        }
        reportFailure("live shader/draw methods are not owned by d3d11.dll");
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    auto status = MH_Initialize();
    if (status != MH_OK) {
        reportFailure("MinHook initialization failed");
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    const std::array<void*, 10> detours{
        reinterpret_cast<void*>(&hookCreatePixelShader),
        reinterpret_cast<void*>(&hookPSSetShader),
        reinterpret_cast<void*>(&hookOMSetRenderTargets),
        reinterpret_cast<void*>(
            &hookOMSetRenderTargetsAndUnorderedAccessViews),
        reinterpret_cast<void*>(&hookDrawIndexed),
        reinterpret_cast<void*>(&hookDraw),
        reinterpret_cast<void*>(&hookDrawIndexedInstanced),
        reinterpret_cast<void*>(&hookDrawInstanced),
        reinterpret_cast<void*>(&hookDispatch),reinterpret_cast<void*>(&hookDispatchIndirect),
    };
    std::array<void*, 10> trampolines{};
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (MH_CreateHook(
                targets[index],
                detours[index],
                &trampolines[index]) == MH_OK &&
            trampolines[index]) {
            continue;
        }
        reportFailure("D3D11 method prologue relocation failed");
        (void)MH_Uninitialize();
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }
    originalCreatePixelShader =
        reinterpret_cast<CreatePixelShaderFunction>(trampolines[0]);
    originalPSSetShader = reinterpret_cast<PSSetShaderFunction>(trampolines[1]);
    originalOMSetRenderTargets =
        reinterpret_cast<OMSetRenderTargetsFunction>(trampolines[2]);
    originalOMSetRenderTargetsAndUnorderedAccessViews = reinterpret_cast<
        OMSetRenderTargetsAndUnorderedAccessViewsFunction>(trampolines[3]);
    originalDrawIndexed = reinterpret_cast<DrawIndexedFunction>(trampolines[4]);
    originalDraw = reinterpret_cast<DrawFunction>(trampolines[5]);
    originalDrawIndexedInstanced =
        reinterpret_cast<DrawIndexedInstancedFunction>(trampolines[6]);
    originalDrawInstanced =
        reinterpret_cast<DrawInstancedFunction>(trampolines[7]);

    originalDispatch=reinterpret_cast<DispatchFunction>(trampolines[8]);
    originalDispatchIndirect=reinterpret_cast<DispatchIndirectFunction>(trampolines[9]);
    bool queueSucceeded = true;
    for (const auto target : targets) {
        queueSucceeded = MH_QueueEnableHook(target) == MH_OK &&
            queueSucceeded;
    }
    status = queueSucceeded ? MH_ApplyQueued() : MH_UNKNOWN;
    if (!queueSucceeded || status != MH_OK) {
        reportFailure("queued D3D11 method detour activation failed");
        (void)MH_Uninitialize();
        context->Release();
        device->Release();
        return EXIT_FAILURE;
    }

    ID3D11PixelShader* invalidShader{};
    (void)device->CreatePixelShader(nullptr, 0, nullptr, &invalidShader);
    context->PSSetShader(nullptr, nullptr, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->OMSetRenderTargetsAndUnorderedAccessViews(
        0,
        nullptr,
        nullptr,
        0,
        0,
        nullptr,
        nullptr);
    context->DrawIndexed(0, 0, 0);
    context->Draw(0, 0);
    context->DrawIndexedInstanced(0, 0, 0, 0, 0);
    context->DrawInstanced(0, 0, 0, 0);
    D3D11_BUFFER_DESC indirectDesc{};indirectDesc.ByteWidth=12;indirectDesc.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    const std::array<UINT,3> zeroGroups{};D3D11_SUBRESOURCE_DATA initial{zeroGroups.data(),0,0};ID3D11Buffer* indirect{};
    const auto indirectResult=device->CreateBuffer(&indirectDesc,&initial,&indirect);
    context->Dispatch(0,0,0);if(indirect) {context->DispatchIndirect(indirect,0);indirect->Release();}
    const auto callsObserved = SUCCEEDED(indirectResult) && dispatchCalls==1 && dispatchIndirectCalls==1 &&
        createCalls.load(std::memory_order_relaxed) == 1 &&
        bindCalls.load(std::memory_order_relaxed) == 1 &&
        renderTargetCalls.load(std::memory_order_relaxed) == 1 &&
        renderTargetAndUnorderedAccessCalls.load(
            std::memory_order_relaxed) == 1 &&
        drawIndexedCalls.load(std::memory_order_relaxed) == 1 &&
        drawCalls.load(std::memory_order_relaxed) == 1 &&
        drawIndexedInstancedCalls.load(std::memory_order_relaxed) == 1 &&
        drawInstancedCalls.load(std::memory_order_relaxed) == 1;

    const auto disableStatus = MH_DisableHook(MH_ALL_HOOKS);
    bool removeSucceeded = true;
    for (const auto target : targets) {
        removeSucceeded = MH_RemoveHook(target) == MH_OK && removeSucceeded;
    }
    const auto uninitializeStatus = MH_Uninitialize();
    context->Release();
    device->Release();

    if (!callsObserved) {
        reportFailure("detoured D3D11 calls did not reach all ten hooks");
        return EXIT_FAILURE;
    }
    if (disableStatus != MH_OK || !removeSucceeded ||
        uninitializeStatus != MH_OK) {
        reportFailure("D3D11 method detour cleanup failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main()
{
    if(run(D3D_DRIVER_TYPE_WARP)!=EXIT_SUCCESS) return EXIT_FAILURE;
    return run(D3D_DRIVER_TYPE_HARDWARE);
}
