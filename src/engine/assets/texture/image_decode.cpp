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

// The stb entry points this TU deliberately does not call.
//
// STB_IMAGE_STATIC gives every stb symbol internal linkage, so each entry point
// we never call is a -Wunused-function. Naming them here rather than switching
// the warning off for the whole header keeps two properties worth having: you
// delete a line the day you start using one, and a new stb version that adds an
// entry point warns until someone decides which side of this list it lands on.
//
// The engine calls exactly two of them -- stbi_info_from_memory to bound the
// dimensions from the header, and stbi_load_from_memory to decode. Everything
// below is API surface that comes with the implementation, not capability we
// have given up: the codecs are all still compiled in, because decode_image_rgba8
// is fed arbitrary authored bytes (texture RawFile deps, Inochi embedded
// textures) and must keep sniffing whatever format those turn out to be. If we
// ever want the binary smaller, the lever is STBI_NO_* above, and that is a
// decision about what the engine can load -- not a warning cleanup.
//
// Taking the address is what marks a function used; the enclosing function is
// [[maybe_unused]] so it does not become the next unused-function warning
// itself. Nothing here is referenced, so the linker still drops all of it.
[[maybe_unused]] static void stb_entry_points_this_tu_does_not_call()
{
    // Orientation and premultiply options -- we decode as authored.
    (void)&stbi_set_flip_vertically_on_load;
    (void)&stbi_set_flip_vertically_on_load_thread;
    (void)&stbi_set_unpremultiply_on_load;
    (void)&stbi_set_unpremultiply_on_load_thread;
    (void)&stbi_convert_iphone_png_to_rgb;
    (void)&stbi_convert_iphone_png_to_rgb_thread;

    // Callback-driven input. Assets always arrive as one contiguous span.
    (void)&stbi_load_from_callbacks;
    (void)&stbi_load_16_from_callbacks;
    (void)&stbi_loadf_from_callbacks;
    (void)&stbi_info_from_callbacks;
    (void)&stbi_is_16_bit_from_callbacks;
    (void)&stbi_is_hdr_from_callbacks;

    // Non-RGBA8 results. decode_image_rgba8's contract is 8 bits per channel;
    // 16-bit and float paths would need a different DecodedImage.
    (void)&stbi_load_16_from_memory;
    (void)&stbi_is_16_bit_from_memory;
    (void)&stbi_loadf_from_memory;
    (void)&stbi_is_hdr_from_memory;
    (void)&stbi_ldr_to_hdr_gamma;
    (void)&stbi_ldr_to_hdr_scale;
    (void)&stbi_hdr_to_ldr_gamma;
    (void)&stbi_hdr_to_ldr_scale;

    // Multi-frame GIF. Nothing authors animated textures.
    (void)&stbi_load_gif_from_memory;

    // stb's zlib helpers, exposed because the PNG decoder needs zlib. We have
    // no standalone zlib caller.
    (void)&stbi_zlib_decode_malloc;
    (void)&stbi_zlib_decode_buffer;
    (void)&stbi_zlib_decode_noheader_malloc;
    (void)&stbi_zlib_decode_noheader_buffer;
}

#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    DecodedImage decode_image_rgba8(std::span<const uint8_t> bytes)
    {
        DecodedImage out;
        if (bytes.empty()) {
            out.error = "empty image bytes";
            return out;
        }

        // stb takes an int length; a span larger than INT_MAX would arrive
        // negative, and buffer+len with a negative len is out-of-bounds pointer
        // arithmetic before stb ever looks at it. Refuse rather than narrow.
        if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
            out.error = "image is too large to decode (over 2 GiB)";
            return out;
        }

        // Bound the dimensions from the HEADER, before stb allocates anything
        // (issue #310, A4-C12). stb sizes its raw buffer from the declared
        // dimensions before decompressing, so without this a ~1 MB file whose
        // header claims 16384x16384 RGBA costs a gigabyte -- and costs it even
        // when the compressed payload is truncated and the decode then fails.
        // In the Inochi path that gigabyte was also RETAINED for the asset's
        // lifetime, once per texture entry, with nothing bounding the entries.
        {
            int info_w = 0, info_h = 0, info_channels = 0;
            if (stbi_info_from_memory(
                    bytes.data(),
                    static_cast<int>(bytes.size()),
                    &info_w, &info_h, &info_channels) == 0)
            {
                const char* reason = stbi_failure_reason();
                out.error = reason != nullptr
                    ? std::string("image header rejected: ") + reason
                    : "image header could not be read";
                return out;
            }

            if (info_w <= 0 || info_h <= 0) {
                out.error = "image header declares non-positive dimensions";
                return out;
            }

            const auto uw = static_cast<uint32_t>(info_w);
            const auto uh = static_cast<uint32_t>(info_h);
            if (uw > kMaxImageAxis || uh > kMaxImageAxis) {
                out.error =
                    "image is " + std::to_string(uw) + "x" + std::to_string(uh)
                    + ", which exceeds the maximum axis of "
                    + std::to_string(kMaxImageAxis);
                return out;
            }

            // 64-bit product: both axes are individually inside the cap here,
            // so this is the only place the total can still be unreasonable.
            const std::uint64_t decoded_bytes =
                static_cast<std::uint64_t>(uw) * static_cast<std::uint64_t>(uh) * 4ull;
            if (decoded_bytes > kMaxDecodedImageBytes) {
                out.error =
                    "image would decode to " + std::to_string(decoded_bytes)
                    + " bytes, over the " + std::to_string(kMaxDecodedImageBytes)
                    + " byte limit";
                return out;
            }
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

        // assign() ran BEFORE stbi_image_free, so a throw from the copy leaked
        // stb's decoded buffer permanently (issue #310, A4-C13). That is not
        // hypothetical: at this point stb is holding w*h*4 bytes and the copy is
        // asking the allocator for a second block of exactly the same size, so
        // it is the single most likely allocation in the loader to fail -- and
        // the .inp texture path can reach here with ~1 GiB blocks. Nothing
        // between here and the ABI catches, so each retry leaked again.
        //
        // A scope guard rather than reordering, because the copy genuinely has
        // to happen before the free.
        struct StbPixelsGuard
        {
            stbi_uc* p;
            ~StbPixelsGuard() { if (p != nullptr) stbi_image_free(p); }
        } guard{ pixels };

        out.rgba8.assign(pixels, pixels + byte_count);
        out.ok = true;
        return out;
    }
}
