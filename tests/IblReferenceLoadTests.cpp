#include "Features/ibl/IblReferenceLoad.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    enum class Error { none, missing, invalid, ioFailure };
    struct Resource {};
    struct OpenedStream
    {
        std::shared_ptr<Resource> stream;
        Error lastError{};
    };
    struct Texture
    {
        std::shared_ptr<Resource> stream;
    };
    struct NativeProbe
    {
        int* destructions;
        ~NativeProbe() { ++*destructions; }
    };
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
}

int main()
{
    try {
        constexpr auto path = "textures\\shared\\cubemaps\\mipblur_DefaultOutside1.dds";
        int calls{};
        auto source = std::make_shared<Resource>();
        const auto* expectedSource = source.get();
        const auto create = [&](auto& stream, const char* name, bool ddx, bool srgb, bool degrade) {
            ++calls;
            require(stream && stream.get() == expectedSource, "factory did not receive the opened resource");
            require(std::string(name) == path, "factory received the wrong name");
            require(!ddx && !srgb && !degrade, "reference load flags changed");
            return std::make_unique<Texture>(Texture{stream});
        };

        // Reproduce the crashing precondition: a plausible filename with no
        // backing stream must never reach the engine's renderer callback.
        OpenedStream unopened{};
        require(!csah::ibl::createTextureFromOpenedStream(unopened, path, create), "unopened resource was accepted");
        for (auto error : {Error::missing, Error::invalid, Error::ioFailure}) {
            OpenedStream failed{source, error};
            require(!csah::ibl::createTextureFromOpenedStream(failed, path, create), "failed resource reached texture creation");
        }
        OpenedStream opened{source, Error::none};
        require(!csah::ibl::createTextureFromOpenedStream(opened, nullptr, create), "null name accepted");
        require(!csah::ibl::createTextureFromOpenedStream(opened, "", create), "empty name accepted");
        require(calls == 0, "invalid load called the native factory");

        auto texture = csah::ibl::createTextureFromOpenedStream(opened, path, create);
        require(texture && calls == 1, "valid opened stream did not create a texture");
        std::weak_ptr<Resource> lifetime = source;
        source.reset();
        opened.stream.reset();
        require(!lifetime.expired(), "resource died before its texture consumer");
        texture.reset();
        require(lifetime.expired(), "resource ownership leaked after texture release");
        int nativeDestructions{};
        {
            csah::ibl::NativeStreamOwner<NativeProbe> nativeOwner(
                [&](void* storage) { std::construct_at(static_cast<NativeProbe*>(storage), &nativeDestructions); },
                [](NativeProbe* object) { std::destroy_at(object); });
            require(nativeOwner.get().destructions == &nativeDestructions, "native stream storage changed");
        }
        require(nativeDestructions == 1, "native stream members were destroyed more than once");
        std::cout << "Cubemap reference: unopened/failed resources rejected, stream overload arguments and ownership preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
