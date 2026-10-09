#include "Features/ibl/IblMaterialStyle.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"
#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif
#include <RE/NetImmerse/NiSmartPointer.h>
#include <RE/NetImmerse/NiTexture.h>
#include <REL/Relocation.h>
#include <Windows.h>
#include <cstring>
#include <utility>

namespace csah::ibl
{
    namespace
    {
        bool srgb(DXGI_FORMAT format) noexcept
        {
            return format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC2_UNORM_SRGB ||
                format == DXGI_FORMAT_BC3_UNORM_SRGB || format == DXGI_FORMAT_BC7_UNORM_SRGB ||
                format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        }
        bool readable(const void* pointer, std::size_t size) noexcept
        {
            MEMORY_BASIC_INFORMATION region{};
            const auto address = reinterpret_cast<std::uintptr_t>(pointer);
            return pointer && VirtualQuery(pointer, &region, sizeof(region)) &&
                region.State == MEM_COMMIT && !(region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                address >= reinterpret_cast<std::uintptr_t>(region.BaseAddress) &&
                size <= region.RegionSize && address - reinterpret_cast<std::uintptr_t>(region.BaseAddress) <= region.RegionSize - size;
        }
    }
    MaterialStyle::~MaterialStyle()
    {
        if (auto* pending = pendingReference_.exchange(nullptr)) pending->Release();
    }
    void MaterialStyle::loadReference() noexcept
    {
        try {
            // Raw FO4VR constructor and destruction/binding witnesses were
            // rechecked at 141C24220, 141C24180 and 14287D816 (2026-10-08).
            REL::Relocation<std::uintptr_t> create{ REL::ID(1071950) };
            const auto expected = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + 0x01C24220;
            constexpr unsigned char prefix[]{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20 };
            if (create.address() != expected || !readable(reinterpret_cast<void*>(expected), sizeof(prefix)) ||
                std::memcmp(reinterpret_cast<void*>(expected), prefix, sizeof(prefix)) != 0) {
                logging::warn("Cubemap style reference: VR texture loader entry unavailable; native material fallback retained.");
                return;
            }
            RE::BSFixedString path("textures\\shared\\cubemaps\\mipblur_DefaultOutside1.dds");
            RE::NiPointer<RE::NiTexture> texture{ RE::NiTexture::Create(path, false, false) };
            if (!texture || !readable(texture.get(), 0x40) ||
                !readable(texture->rendererTexture, sizeof(void*))) {
                logging::warn("Cubemap style reference: texture/rendererTexture unavailable.");
                return;
            }
            ID3D11ShaderResourceView* view{};
            std::memcpy(&view, texture->rendererTexture, sizeof(view));
            if (!readable(view, sizeof(void*))) {
                logging::warn("Cubemap style reference: renderer SRV unavailable.");
                return;
            }
            view->AddRef();
            if (auto* previous = pendingReference_.exchange(view, std::memory_order_acq_rel)) previous->Release();
            logging::info("Cubemap authored-style reference loaded through the VR resource system.");
        } catch (...) {
            logging::warn("Cubemap style reference loading failed; native material fallback retained.");
        }
    }
    void MaterialStyle::reset() noexcept
    {
        entries_ = {};
        nextEntry_ = 0;
        shader_.Reset();
        sampler_.Reset();
        device_.Reset();
        reference_.Reset();
        failureLogged_ = false;
    }
    bool MaterialStyle::initialize(ID3D11Device* device, const void* code, std::size_t size) noexcept
    {
        reset();
        if (!device || !code || FAILED(device->CreateComputeShader(code, size, nullptr, &shader_))) return false;
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&desc, &sampler_))) return false;
        device_ = device;
        return true;
    }
    ID3D11ShaderResourceView* MaterialStyle::prepare(ID3D11DeviceContext* context) noexcept
    {
        using Microsoft::WRL::ComPtr;
        if (!context || !shader_ || !device_) return nullptr;
        if (auto* pending = pendingReference_.exchange(nullptr, std::memory_order_acq_rel)) {
            reference_.Attach(pending);
            entries_ = {};
        }
        ComPtr<ID3D11ShaderResourceView> original;
        context->PSGetShaderResources(8, 1, &original);
        if (!original) return nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        original->GetDesc(&desc);
        if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBEARRAY ||
            desc.TextureCubeArray.NumCubes == 0 || desc.TextureCubeArray.NumCubes > 341) return nullptr;
        Entry* entry{};
        for (auto& candidate : entries_) if (candidate.original.Get() == original.Get()) { entry = &candidate; break; }
        if (!entry) {
            auto& candidate = entries_[nextEntry_++ % entries_.size()];
            candidate = {};
            candidate.cubes = desc.TextureCubeArray.NumCubes;
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = 2;
            texture.Height = candidate.cubes;
            texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
            texture.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(&texture, nullptr, &candidate.texture)) ||
                FAILED(device_->CreateShaderResourceView(candidate.texture.Get(), nullptr, &candidate.view)) ||
                FAILED(device_->CreateUnorderedAccessView(candidate.texture.Get(), nullptr, &candidate.output))) {
                if (!failureLogged_) logging::warn("Cubemap authored-style GPU allocation failed; native cubemaps retained.");
                failureLogged_ = true;
                candidate = {};
                return nullptr;
            }
            candidate.original = original;
            std::array<UINT, 4> encoding{};
            if (reference_) {
                D3D11_SHADER_RESOURCE_VIEW_DESC referenceDesc{};
                reference_->GetDesc(&referenceDesc);
                if (referenceDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE) {
                    candidate.reference = reference_;
                    const bool sourceSrgb = srgb(desc.Format);
                    const bool referenceSrgb = srgb(referenceDesc.Format);
                    encoding[0] = sourceSrgb == referenceSrgb ? 0U : (sourceSrgb ? 1U : 2U);
                }
            }
            D3D11_BUFFER_DESC constants{};
            constants.ByteWidth = sizeof(encoding);
            constants.Usage = D3D11_USAGE_IMMUTABLE;
            constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA initial{ encoding.data(), 0, 0 };
            if (FAILED(device_->CreateBuffer(&constants, &initial, &candidate.encoding))) {
                if (!failureLogged_) logging::warn("Cubemap style encoding constants unavailable; native fallback retained.");
                failureLogged_ = true;
                candidate = {};
                return nullptr;
            }
            entry = &candidate;
        }
        const auto now = GetTickCount64();
        if (entry->refreshed && now - entry->refreshed < 1000) return entry->view.Get();
        // Streaming may update a native array in place; refresh bounded GPU data
        // rather than assuming a pointer permanently identifies its contents.
        render::ScopedComputeState restore(context, {
            .firstShaderResource = 0, .shaderResourceCount = 2,
            .firstUnorderedAccess = 0, .unorderedAccessCount = 1,
            .firstSampler = 0, .samplerCount = 1,
            .firstConstantBuffer = 0, .constantBufferCount = 1 });
        if (!restore.captured()) return nullptr;
        ID3D11ShaderResourceView* sources[]{ original.Get(), entry->reference.Get() };
        auto* output = entry->output.Get();
        auto* sampler = sampler_.Get();
        auto* encoding = entry->encoding.Get();
        context->CSSetConstantBuffers(0, 1, &encoding);
        context->CSSetShader(shader_.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 2, sources);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->CSSetSamplers(0, 1, &sampler);
        context->Dispatch(entry->cubes, 1, 1);
        if (!restore.restore()) return nullptr;
        entry->refreshed = now;
        return entry->view.Get();
    }
}
