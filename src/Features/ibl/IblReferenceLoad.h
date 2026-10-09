#pragma once

namespace csah::ibl
{
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
