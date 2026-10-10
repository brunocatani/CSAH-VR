#include "render/GpuTimingProfiler.h"

#include <algorithm>
#include <atomic>
#include <spdlog/spdlog.h>
#include <utility>

namespace csah::render
{
    namespace
    {
        // All profiled owners execute on the retained immediate context.
        // Permit one disjoint interval at a time so nested feature scopes do
        // not create overlapping D3D11 timestamp-disjoint transactions.
        GpuTimingProfiler* activeProfiler{};
        constexpr std::uint32_t kOnDemandGroupMask =
            static_cast<std::uint32_t>(
                GpuTimingProfiler::Group::ContactShadows) |
            static_cast<std::uint32_t>(
                GpuTimingProfiler::Group::Skylighting) |
            static_cast<std::uint32_t>(GpuTimingProfiler::Group::General) |
            static_cast<std::uint32_t>(GpuTimingProfiler::Group::StereoMeasurements);
        std::atomic_uint32_t onDemandGroups{};
        std::atomic_uint64_t collectionRequestRevision{};
    }

    GpuTimingProfiler::Scope::Scope(
        GpuTimingProfiler* owner,
        std::size_t slotIndex) noexcept :
        owner_(owner),
        slotIndex_(slotIndex),
        cpuStart_(std::chrono::steady_clock::now())
    {}

    GpuTimingProfiler::Scope::~Scope() noexcept
    {
        if (owner_) {
            owner_->finish(*this);
        }
    }

    GpuTimingProfiler::Scope::Scope(Scope&& other) noexcept :
        owner_(std::exchange(other.owner_, nullptr)),
        slotIndex_(other.slotIndex_),
        nextPoint_(other.nextPoint_),
        cpuStart_(other.cpuStart_)
    {}

    GpuTimingProfiler::Scope& GpuTimingProfiler::Scope::operator=(
        Scope&& other) noexcept
    {
        if (this != &other) {
            if (owner_) {
                owner_->finish(*this);
            }
            owner_ = std::exchange(other.owner_, nullptr);
            slotIndex_ = other.slotIndex_;
            nextPoint_ = other.nextPoint_;
            cpuStart_ = other.cpuStart_;
        }
        return *this;
    }

    void GpuTimingProfiler::Scope::mark() noexcept
    {
        if (owner_) {
            owner_->mark(*this);
        }
    }

    void GpuTimingProfiler::Scope::finish() noexcept
    {
        if (owner_) {
            owner_->finish(*this);
        }
    }

