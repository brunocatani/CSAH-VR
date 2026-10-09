#include "Features/water_effects/WaterWorldData.h"
#include <Windows.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
namespace csah::water_effects
{
    namespace
    {
        template<class T> bool readValue(std::uintptr_t address,T& value) noexcept
        {
            MEMORY_BASIC_INFORMATION memory{};
            if(!address || !VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory)) ||
                memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
            const auto begin=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            if(address<begin || sizeof(T)>memory.RegionSize || address-begin>memory.RegionSize-sizeof(T)) return false;
            std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return true;
        }
    }
    void WorldData::capture(bool enabled,bool worldReady,float deltaSeconds) noexcept
    {
        if(!enabled || !worldReady) {invalidate();return;}
        const auto epoch=resetEpoch_.load(std::memory_order_acquire);
        static const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uintptr_t tes{},interior{},grid{},array{};
        std::int32_t centerX{},centerY{};std::uint32_t dimension{};
        // FO4VR 1.2.72: 14074B6F3 and 140396320 load this TES singleton.
        // 1400F5360 / 1400F8A10 independently establish center +48/+4C,
        // interior +58 and grid +18. Grid get 1401DCAB0 and set 1401DCB20
        // agree on dimension +10, array +18, and x*dimension+y ordering.
        if(!readValue(image+0x05B042C0,tes) || !tes ||
            !readValue(tes+0x58,interior) || interior || !readValue(tes+0x18,grid) || !grid ||
            !readValue(tes+0x48,centerX) || !readValue(tes+0x4c,centerY) ||
            centerX==INT32_MAX || centerY==INT32_MAX || !readValue(grid+0x10,dimension) ||
            dimension==0 || !readValue(grid+0x18,array) || !array) {invalidate();return;}
        WaterWorldFrame frame;
        const auto half=static_cast<std::int64_t>(dimension/2);
        using Height=float(__fastcall*)(const void*);
        const auto height=reinterpret_cast<Height>(image+0x0039B410);
        for(int x=0;x<5;x++) for(int y=0;y<5;y++) {
            const auto gridX=half+x-2,gridY=half+y-2;
            if(gridX<0 || gridY<0 || gridX>=dimension || gridY>=dimension) continue;
            const auto index=static_cast<std::uint64_t>(gridX)*dimension+gridY;
            if(index>((std::numeric_limits<std::uintptr_t>::max)()-array)/sizeof(void*)) continue;
            std::uintptr_t cell{},coordinates{},world{};std::uint16_t flags{};std::uint8_t state{};std::array<std::int32_t,2> position{};
            if(!readValue(array+index*sizeof(void*),cell) || !cell ||
                !readValue(cell+0x40,flags) || (flags&3)!=2 || !readValue(cell+0x44,state) || state!=7 ||
                !readValue(cell+0x50,coordinates) || !coordinates ||
                !readValue(coordinates,position) || position[0]!=static_cast<std::int64_t>(centerX)+x-2 || position[1]!=static_cast<std::int64_t>(centerY)+y-2 ||
                !readValue(cell+0xc8,world)) continue;
            float storedHeight{};if(!readValue(cell+0x60,storedHeight)) continue;
            // 1401DCB60 and 14039652E use state 7 for attached VR cells.
            // 14039B410 selects the cell height or parent-world water height;
            // 1403F8E90 and 1407BFC80 independently consume its float result.
            // Its sentinel at 142C81BEC is -FLT_MAX, not a water plane at zero.
            const auto z=height(reinterpret_cast<const void*>(cell));
            if(std::isfinite(z) && z>-(std::numeric_limits<float>::max)()) frame.cells[x*5+y]={float(position[0]),float(position[1]),z,1};
        }
        if(std::isfinite(deltaSeconds) && deltaSeconds>=0) seconds_=std::fmod(seconds_+deltaSeconds,1000.0f);
        sequence_.fetch_add(1,std::memory_order_acq_rel);
        for(std::size_t i=0;i<frame.cells.size();i++) {
            const auto& c=frame.cells[i];const std::array data{c.x,c.y,c.height,c.valid};
            for(std::size_t j=0;j<4;j++) values_[i*4+j].store(std::bit_cast<std::uint32_t>(data[j]),std::memory_order_relaxed);
        }
        values_[100].store(std::bit_cast<std::uint32_t>(seconds_),std::memory_order_relaxed);
        values_[101].store(std::bit_cast<std::uint32_t>(centerX),std::memory_order_relaxed);
        values_[102].store(std::bit_cast<std::uint32_t>(centerY),std::memory_order_relaxed);
        sequence_.fetch_add(1,std::memory_order_release);
        // A load/reset arriving during capture cannot re-enable the old world.
        publishedEpoch_.store(epoch,std::memory_order_release);
    }
    WaterWorldFrame WorldData::read() const noexcept
    {
        if(!available()) return {};
        for(unsigned attempt=0;attempt<3;attempt++) {
            const auto before=sequence_.load(std::memory_order_acquire);if(before&1) continue;
            WaterWorldFrame result;
            for(std::size_t i=0;i<result.cells.size();i++) {
                std::array<float,4> data{};for(std::size_t j=0;j<4;j++) data[j]=std::bit_cast<float>(values_[i*4+j].load(std::memory_order_relaxed));
                result.cells[i]={data[0],data[1],data[2],data[3]};
            }
            result.seconds=std::bit_cast<float>(values_[100].load(std::memory_order_relaxed));
            result.centerX=std::bit_cast<std::int32_t>(values_[101].load(std::memory_order_relaxed));
            result.centerY=std::bit_cast<std::int32_t>(values_[102].load(std::memory_order_relaxed));
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto after=sequence_.load(std::memory_order_relaxed);
            if(before==after && available()) {result.revision=after;result.valid=true;return result;}
        }
        return {};
    }
}
