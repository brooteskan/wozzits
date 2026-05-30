#include <engine/assets/hdri/hdri_image_loader.h>

#include <tinyexr.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        struct TinyEXRError
        {
            const char* message = nullptr;

            ~TinyEXRError()
            {
                if (message) {
                    FreeEXRErrorMessage(message);
                }
            }
        };
    }

    bool load_openexr_image_from_memory(
        std::span<const uint8_t> bytes,
        HDRImageData& out,
        std::string& error)
    {
        out = {};
        error.clear();

        if (bytes.empty()) {
            error = "OpenEXR image payload is empty";
            return false;
        }

        float* rgba = nullptr;
        int width = 0;
        int height = 0;
        TinyEXRError tiny_error{};

        const int result = LoadEXRFromMemory(
            &rgba,
            &width,
            &height,
            bytes.data(),
            bytes.size(),
            &tiny_error.message);

        if (result != TINYEXR_SUCCESS) {
            error = tiny_error.message
                ? tiny_error.message
                : "TinyEXR failed to decode OpenEXR image";
            return false;
        }

        if (!rgba || width <= 0 || height <= 0) {
            std::free(rgba);
            error = "TinyEXR returned an invalid OpenEXR image";
            return false;
        }

        const size_t pixel_count =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        HDRImageData decoded{};
        decoded.width = static_cast<uint32_t>(width);
        decoded.height = static_cast<uint32_t>(height);
        decoded.channels = 4;
        decoded.pixels.resize(pixel_count * decoded.channels);
        std::copy(
            rgba,
            rgba + decoded.pixels.size(),
            decoded.pixels.begin());
        std::free(rgba);

        if (!decoded.valid()) {
            error = "decoded OpenEXR image is internally inconsistent";
            return false;
        }

        out = std::move(decoded);
        return true;
    }

} // namespace wz::engine::assets
