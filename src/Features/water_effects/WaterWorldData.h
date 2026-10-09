#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace csah::water_effects
{
    struct WaterCell {float x{},y{},height{},valid{};};
    struct WaterWorldFrame
    {
        std::array<WaterCell,25> cells{};
        float seconds{};
        std::int32_t centerX{},centerY{};
        std::uint64_t revision{};
        bool valid{};
    };
    // One writer: the existing native Sky::Update callback. Consumers receive
    // value copies; no cell, worldspace or grid pointer crosses that callback.
    class WorldData final
    {
    public:
        void capture(bool enabled,bool worldReady,float deltaSeconds) noexcept;
        void invalidate() noexcept {resetEpoch_.fetch_add(1,std::memory_order_release);}
        [[nodiscard]] WaterWorldFrame read() const noexcept;
        [[nodiscard]] std::uint64_t revision() const noexcept
        {return available()?sequence_.load(std::memory_order_acquire):0;}
    private:
        [[nodiscard]] bool available() const noexcept
        {return publishedEpoch_.load(std::memory_order_acquire)==resetEpoch_.load(std::memory_order_acquire);}
        std::array<std::atomic_uint32_t,103> values_{};
        std::atomic_uint64_t sequence_{};
        std::atomic_uint64_t resetEpoch_{1},publishedEpoch_{};
        float seconds_{};
    };
}
