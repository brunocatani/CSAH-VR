#include "Features/ibl/IblMaterialBindingScope.h"

#include <utility>

namespace csah::ibl
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11DeviceChild* child) noexcept
        {
            if (!expected || !child) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            child->GetDevice(&device);
            return device.Get() == expected;
        }

        [[nodiscard]] bool sameDevice(
            ID3D11Device* expected,
            ID3D11ShaderResourceView* view) noexcept
        {
            if (!expected || !view) {
                return false;
            }
            ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            if (!resource) {
                return false;
            }
            ComPtr<ID3D11Device> device;
            resource->GetDevice(&device);
            return device.Get() == expected;
        }
    }

    ScopedMaterialBindings::ScopedMaterialBindings(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* albedo,
        ID3D11ShaderResourceView* radiance,
        ID3D11ShaderResourceView* validity,
        ID3D11ShaderResourceView* previousRadiance,
        ID3D11ShaderResourceView* previousValidity,
        ID3D11ShaderResourceView* position,
        ID3D11ShaderResourceView* previousPosition,
        ID3D11ShaderResourceView* materialProperties,
        ID3D11ShaderResourceView* pbrMaterial,
        ID3D11ShaderResourceView* surfaceClass,
        ID3D11Buffer* constants,
        ID3D11ShaderResourceView* style) noexcept
    {
        if (!context) {
            return;
        }
        if (!constants) {
            rejection_ = MaterialBindingRejection::missingConstants;
            return;
        }

        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (!device || !sameDevice(device.Get(), constants) ||
            (albedo && !sameDevice(device.Get(), albedo)) ||
            (style && !sameDevice(device.Get(), style)) ||
            (radiance && !sameDevice(device.Get(), radiance)) ||
            (validity && !sameDevice(device.Get(), validity)) ||
            (previousRadiance &&
                !sameDevice(device.Get(), previousRadiance)) ||
            (previousValidity &&
                !sameDevice(device.Get(), previousValidity)) ||
            (position && !sameDevice(device.Get(), position)) ||
            (previousPosition &&
                !sameDevice(device.Get(), previousPosition)) ||
            (materialProperties &&
                !sameDevice(device.Get(), materialProperties)) ||
            (pbrMaterial && !sameDevice(device.Get(), pbrMaterial)) ||
            (surfaceClass && !sameDevice(device.Get(), surfaceClass))) {
            rejection_ = MaterialBindingRejection::deviceMismatch;
            return;
        }

        context_ = context;
        std::array<ID3D11ShaderResourceView*, 9> previousResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(previousResources.size()),
            previousResources.data());
        for (std::size_t index = 0; index < previousResources.size(); ++index) {
            previousResources_[index].Attach(previousResources[index]);
        }
        ID3D11Buffer* previousConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &previousConstants);
        previousConstants_.Attach(previousConstants);
        captured_ = true;
        std::array<ID3D11ShaderResourceView*, 2> previousPbrResources{};
        context_->PSGetShaderResources(
            kPbrMaterialSlot,
            1,
            &previousPbrResources[0]);
        context_->PSGetShaderResources(
            kSurfaceClassSlot,
            1,
            &previousPbrResources[1]);
        for (std::size_t index = 0;
             index < previousPbrResources.size();
             ++index) {
            previousPbrResources_[index].Attach(
                previousPbrResources[index]);
        }
        pbrResourcesCaptured_ = true;

        std::array<ID3D11ShaderResourceView*, 9> resources{
            albedo,
            radiance,
            validity,
            previousRadiance,
            previousValidity,
            position,
            previousPosition,
            materialProperties,
            style,
        };
        context_->PSSetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(resources.size()),
            resources.data());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        context_->PSSetShaderResources(
            kPbrMaterialSlot,
            1,
            &pbrMaterial);
        context_->PSSetShaderResources(
            kSurfaceClassSlot,
            1,
            &surfaceClass);

        std::array<ID3D11ShaderResourceView*, 9> appliedResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(appliedResources.size()),
            appliedResources.data());
        ID3D11Buffer* appliedConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &appliedConstants);
        const auto applied = appliedResources[0] == albedo &&
            appliedResources[1] == radiance &&
            appliedResources[2] == validity &&
            appliedResources[3] == previousRadiance &&
            appliedResources[4] == previousValidity &&
            appliedResources[5] == position &&
            appliedResources[6] == previousPosition &&
            appliedResources[7] == materialProperties &&
            appliedResources[8] == style &&
            appliedConstants == constants;
        std::array<ID3D11ShaderResourceView*, 2> appliedPbrResources{};
        context_->PSGetShaderResources(
            kPbrMaterialSlot,
            1,
            &appliedPbrResources[0]);
        context_->PSGetShaderResources(
            kSurfaceClassSlot,
            1,
            &appliedPbrResources[1]);
        const auto pbrApplied =
            appliedPbrResources[0] == pbrMaterial &&
            appliedPbrResources[1] == surfaceClass;
        for (auto* resource : appliedResources) {
            if (resource) {
                resource->Release();
            }
        }
        if (appliedConstants) {
            appliedConstants->Release();
        }
        for (auto* resource : appliedPbrResources) {
            if (resource) {
                resource->Release();
            }
        }
        if (!applied || !pbrApplied) {
            rejection_ = MaterialBindingRejection::applyFailed;
            (void)restoreCaptured();
            restored_ = true;
            return;
        }

        rejection_ = MaterialBindingRejection::none;
        active_ = true;
    }

    ScopedMaterialBindings::~ScopedMaterialBindings()
    {
        (void)restore();
    }

    ScopedMaterialBindings::ScopedMaterialBindings(
        ScopedMaterialBindings&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    ScopedMaterialBindings& ScopedMaterialBindings::operator=(
        ScopedMaterialBindings&& other) noexcept
    {
        if (this != &other) {
            (void)restore();
            moveFrom(std::move(other));
        }
        return *this;
    }

    bool ScopedMaterialBindings::active() const noexcept
    {
        return active_ && !restored_;
    }

    bool ScopedMaterialBindings::restore() noexcept
    {
        if (restored_) {
            return true;
        }
        if (!captured_) {
            restored_ = true;
            active_ = false;
            return false;
        }
        auto restored = restoreCaptured();
        if (!restored) {
            restored = restoreCaptured();
        }
        restored_ = true;
        active_ = false;
        return restored;
    }

    MaterialBindingRejection ScopedMaterialBindings::rejection() const noexcept
    {
        return rejection_;
    }

    bool ScopedMaterialBindings::restoreCaptured() noexcept
    {
        if (!context_ || !captured_) {
            return false;
        }
        std::array<ID3D11ShaderResourceView*, 9> resources{
            previousResources_[0].Get(),
            previousResources_[1].Get(),
            previousResources_[2].Get(),
            previousResources_[3].Get(),
            previousResources_[4].Get(),
            previousResources_[5].Get(),
            previousResources_[6].Get(),
            previousResources_[7].Get(),
            previousResources_[8].Get(),
        };
        auto* constants = previousConstants_.Get();
        context_->PSSetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(resources.size()),
            resources.data());
        context_->PSSetConstantBuffers(kConstantSlot, 1, &constants);
        if (pbrResourcesCaptured_) {
            auto* previousPbrMaterial = previousPbrResources_[0].Get();
            auto* previousSurfaceClass = previousPbrResources_[1].Get();
            context_->PSSetShaderResources(
                kPbrMaterialSlot,
                1,
                &previousPbrMaterial);
            context_->PSSetShaderResources(
                kSurfaceClassSlot,
                1,
                &previousSurfaceClass);
        }

        std::array<ID3D11ShaderResourceView*, 9> restoredResources{};
        context_->PSGetShaderResources(
            kAlbedoSlot,
            static_cast<UINT>(restoredResources.size()),
            restoredResources.data());
        ID3D11Buffer* restoredConstants{};
        context_->PSGetConstantBuffers(
            kConstantSlot,
            1,
            &restoredConstants);
        const auto restored =
            restoredResources[0] == resources[0] &&
            restoredResources[1] == resources[1] &&
            restoredResources[2] == resources[2] &&
            restoredResources[3] == resources[3] &&
            restoredResources[4] == resources[4] &&
            restoredResources[5] == resources[5] &&
            restoredResources[6] == resources[6] &&
            restoredResources[7] == resources[7] &&
            restoredResources[8] == resources[8] &&
            restoredConstants == constants;
        std::array<ID3D11ShaderResourceView*, 2> restoredPbrResources{};
        if (pbrResourcesCaptured_) {
            context_->PSGetShaderResources(
                kPbrMaterialSlot,
                1,
                &restoredPbrResources[0]);
            context_->PSGetShaderResources(
                kSurfaceClassSlot,
                1,
                &restoredPbrResources[1]);
        }
        const auto pbrRestored = !pbrResourcesCaptured_ ||
            (restoredPbrResources[0] ==
                    previousPbrResources_[0].Get() &&
                restoredPbrResources[1] ==
                    previousPbrResources_[1].Get());
        for (auto* resource : restoredResources) {
            if (resource) {
                resource->Release();
            }
        }
        if (restoredConstants) {
            restoredConstants->Release();
        }
        for (auto* resource : restoredPbrResources) {
            if (resource) {
                resource->Release();
            }
        }
        return restored && pbrRestored;
    }

    void ScopedMaterialBindings::moveFrom(
        ScopedMaterialBindings&& other) noexcept
    {
        context_ = std::move(other.context_);
        previousResources_ = std::move(other.previousResources_);
        previousConstants_ = std::move(other.previousConstants_);
        previousPbrResources_ = std::move(other.previousPbrResources_);
        rejection_ = other.rejection_;
        captured_ = other.captured_;
        pbrResourcesCaptured_ = other.pbrResourcesCaptured_;
        active_ = other.active_;
        restored_ = other.restored_;
        other.captured_ = false;
        other.pbrResourcesCaptured_ = false;
        other.active_ = false;
        other.restored_ = true;
    }
}
