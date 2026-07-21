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

    // Decode `bytes` (a complete image file) to RGBA8. Forces 4 channels so the
    // result always has a valid RGBA layout. On failure returns ok == false with
    // `error` set; never throws.
    DecodedImage decode_image_rgba8(std::span<const uint8_t> bytes);
}
