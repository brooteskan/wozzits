// src/engine/assets/texture/image_decode.cpp
//
// The one TU that carries stb_image's implementation. STB_IMAGE_STATIC keeps its
// symbols file-local so they never clash with the copy vendored inside pmp;
// STBI_NO_STDIO drops the file-IO surface -- assets always decode from memory
// (the RawFile dependency's bytes), never a path.

#include <engine/assets/texture/image_decode.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstddef>

namespace wz::engine::assets
{
    DecodedImage decode_image_rgba8(std::span<const uint8_t> bytes)
    {
        DecodedImage out;
        if (bytes.empty()) {
            out.error = "empty image bytes";
            return out;
        }

        int w = 0, h = 0, source_channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &w, &h, &source_channels,
            4);   // force RGBA regardless of the source channel count
        if (pixels == nullptr) {
            const char* reason = stbi_failure_reason();
            out.error = reason != nullptr ? reason : "stb_image decode failed";
            return out;
        }
        if (w <= 0 || h <= 0) {
            stbi_image_free(pixels);
            out.error = "decoded image has non-positive dimensions";
            return out;
        }

        out.width  = static_cast<uint32_t>(w);
        out.height = static_cast<uint32_t>(h);
        const std::size_t byte_count =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        out.rgba8.assign(pixels, pixels + byte_count);
        stbi_image_free(pixels);
        out.ok = true;
        return out;
    }
}
