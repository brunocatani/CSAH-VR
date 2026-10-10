#pragma once
#include "Features/terrain_shadows/TerrainShadowSettings.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace csah::terrain_shadows
{
    class ScopedBindings final
    {
    public:
        ScopedBindings(ID3D11DeviceContext* context,ID3D11ShaderResourceView* shadow,ID3D11ShaderResourceView* parameters) noexcept;
        ~ScopedBindings();
        ScopedBindings(const ScopedBindings&)=delete;
        ScopedBindings& operator=(const ScopedBindings&)=delete;
    private:
        ID3D11DeviceContext* context_{};
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,2> previous_;
    };
    class Runtime final
    {
    public:
        static Runtime& get() noexcept;
        // Startup only, before callbacks. CPU maps remain immutable thereafter.
        void loadMaps(const std::filesystem::path& directory) noexcept;
        void applySettings(const Settings& settings) noexcept;
        bool requested() const noexcept {return enabled_.load(std::memory_order_acquire);}
        void onDeviceCreated(ID3D11Device*,ID3D11DeviceContext*) noexcept;
        void beginFrame() noexcept {++frame_;}
        void resetWorld() noexcept {epoch_.fetch_add(1,std::memory_order_acq_rel);}
        void captureWorld(bool ready) noexcept;
        ScopedBindings scopeDraw(ID3D11DeviceContext*) noexcept;
    private:
        Runtime()=default;
        struct Map {std::string name;std::uint32_t width{},height{},samples{};std::int32_t west{},south{};std::vector<float> pixels;};
        bool createResources(const Map&) noexcept;
        bool update(const Map&) noexcept;
        std::vector<Map> maps_;
        std::atomic_bool enabled_{};
        std::atomic<float> strength_{1};
        std::atomic_uint32_t epoch_{1};
        std::atomic_uint64_t selected_{};
        std::uint64_t frame_{},updatedFrame_{~0ull},loadedSelection_{};
        unsigned retry_{};
        bool loggedFailure_{};
        bool parametersUploaded_{};
        std::array<float,8> uploadedParameters_{};
        template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
        Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
        Ptr<ID3D11Texture2D> height_,shadow_;
        Ptr<ID3D11ShaderResourceView> heightView_,shadowView_,parameterView_;
        Ptr<ID3D11UnorderedAccessView> shadowWrite_;
        Ptr<ID3D11Buffer> parameters_,updateParameters_;
        Ptr<ID3D11ComputeShader> compute_;
    };
}
