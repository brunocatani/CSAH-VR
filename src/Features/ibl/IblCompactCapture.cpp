#include "Features/ibl/IblCompactCapture.h"
#include "render/ComputeStateScope.h"
#include "render/DebugResourceName.h"
#include "support/Logger.h"

namespace csah::ibl
{
    using Microsoft::WRL::ComPtr;

    bool CompactCapture::initialize(ID3D11Device* device,
        const std::array<Bytecode, kShaderCount>& shaders, UINT extent) noexcept
    {
        reset();
        if (!device || extent < 16 || extent > 512) return false;
        std::array<ComPtr<ID3D11ComputeShader>, kShaderCount> created;
        for (std::size_t index = 0; index < shaders.size(); ++index) {
            const auto& code = shaders[index];
            if (!code.data || !code.size || FAILED(device->CreateComputeShader(code.data, code.size, nullptr, &created[index]))) return false;
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = description.Height = extent;
        description.MipLevels = description.SampleDesc.Count = 1;
        description.ArraySize = 12;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> input;
        ComPtr<ID3D11UnorderedAccessView> output;
        D3D11_BUFFER_DESC bufferDescription{};
        bufferDescription.ByteWidth = 32;
        bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &texture)) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &input)) ||
            FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &output)) ||
            FAILED(device->CreateBuffer(&bufferDescription, nullptr, &constants))) return false;
        render::nameResource(texture.Get(), "CSAH / Cubemaps / Compact native radiance (six faces per eye)");
        device_ = device;
        shaders_ = std::move(created);
        radiance_ = std::move(texture);
        radianceView_ = std::move(input);
        radianceOutput_ = std::move(output);
        constants_ = std::move(constants);
        extent_ = extent;
        return true;
    }

    void CompactCapture::reset() noexcept
    {
        timing_.reset();
        timingAttempted_ = false;
        shaders_ = {};
        radianceOutput_.Reset(); radianceView_.Reset(); radiance_.Reset();
        constants_.Reset(); device_.Reset(); extent_ = 0;
    }

    bool CompactCapture::dispatch(ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* coverage, std::size_t shaderIndex) noexcept
    {
        if (!context || !device_ || !coverage || shaderIndex >= shaders_.size() || !shaders_[shaderIndex]) return false;
        ComPtr<ID3D11Device> actualDevice;
        context->GetDevice(&actualDevice);
        if (actualDevice.Get() != device_.Get()) return false;
        coverage->GetDevice(actualDevice.ReleaseAndGetAddressOf());
        if (actualDevice.Get() != device_.Get()) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        coverage->GetDesc(&view);
        if (view.Format != DXGI_FORMAT_R8_UNORM || view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            view.Texture2D.MostDetailedMip != 0 || view.Texture2D.MipLevels != 1) return false;
        ComPtr<ID3D11Resource> resource;
        coverage->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture))) return false;
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Width < 2 || (description.Width & 1) || !description.Height ||
            description.ArraySize != 1 || description.SampleDesc.Count != 1) return false;

        render::ScopedComputeState restore(context, {0, 32, 0, 1, 0, 16, 0, 14});
        if (!restore.captured()) return false;
        std::array<ID3D11ShaderResourceView*, 16> sources{};
        std::array<ID3D11SamplerState*, 16> samplers{};
        std::array<ID3D11Buffer*, 13> buffers{};
        context->PSGetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
        context->PSGetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        context->PSGetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
        // Retain every getter reference until after binding. These are borrowed
        // engine objects only for the duration of this immediate-context call.
        std::array<ComPtr<ID3D11ShaderResourceView>, 16> retainedSources;
        std::array<ComPtr<ID3D11SamplerState>, 16> retainedSamplers;
        std::array<ComPtr<ID3D11Buffer>, 13> retainedBuffers;
        for (std::size_t index = 0; index < sources.size(); ++index) retainedSources[index].Attach(sources[index]);
        for (std::size_t index = 0; index < samplers.size(); ++index) retainedSamplers[index].Attach(samplers[index]);
        for (std::size_t index = 0; index < buffers.size(); ++index) retainedBuffers[index].Attach(buffers[index]);
        if (!sources[7] || !buffers[2] || !buffers[12]) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC depthView{};
        sources[7]->GetDesc(&depthView);
        if (depthView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || depthView.Texture2D.MostDetailedMip != 0) return false;
        sources[7]->GetResource(resource.ReleaseAndGetAddressOf());
        if (FAILED(resource.As(&texture))) return false;
        D3D11_TEXTURE2D_DESC depthDescription{};
        texture->GetDesc(&depthDescription);
        if (depthDescription.Width != description.Width || depthDescription.Height != description.Height ||
            depthDescription.ArraySize != 1 || depthDescription.SampleDesc.Count != 1) return false;
        D3D11_BUFFER_DESC cameraDescription{};
        buffers[12]->GetDesc(&cameraDescription);
        if (cameraDescription.ByteWidth < 77 * 16) return false;
        D3D11_VIEWPORT viewport{};UINT count=1;context->RSGetViewports(&count,&viewport);
        const std::array<UINT, 8> parameters{static_cast<UINT>(viewport.Width),static_cast<UINT>(viewport.Height),extent_,0,
            static_cast<UINT>(viewport.TopLeftX),static_cast<UINT>(viewport.TopLeftY),0,0};
        context->UpdateSubresource(constants_.Get(), 0, nullptr, parameters.data(), 0, 0);
        if (!timingAttempted_) {
            timingAttempted_ = true;
            if (!timing_.initialize(device_.Get(), context, "Image Based Lighting native samples",
                {"compact native lighting", nullptr, nullptr, nullptr}, 1, 5))
                logging::warn("IBL compact capture timing queries could not be created.");
        }
        auto timing = timing_.begin();
        context->CSSetShader(shaders_[shaderIndex].Get(), nullptr, 0);
        context->CSSetShaderResources(0, static_cast<UINT>(sources.size()), sources.data());
        context->CSSetShaderResources(31, 1, &coverage);
        context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        context->CSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
        auto* constants = constants_.Get();
        context->CSSetConstantBuffers(13, 1, &constants);
        auto* output = radianceOutput_.Get();
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->Dispatch((extent_ + 7) / 8, (extent_ + 7) / 8, 12);
        return restore.restore();
    }
}