    bool GpuTimingProfiler::initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const char* name,
        const std::array<const char*, kMaximumSegments>& labels,
        std::uint32_t segmentCount,
        std::uint32_t reportSampleCount,
        std::uint32_t sampleStride,
        Group group) noexcept
    {
        reset();
        if (!device || !context || !name || segmentCount == 0 ||
            segmentCount > kMaximumSegments || reportSampleCount == 0) {
            return false;
        }

        device_ = device;
        context_ = context;
        labels_ = labels;
        name_ = name;
        segmentCount_ = segmentCount;
        reportSampleCount_ = reportSampleCount;
        sampleStride_ = (std::max)(1u, sampleStride);
        group_ = group;
        collectionRequestRevision_ =
            collectionRequestRevision.load(std::memory_order_acquire);
        const auto requestedGroups =
            onDemandGroups.load(std::memory_order_acquire);
        collecting_ =
            (requestedGroups & static_cast<std::uint32_t>(group_)) != 0;
        nextReport_ = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        return true;
    }

    bool GpuTimingProfiler::groupRequested(Group group) noexcept
    {
        return (onDemandGroups.load(std::memory_order_acquire) & static_cast<std::uint32_t>(group)) != 0;
    }

    bool GpuTimingProfiler::ensureQueries() noexcept
    {
        if (slots_[0].disjoint) return true;
        if (queryAllocationAttempted_ || !device_) return false;
        queryAllocationAttempted_ = true;
        std::array<Slot, kSlotCount> created{};
        const D3D11_QUERY_DESC disjointDescription{
            .Query = D3D11_QUERY_TIMESTAMP_DISJOINT,
            .MiscFlags = 0,
        };
        const D3D11_QUERY_DESC timestampDescription{
            .Query = D3D11_QUERY_TIMESTAMP,
            .MiscFlags = 0,
        };
        for (auto& slot : created) {
            if (FAILED(device_->CreateQuery(
                    &disjointDescription,
                    slot.disjoint.GetAddressOf()))) {
                try { spdlog::warn("GPU timing query allocation failed for {}; rendering continues without timing.", name_); } catch (...) {}
                return false;
            }
            for (std::uint32_t point = 0;
                 point <= segmentCount_;
                 ++point) {
                if (FAILED(device_->CreateQuery(
                        &timestampDescription,
                        slot.timestamps[point].GetAddressOf()))) {
                    try { spdlog::warn("GPU timing query allocation failed for {}; rendering continues without timing.", name_); } catch (...) {}
                return false;
                }
            }
        }
        slots_ = std::move(created);
        return true;
    }

    void GpuTimingProfiler::setOnDemandGroups(
        std::uint32_t groups) noexcept
    {
        groups &= kOnDemandGroupMask;
        if (onDemandGroups.exchange(
                groups,
                std::memory_order_acq_rel) != groups) {
            collectionRequestRevision.fetch_add(
                1,
                std::memory_order_acq_rel);
        }
    }

    void GpuTimingProfiler::reset() noexcept
    {
        if (activeProfiler == this) {
            activeProfiler = nullptr;
        }
        slots_ = {};
        context_.Reset();
        device_.Reset();
        labels_ = {};
        accumulatedMilliseconds_ = {};
        name_ = nullptr;
        segmentCount_ = 0;
        reportSampleCount_ = 120;
        sampleStride_ = 1;
        writeIndex_ = 0;
        beginAttempts_ = 0;
        completedSamples_ = 0;
        accumulatedCpuMilliseconds_ = 0.0;
        completedCpuSamples_ = 0;
        nextReport_ = {};
        collectionRequestRevision_ = 0;
        group_ = Group::General;
        queryAllocationAttempted_ = false;
        collecting_ = false;
    }

    GpuTimingProfiler::Scope GpuTimingProfiler::begin() noexcept
    {
        synchronizeCollectionRequest();
        poll();
        if (!context_ || segmentCount_ == 0 || !collecting_) {
            return {};
        }
        const auto attempt = beginAttempts_++;
        if ((attempt % sampleStride_) != 0) {
            return {};
        }
        if (activeProfiler) {
            return {};
        }
        if (!ensureQueries()) return {};
        const auto slotIndex = writeIndex_++ % slots_.size();
        auto& slot = slots_[slotIndex];
        if (slot.pending || slot.recording || !slot.disjoint ||
            !slot.timestamps[0]) {
            return {};
        }
        slot.pointCount = 0;
        slot.recording = true;
        slot.collectionRevision = collectionRequestRevision_;
        activeProfiler = this;
        context_->Begin(slot.disjoint.Get());
        context_->End(slot.timestamps[0].Get());
        return Scope(this, slotIndex);
    }

    void GpuTimingProfiler::mark(Scope& scope) noexcept
    {
        if (!context_ || scope.owner_ != this ||
            scope.slotIndex_ >= slots_.size() ||
            scope.nextPoint_ >= segmentCount_) {
            return;
        }
        auto& slot = slots_[scope.slotIndex_];
        if (!slot.recording || !slot.timestamps[scope.nextPoint_]) {
            return;
        }
        context_->End(slot.timestamps[scope.nextPoint_].Get());
        ++scope.nextPoint_;
    }

    void GpuTimingProfiler::finish(Scope& scope) noexcept
    {
        if (activeProfiler == this) {
            activeProfiler = nullptr;
        }
        if (!context_ || scope.owner_ != this ||
            scope.slotIndex_ >= slots_.size()) {
            scope.owner_ = nullptr;
            return;
        }
        auto& slot = slots_[scope.slotIndex_];
        if (scope.nextPoint_ == segmentCount_) {
            accumulatedCpuMilliseconds_ +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - scope.cpuStart_)
                    .count();
            ++completedCpuSamples_;
        }
        if (slot.recording && scope.nextPoint_ <= segmentCount_ &&
            slot.timestamps[scope.nextPoint_]) {
            context_->End(slot.timestamps[scope.nextPoint_].Get());
            context_->End(slot.disjoint.Get());
            slot.pointCount = scope.nextPoint_ + 1;
            slot.recording = false;
            slot.pending = true;
        }
        scope.owner_ = nullptr;
    }

    void GpuTimingProfiler::discard(Slot& slot) noexcept
    {
        slot.pointCount = 0;
        slot.recording = false;
        slot.pending = false;
    }

    void GpuTimingProfiler::poll() noexcept
    {
        if (!context_ || segmentCount_ == 0 || !slots_[0].disjoint) {
            return;
        }
        for (auto& slot : slots_) {
            if (!slot.pending) {
                continue;
            }
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            const auto disjointResult = context_->GetData(
                slot.disjoint.Get(),
                &disjoint,
                sizeof(disjoint),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (disjointResult == S_FALSE) {
                continue;
            }
            if (FAILED(disjointResult) || slot.pointCount == 0 ||
                slot.pointCount > segmentCount_ + 1) {
                discard(slot);
                continue;
            }
            // Retired/incomplete scopes still own timestamp results. Drain the
            // points actually submitted before reusing their query objects.
            const bool publish = !disjoint.Disjoint && disjoint.Frequency != 0 &&
                slot.pointCount == segmentCount_ + 1 &&
                slot.collectionRevision == collectionRequestRevision_;
            std::array<std::uint64_t, kMaximumSegments + 1> timestamps{};
            auto ready = true;
            auto failed = false;
            for (std::uint32_t point = 0;
                 point < slot.pointCount;
                 ++point) {
                const auto result = context_->GetData(
                    slot.timestamps[point].Get(),
                    &timestamps[point],
                    sizeof(timestamps[point]),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (result == S_FALSE) {
                    ready = false;
                    break;
                }
                if (FAILED(result)) {
                    failed = true;
                    break;
                }
            }
            if (!ready) {
                continue;
            }
            if (failed || !publish) {
                discard(slot);
                continue;
            }
            auto monotonic = true;
            for (std::uint32_t point = 1;
                 point < slot.pointCount;
                 ++point) {
                monotonic = monotonic &&
                    timestamps[point - 1] <= timestamps[point];
            }
            if (!monotonic) {
                discard(slot);
                continue;
            }
            for (std::uint32_t segment = 0;
                 segment < segmentCount_;
                 ++segment) {
                accumulatedMilliseconds_[segment] +=
                    static_cast<double>(
                        timestamps[segment + 1] - timestamps[segment]) *
                    1000.0 / static_cast<double>(disjoint.Frequency);
            }
            totalSampleMilliseconds_[completedSamples_ % totalSampleMilliseconds_.size()] =
                static_cast<double>(timestamps[segmentCount_] - timestamps[0]) *
                1000.0 / static_cast<double>(disjoint.Frequency);
            ++completedSamples_;
            discard(slot);
        }
        logIfReady();
    }

    void GpuTimingProfiler::synchronizeCollectionRequest() noexcept
    {
        const auto revision =
            collectionRequestRevision.load(std::memory_order_acquire);
        if (collectionRequestRevision_ == revision) {
            return;
        }
        collectionRequestRevision_ = revision;
        queryAllocationAttempted_ = false;
        const auto requestedGroups =
            onDemandGroups.load(std::memory_order_acquire);
        collecting_ =
            (requestedGroups & static_cast<std::uint32_t>(group_)) != 0;
        accumulatedMilliseconds_ = {};
        completedSamples_ = 0;
        accumulatedCpuMilliseconds_ = 0.0;
        completedCpuSamples_ = 0;
        nextReport_ = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
    }

    void GpuTimingProfiler::logIfReady() noexcept
    {
        if (!collecting_) return;
        const auto now = std::chrono::steady_clock::now();
        if (!collecting_ || completedSamples_ < reportSampleCount_ || !name_ ||
            now < nextReport_) {
            return;
        }
        const auto divisor = static_cast<double>(completedSamples_);
        const auto cpuAverage = completedCpuSamples_ != 0 ?
            accumulatedCpuMilliseconds_ /
                static_cast<double>(completedCpuSamples_) :
            0.0;
        std::array<double, kMaximumSegments> average{};
        double total{};
        for (std::uint32_t segment = 0;
             segment < segmentCount_;
             ++segment) {
            average[segment] = accumulatedMilliseconds_[segment] / divisor;
            total += average[segment];
        }
        try {
            const auto count = static_cast<std::size_t>((std::min)(
                completedSamples_, static_cast<std::uint64_t>(totalSampleMilliseconds_.size())));
            auto ordered = totalSampleMilliseconds_;
            std::sort(ordered.begin(), ordered.begin() + count);
            const auto p95 = (count * 95 + 99) / 100 - 1;
            spdlog::info(
                "Feature timing {} distribution: GPU min={:.3f} ms, median={:.3f} ms, p95={:.3f} ms, max={:.3f} ms over the latest {} completed samples.",
                name_, ordered[0], ordered[count / 2], ordered[p95], ordered[count - 1], count);
            if (segmentCount_ == 1) {
                spdlog::info(
                    "Feature timing {}: GPU {}={:.6f} ms, total={:.6f} ms over {} samples; CPU submit={:.6f} ms over {} calls. Timestamp queries use DONOTFLUSH.",
                    name_, labels_[0], average[0], total, completedSamples_,
                    cpuAverage, completedCpuSamples_);
            } else if (segmentCount_ == 2) {
                spdlog::info(
                    "Feature timing {}: GPU {}={:.3f} ms, {}={:.3f} ms, total={:.3f} ms over {} samples; CPU submit={:.3f} ms over {} calls. Timestamp queries use DONOTFLUSH.",
                    name_, labels_[0], average[0], labels_[1], average[1],
                    total, completedSamples_, cpuAverage,
                    completedCpuSamples_);
            } else if (segmentCount_ == 3) {
                spdlog::info(
                    "Feature timing {}: GPU {}={:.3f} ms, {}={:.3f} ms, {}={:.3f} ms, total={:.3f} ms over {} samples; CPU submit={:.3f} ms over {} calls. Timestamp queries use DONOTFLUSH.",
                    name_, labels_[0], average[0], labels_[1], average[1],
                    labels_[2], average[2], total, completedSamples_,
                    cpuAverage, completedCpuSamples_);
            } else {
                spdlog::info(
                    "Feature timing {}: GPU {}={:.3f} ms, {}={:.3f} ms, {}={:.3f} ms, {}={:.3f} ms, total={:.3f} ms over {} samples; CPU submit={:.3f} ms over {} calls. Timestamp queries use DONOTFLUSH.",
                    name_, labels_[0], average[0], labels_[1], average[1],
                    labels_[2], average[2], labels_[3], average[3], total,
                    completedSamples_, cpuAverage, completedCpuSamples_);
            }
        } catch (...) {
        }
        completedSamples_ = 0;
        accumulatedMilliseconds_ = {};
        accumulatedCpuMilliseconds_ = 0.0;
        completedCpuSamples_ = 0;
        nextReport_ = now + std::chrono::seconds(5);

    }
}
