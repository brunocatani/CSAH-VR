#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace csah::render
{
    // Creation-time SM5 declaration inspection, including stripped native
    // shaders without RDEF reflection. No shader data is retained.
    [[nodiscard]] inline bool declaresTexture2D(const void* bytes,std::size_t size,std::uint32_t slot) noexcept
    {
        if(!bytes || size<32) return false;
        const auto* data=static_cast<const std::byte*>(bytes);
        const auto word=[&](std::size_t offset) {std::uint32_t value{};std::memcpy(&value,data+offset,4);return value;};
        if(word(0)!=0x43425844u || word(24)>size) return false;
        const auto count=word(28);if(count>(size-32)/4) return false;
        for(std::uint32_t chunk=0;chunk<count;chunk++) {
            const auto offset=word(32+chunk*4);if(offset>size-8) return false;
            const auto tag=word(offset),length=word(offset+4);
            if(length>size-offset-8) return false;
            if(tag!=0x58454853u && tag!=0x52444853u) continue;
            if(length<8 || length%4) return false;
            const auto words=word(offset+12);if(words<2 || words>length/4) return false;
            for(std::uint32_t index=2;index<words;) {
                const auto token=word(offset+8+index*4),opcode=token&0x7ff;
                auto instructionLength=(token>>24)&0x7f;
                if(opcode==0x35) {if(index+1>=words) return false;instructionLength=word(offset+8+(index+1)*4);}
                if(!instructionLength || instructionLength>words-index) return false;
                if(opcode==0x58 && ((token>>11)&31)==3 && instructionLength==4 &&
                    word(offset+8+(index+2)*4)==slot) return true;
                index+=instructionLength;
            }
        }
        return false;
    }
}
