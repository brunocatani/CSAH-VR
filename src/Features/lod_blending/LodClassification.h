#pragma once
#include <cstddef>
namespace csah::lod_blending
{
    // Bound malformed/cyclic graphs; a missing root can never classify a draw.
    template<class Parent>
    [[nodiscard]] bool beneathRoot(const void* node,const void* root,Parent&& parent) noexcept
    {
        if(!root) return false;
        for(std::size_t depth=0;node && depth<64;++depth) {
            if(node==root) return true;
            const auto* next=parent(node);
            if(next==node) return false;
            node=next;
        }
        return false;
    }
}
