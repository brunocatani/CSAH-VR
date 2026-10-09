#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace csah::render
{
    class GpuTimingProfiler final
    {
    public:
        enum class Group : std::uint32_t
        {
            ContactShadows = 1u,
            Skylighting = 2u,
            General = 4u,
        };

        static constexpr std::size_t kMaximumSegments = 4;
        static constexpr std::size_t kSlotCount = 8;
        static constexpr std::uint32_t kMaximumReports = 6;

        class Scope final
        {
        public:
            Scope() noexcept = default;
            ~Scope() noexcept;
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;
            Scope(Scope&& other) noexcept;
            Scope& operator=(Scope&& other) noexcept;

            void mark() noexcept;
            void finish() noexcept;
            [[nodiscard]] explicit operator bool() const noexcept
            {
                return owner_ != nullptr;
            }

        private:
            friend class GpuTimingProfiler;
            Scope(
                GpuTimingProfiler* owner,
                std::size_t slotIndex) noexcept;

            GpuTimingProfiler* owner_{};
            std::size_t slotIndex_{};
            std::uint32_t nextPoint_{ 1 };
            std::chrono::steady_clock::time_point cpuStart_{};
        };

        [[nodiscard]] bool initialize(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            const char* name,
            const std::array<const char*, kMaximumSegments>& labels,
            std::uint32_t segmentCount,
            std::uint32_t reportSampleCount = 120,
            std::uint32_t sampleStride = 1,
            Group group = Group::General) noexcept;
        static void setOnDemandGroups(std::uint32_t groups) noexcept;
        void reset() noexcept;
        [[nodiscard]] Scope begin() noexcept;
        void poll() noexcept;

    private:
        struct Slot
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            std::array<
                Microsoft::WRL::ComPtr<ID3D11Query>,
                kMaximumSegments + 1>
                timestamps;
            std::uint32_t pointCount{};
            bool recording{};
            bool pending{};
            std::uint64_t collectionRevision{};
        };

        void mark(Scope& scope) noexcept;
        void finish(Scope& scope) noexcept;
        void discard(Slot& slot) noexcept;
        void logIfReady() noexcept;
        void synchronizeCollectionRequest() noexcept;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        std::array<Slot, kSlotCount> slots_{};
        std::array<const char*, kMaximumSegments> labels_{};
        std::array<double, kMaximumSegments> accumulatedMilliseconds_{};
        const char* name_{};
        std::uint32_t segmentCount_{};
        std::uint32_t reportSampleCount_{ 120 };
        std::uint32_t sampleStride_{ 1 };
        std::size_t writeIndex_{};
        std::uint64_t beginAttempts_{};
        std::uint64_t completedSamples_{};
        // Bounded recent-sample window for diagnosing spikes during A/B runs.
        std::array<double, 1024> totalSampleMilliseconds_{};
        double accumulatedCpuMilliseconds_{};
        std::uint64_t completedCpuSamples_{};
        std::chrono::steady_clock::time_point nextReport_{};
        std::uint32_t reportsEmitted_{};
        std::uint64_t collectionRequestRevision_{};
        Group group_{ Group::General };
        bool continuousCollection_{};
        bool collecting_{};
    };
}
