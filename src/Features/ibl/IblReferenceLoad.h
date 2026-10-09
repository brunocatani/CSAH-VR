#pragma once
#include <array>
#include <cstddef>

namespace csah::ibl
{
    // The engine destructor already releases the stream member. Store its
    // object in raw, aligned value storage so C++ cannot destroy that member
    // a second time after the native destructor returns.
    template <class Stream>
    class NativeStreamOwner final
    {
    public:
        template <class Construct>
        NativeStreamOwner(Construct&& construct, void (*destroy)(Stream*)) : destroy_(destroy)
        {
            construct(storage_.data());
        }
        ~NativeStreamOwner() { destroy_(&get()); }
        NativeStreamOwner(const NativeStreamOwner&) = delete;
        NativeStreamOwner& operator=(const NativeStreamOwner&) = delete;
        Stream& get() noexcept { return *reinterpret_cast<Stream*>(storage_.data()); }
    private:
        alignas(Stream) std::array<std::byte, sizeof(Stream)> storage_{};
        void (*destroy_)(Stream*);
    };

    // The VR renderer consumes NiTexture::stream synchronously. Keep the
    // opened stream owner alive across creation and never use a filename-only
    // texture factory: that overload leaves NiTexture::stream null in FO4VR.
    template <class OpenedStream, class CreateTexture>
    auto createTextureFromOpenedStream(
        OpenedStream& opened,
        const char* path,
        CreateTexture&& create)
        -> decltype(create(opened.stream, path, false, false, false))
    {
        if (!path || !*path || !opened.stream ||
            opened.lastError != decltype(opened.lastError){}) {
            return {};
        }
        return create(opened.stream, path, false, false, false);
    }
}
