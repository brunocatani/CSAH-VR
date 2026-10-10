#pragma once
#include "render/GpuTimingProfiler.h"
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace csah::diagnostics::stereo_measurement {
inline constexpr UINT kClassCount=6, kClassStride=9, kClassBase=24;
inline constexpr UINT kCounterCount=kClassBase+kClassCount*kClassStride;
enum ClassCounter : UINT { ClassSamples, ClassGeometry, ClassNormal, ClassAlbedo,
    ClassMaterial, ClassMotionPairs, ClassMotionMatch, ClassAll, ClassMismatch };
enum Counter : UINT { Samples, Sky, FirstPerson, Near, Outside, Invalid, Edge, DepthMismatch,
    RoundTripMismatch, GeometryMatch, NormalMatch, AlbedoMatch, MaterialMatch, AllMaterialMatch,
    MotionAvailable, MotionMatch, AllWithMotion, InvalidMotion,
    MaxNormalError, MaxAlbedoError, MaxMaterialError, MaxMotionError, NonStereo };
struct Snapshot {std::uint64_t submitted{},completed{},skipped{};std::array<UINT,kCounterCount> last{};};
class Runtime final {
public:
    static Runtime& get() noexcept;
    void setEnabled(bool value) noexcept;
    void resetWorld() noexcept {epoch_.fetch_add(1,std::memory_order_release);}
    void onDeviceCreated(ID3D11Device*,ID3D11DeviceContext*) noexcept;
    void beginFrame() noexcept;
    void observeDraw(ID3D11DeviceContext*,bool material,bool ambient) noexcept;
    // Scope only the native draw, after material bindings; no replay/culling.
    [[nodiscard]] render::GpuTimingProfiler::Scope timeMaterialDraw(
        ID3D11DeviceContext*,bool material,UINT classCode,bool authoredPbr=false) noexcept;
    // Immediate-context thread only; used by offline GPU tests.
    Snapshot snapshot() const noexcept {return snapshot_;}
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Slot {Ptr<ID3D11Buffer> gpu,staging;Ptr<ID3D11UnorderedAccessView> output;Ptr<ID3D11Query> event;
        bool pending{};bool motion{},timed{};std::uint64_t epoch{},draws{};};
    bool ensureResources() noexcept;
    void synchronize() noexcept;
    void poll() noexcept;
    void materialDraw(ID3D11DeviceContext*) noexcept;
    void sample(ID3D11DeviceContext*) noexcept;
    void reject(const char*) noexcept;
    std::atomic_bool enabled_{};std::atomic_uint64_t epoch_{1};std::uint64_t observedEpoch_{};
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;Ptr<ID3D11DeviceContext1> context1_;
    Ptr<ID3D11ComputeShader> shader_;Ptr<ID3D11Buffer> parameters_;
    Ptr<ID3D11Resource> materialNormal_,motionResource_;Ptr<ID3D11ShaderResourceView> motionView_;
    Ptr<ID3D11ShaderResourceView> classView_;Ptr<ID3D11Resource> classResource_;
    bool classThisFrame_{};std::uint64_t classRetryAfter_{};
    std::array<Slot,4> slots_{};
    render::GpuTimingProfiler spanTiming_,probeTiming_;render::GpuTimingProfiler::Scope span_;
    std::array<render::GpuTimingProfiler,6> drawTiming_;
    std::array<std::uint64_t,6> drawAttempts_{},drawCounts_{},drawTimed_{},depthWrites_{},depthEqual_{},depthOther_{};
    UINT frameTimedDraws_{};
    Snapshot snapshot_{};std::array<std::uint64_t,kCounterCount> totals_{};
    std::uint64_t frame_{},sampleFrame_{~0ull},draws_{},windowFrames_{},windowDraws_{},timedFrames_{},retryAfter_{},motionRetryAfter_{};
    bool firstReject_{},firstSample_{},noSampleWarning_{},motionThisFrame_{};
    UINT width_{},height_{};D3D11_VIEWPORT viewport_{};
};
}
