#pragma once

// engine/assets/texture/image_decode.h
//
// Decode whole-file image bytes (PNG / JPG / BMP / ...) to tightly-packed 8-bit
// RGBA, isolating stb_image behind a small API so its single-header
// implementation lives in exactly one TU (image_decode.cpp). The HDRI/EXR loader
// (tinyexr) is the sibling for high-dynamic-range sources; this is the LDR path.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DecodedImage
    {
        uint32_t             width  = 0;
        uint32_t             height = 0;
        std::vector<uint8_t> rgba8;   // width * height * 4, row-major, no padding
        bool                 ok = false;
        std::string          error;
    };

    // Upper bounds applied BEFORE any pixel memory is allocated (issue #310,
    // A4-C12). Both numbers are anchored on something real rather than chosen
    // for feel:
    //
    //   kMaxImageAxis = 16384 is D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION. An image
    //   larger than this on either axis can never become a 2D texture on this
    //   backend, so decoding it cannot lead anywhere useful.
    //
    //   kMaxDecodedImageBytes = 256 MiB bounds the ONE thing the axis limit
    //   does not: 16384 x 16384 is inside the axis cap and is still a gigabyte
    //   of RGBA. 256 MiB is ~4x the largest plausible single source here (a
    //   4096-square atlas page is 64 MiB) and comfortably above any texture the
    //   engine ships.
    //
    // The bound has to be applied before decoding, not after, because the
    // damage is done inside stb: it allocates the full raw buffer from the
    // header dimensions BEFORE decompressing, so a ~1 MB file whose IHDR
    // declares 16384x16384 costs a gigabyte even when its IDAT is truncated
    // and the decode ultimately fails. stbi_info_from_memory reads only the
    // header, which is what makes checking first possible.
    inline constexpr uint32_t    kMaxImageAxis         = 16384u;
    inline constexpr std::size_t kMaxDecodedImageBytes = 256u * 1024u * 1024u;

    // Decode `bytes` (a complete image file) to RGBA8. Forces 4 channels so the
    // result always has a valid RGBA layout. Refuses, without allocating pixel
    // memory, any image exceeding the limits above. On failure returns
    // ok == false with `error` set; never throws.
    DecodedImage decode_image_rgba8(std::span<const uint8_t> bytes);
}
