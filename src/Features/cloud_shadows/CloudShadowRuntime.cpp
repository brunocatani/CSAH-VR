#include "Features/cloud_shadows/CloudShadowRuntime.h"
#include "CloudGeometryCapture.h"
#include "CloudFieldUpdate.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"
#include <d3dcompiler.h>

namespace csah::cloud_shadows
{
    namespace
    {
        constexpr unsigned kAllCubeFaces = 0x1f;
        constexpr unsigned kCubeSize = 256;
        thread_local bool internalRender{};
        struct InternalScope {
            bool previous{internalRender};
            InternalScope() { internalRender = true; }
            ~InternalScope() { internalRender = previous; }
        };
    }
    Runtime& Runtime::get() noexcept { static Runtime value; return value; }
    bool Runtime::internalRenderActive() noexcept { return internalRender; }
    bool Runtime::requested() const noexcept
    {
        return enabled_.load(std::memory_order_acquire) && resourcesReady_.load(std::memory_order_acquire);
    }
    void Runtime::applySettings(const Settings& settings) noexcept
    {
        const auto safe = sanitize(settings);
        const auto old = enabled_.exchange(safe.enabled, std::memory_order_acq_rel);
        opacity_.store(safe.opacity, std::memory_order_relaxed);
        if (old != safe.enabled) resetWorld();
    }
    void Runtime::resetWorld() noexcept { epoch_.fetch_add(1, std::memory_order_release); }
    void Runtime::synchronizeReset() noexcept
    {
        const auto epoch = epoch_.load(std::memory_order_acquire);
        if (observedEpoch_ == epoch) return;
        observedEpoch_ = epoch;
        captureStarted_ = captureRejected_ = false;
        populatedFaces_.store(0, std::memory_order_relaxed);
        mainTarget_.Reset();
        fieldReset_ = true;
        fieldFrame_ = ~0ull;
    }
    void Runtime::beginFrame() noexcept
    {
        synchronizeReset();
        // Sky follows deferred lighting. Publish only the completed preceding
        // frame, never a cube still receiving layers. A cloudless frame withdraws it.
        const bool complete = requested() && captureStarted_ && !captureRejected_;
        if (complete) std::swap(readIndex_, writeIndex_);
        populatedFaces_.store(complete ? kAllCubeFaces : 0, std::memory_order_relaxed);
        captureStarted_ = captureRejected_ = false;
        mainTarget_.Reset();
        ++frame_;
        if (retryFrames_) --retryFrames_;
    }
    void Runtime::onDeviceCreated(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
    {
        resourcesReady_.store(false, std::memory_order_release);
        device_ = device; context_ = context;
        cubes_ = {}; sampler_.Reset(); field_.Reset(); fieldParameters_.Reset();
        fieldView_.Reset(); fieldOutput_.Reset(); fieldUpdate_.Reset();
        geometry::ReleaseDeviceResources();
        resetWorld(); synchronizeReset();
        startTick_ = GetTickCount64(); retryFrames_ = 0;
        if (!device || !context) return;
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&desc, &sampler_))) {
            failures_.fetch_add(1, std::memory_order_relaxed); return;
        }
        resourcesReady_.store(true, std::memory_order_release);
        logging::info("Cloud Shadows main-Sky geometry replay ready; native reflection captures are not required.");
    }
    void Runtime::observeMainView(ID3D11DeviceContext* context) noexcept
    {
        if (!requested() || context != context_.Get()) return;
        synchronizeReset();
        // Called only for the bytecode-authenticated native DFComposite draw.
        // Its output is the main/main-temp resource subsequently used by Sky.
        Ptr<ID3D11RenderTargetView> target;
        context->OMGetRenderTargets(1, &target, nullptr);
        if (!target) return;
        D3D11_RENDER_TARGET_VIEW_DESC vd{}; target->GetDesc(&vd);
        if (vd.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || vd.Texture2D.MipSlice != 0) return;
        target->GetResource(mainTarget_.ReleaseAndGetAddressOf());
    }
    bool Runtime::ensureCaptureResources() noexcept
    {
        if (cubes_[0].view && cubes_[1].view && fieldUpdate_) return true;
        if (retryFrames_) return false;
        retryFrames_ = 60;
        InternalScope internal;
        const auto fail = [this]() {
            failures_.fetch_add(1, std::memory_order_relaxed);
            logging::error("Cloud Shadows resource creation failed; capture withdrawn, retry in 60 render frames.");
            return false;
        };
        std::array<Cube,2> next;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = kCubeSize; td.ArraySize = 6; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R16_FLOAT; td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        for (auto& cube : next) {
            if (FAILED(device_->CreateTexture2D(&td, nullptr, &cube.texture)) ||
                FAILED(device_->CreateShaderResourceView(cube.texture.Get(), nullptr, &cube.view))) return fail();
            for (unsigned face = 0; face < 6; ++face) {
                D3D11_RENDER_TARGET_VIEW_DESC vd{}; vd.Format = td.Format;
                vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                vd.Texture2DArray.ArraySize = 1; vd.Texture2DArray.FirstArraySlice = face;
                if (FAILED(device_->CreateRenderTargetView(cube.texture.Get(), &vd, &cube.targets[face]))) return fail();
            }
        }
        Ptr<ID3DBlob> code, errors;
        auto result = D3DCompile(kCloudFieldUpdate, sizeof(kCloudFieldUpdate)-1, "CloudFieldUpdate",
            nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(result)) {
            logging::error("Cloud field shader: {}", errors ? static_cast<char*>(errors->GetBufferPointer()) : "compile failed");
            return fail();
        }
        if (FAILED(device_->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                fieldUpdate_.ReleaseAndGetAddressOf()))) return fail();
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 48;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, field_.ReleaseAndGetAddressOf()))) return fail();
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Buffer.NumElements = 3;
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = sd.Format;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 3;
        if (FAILED(device_->CreateShaderResourceView(field_.Get(), &sd, fieldView_.ReleaseAndGetAddressOf())) ||
            FAILED(device_->CreateUnorderedAccessView(field_.Get(), &ud, fieldOutput_.ReleaseAndGetAddressOf()))) return fail();
        bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, fieldParameters_.ReleaseAndGetAddressOf()))) return fail();
        cubes_ = std::move(next); retryFrames_ = 0; fieldReset_ = true;
        cubeRebuilds_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    void Runtime::capture(ID3D11DeviceContext* context,
        linear_lighting::ReplacementShaderBinding binding, void (*submit)(void*), void* user) noexcept
    {
        if (!requested() || context != context_.Get() ||
            binding.family != linear_lighting::ReplacementShaderFamily::sky ||
            binding.contractPlusOne < 5 || binding.contractPlusOne > 7) return;
        // Contracts 5-7 therefore map to Clouds, CloudsLerp, and CloudsFade descriptors 4-6.
        synchronizeReset();
        if (!mainTarget_ || captureRejected_) return;
        Ptr<ID3D11RenderTargetView> target; context->OMGetRenderTargets(1, &target, nullptr);
        Ptr<ID3D11Resource> resource;
        if (target) target->GetResource(&resource);
        if (resource.Get() != mainTarget_.Get()) return;
        Ptr<ID3D11Texture2D> texture;
        if (FAILED(resource.As(&texture))) return;
        D3D11_TEXTURE2D_DESC td{}; texture->GetDesc(&td);
        std::array<D3D11_VIEWPORT,16> viewports{}; UINT count = 16;
        context->RSGetViewports(&count, viewports.data());
        const auto& vp = viewports[0];
        if (count != 1 || (td.Width & 1) || vp.TopLeftX != 0 || vp.TopLeftY != 0 ||
            vp.Width != float(td.Width) || vp.Height != float(td.Height)) return;
        captureCandidates_.fetch_add(1, std::memory_order_relaxed);
        if (!ensureCaptureResources()) { captureRejected_ = true; populatedFaces_ = 0; return; }
        InternalScope internal;
        auto& cube = cubes_[writeIndex_];
        if (!captureStarted_) {
            const float clear[4]{};
            Ptr<ID3D11Predicate> predicate; BOOL predicateValue{};
            context->GetPredication(&predicate, &predicateValue);
            context->SetPredication(nullptr, FALSE);
            for (auto& face : cube.targets) context->ClearRenderTargetView(face.Get(), clear);
            context->SetPredication(predicate.Get(), predicateValue);
            captureStarted_ = true;
        }
        std::array<ID3D11RenderTargetView*,6> targets{};
        for (unsigned i = 0; i < 6; ++i) targets[i] = cube.targets[i].Get();
        geometry::RejectReason reason{};
        const bool success = geometry::AccumulateOpacity(context, geometry::SkyConstantLayout::kVr,
            static_cast<geometry::CloudTechnique>(binding.contractPlusOne), kCubeSize, targets,
            submit, user, &reason);
        restores_.fetch_add(1, std::memory_order_relaxed);
        if (!success) {
            captureRejected_ = true; populatedFaces_ = 0;
            captureRejects_.fetch_add(1, std::memory_order_relaxed);
            if (!firstCaptureRejectLogged_.exchange(true))
                logging::warn("Cloud Shadows main-Sky replay rejected (reason={}); whole frame withdrawn.", unsigned(reason));
            return;
        }
        capturedDraws_.fetch_add(1, std::memory_order_relaxed);
        if (!firstCaptureLogged_.exchange(true))
            logging::info("Cloud Shadows captured live main-Sky opacity into five private faces ({} px, stereo duplicates removed).", kCubeSize);
    }
    bool Runtime::updateField(ID3D11Buffer* camera) noexcept
    {
        if (fieldFrame_ == frame_) return true;
        if (!camera || !fieldUpdate_) return false;
        D3D11_BUFFER_DESC bd{}; camera->GetDesc(&bd);
        if (bd.ByteWidth < 61*16) return false;
        InternalScope internal;
        render::ScopedComputeState restore(context_.Get(), {0,0,0,1,0,0,0,13});
        if (!restore.captured()) return false;
        const std::array<float,4> params{fieldReset_ ? 1.f : 0.f,
            float(GetTickCount64()-startTick_)*0.001f,10000.f,4.f};
        context_->UpdateSubresource(fieldParameters_.Get(),0,nullptr,params.data(),0,0);
        auto* cb = fieldParameters_.Get(); context_->CSSetConstantBuffers(0,1,&cb);
        context_->CSSetConstantBuffers(12,1,&camera);
        auto* output = fieldOutput_.Get(); context_->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        context_->CSSetShader(fieldUpdate_.Get(),nullptr,0);
        Ptr<ID3D11Predicate> predicate; BOOL predicateValue{};
        context_->GetPredication(&predicate, &predicateValue);
        context_->SetPredication(nullptr, FALSE);
        context_->Dispatch(1,1,1);
        context_->SetPredication(predicate.Get(), predicateValue);
        fieldFrame_ = frame_; fieldReset_ = false;
        return true;
    }
    bool Runtime::prepareLighting(ID3D11DeviceContext* context, ID3D11ShaderResourceView*& cube,
        ID3D11SamplerState*& sampler, float& opacity, ID3D11Buffer* camera) noexcept
    {
        cube = nullptr; sampler = nullptr; opacity = 0;
        if (!requested() || context != context_.Get()) return false;
        synchronizeReset();
        if (populatedFaces_.load(std::memory_order_relaxed) != kAllCubeFaces) {
            lightingRejects_.fetch_add(1, std::memory_order_relaxed); return false;
        }
        Ptr<ID3D11Buffer> nativeCamera;
        if (!camera) { context->PSGetConstantBuffers(12,1,&nativeCamera); camera = nativeCamera.Get(); }
        if (!updateField(camera)) return false;
        cube = cubes_[readIndex_].view.Get(); sampler = sampler_.Get(); opacity = opacity_.load();
        lightingBinds_.fetch_add(1, std::memory_order_relaxed);
        if (!firstLightingBindLogged_.exchange(true))
            logging::info("Cloud Shadows bound completed main-Sky capture to world-anchored directional lighting (opacity={:.3f}).", opacity);
        return true;
    }
    RuntimeSnapshot Runtime::snapshot() const noexcept
    {
        return {
            .settings = sanitize({
                .enabled = enabled_.load(std::memory_order_acquire),
                .opacity = opacity_.load(std::memory_order_relaxed),
            }),
            .gpuReady = resourcesReady_.load(std::memory_order_acquire),
            .cubeReady = populatedFaces_.load(std::memory_order_relaxed) ==
                kAllCubeFaces,
            .populatedFaceMask =
                populatedFaces_.load(std::memory_order_relaxed),
            .captureCandidates =
                captureCandidates_.load(std::memory_order_relaxed),
            .capturedDraws = capturedDraws_.load(std::memory_order_relaxed),
            .captureRejects =
                captureRejects_.load(std::memory_order_relaxed),
            .cubeRebuilds = cubeRebuilds_.load(std::memory_order_relaxed),
            .lightingBinds = lightingBinds_.load(std::memory_order_relaxed),
            .lightingRejects =
                lightingRejects_.load(std::memory_order_relaxed),
            .restores = restores_.load(std::memory_order_relaxed),
            .failures = failures_.load(std::memory_order_relaxed),
        };
    }
}
