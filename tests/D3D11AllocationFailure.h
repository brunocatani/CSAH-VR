#pragma once
#include <d3d11.h>
#include <cstdlib>
#include <stdexcept>

// Test-only fault injection on the isolated WARP device. Restore the original
// method before returning to normal resource creation.
class DeviceFailureScope final
{
public:
    DeviceFailureScope(ID3D11Device* device, std::size_t slot, void* replacement) :
        entry_(*reinterpret_cast<void***>(device) + slot), original_(*entry_)
    {
        DWORD previous{}, ignored{};
        if (!VirtualProtect(entry_, sizeof(void*), PAGE_READWRITE, &previous))
            throw std::runtime_error("test device vtable access failed");
        *entry_ = replacement;
        VirtualProtect(entry_, sizeof(void*), previous, &ignored);
    }
    ~DeviceFailureScope()
    {
        DWORD previous{}, ignored{};
        if (!VirtualProtect(entry_, sizeof(void*), PAGE_READWRITE, &previous)) std::abort();
        *entry_ = original_;
        VirtualProtect(entry_, sizeof(void*), previous, &ignored);
    }
    DeviceFailureScope(const DeviceFailureScope&) = delete;
    DeviceFailureScope& operator=(const DeviceFailureScope&) = delete;
private:
    void** entry_;
    void* original_;
};
