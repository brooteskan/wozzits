#pragma once

// engine/assets/hdri/hdri_image_loader.h
//
// Engine-facing HDR image decode/encode boundary. Third-party EXR/HDR codecs
// stay behind this interface so engine code works with decoded float pixels
// without depending on a particular library's API or allocation rules. The
// TinyEXR include lives in exactly one place (hdri_image_loader_tinyexr.cpp).

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct HDRImageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        std::vector<float> pixels;

        bool valid() const noexcept
        {
            return width > 0
                && height > 0
                && channels > 0
                && pixels.size()
                    == static_cast<size_t>(width)
                    * static_cast<size_t>(height)
                    * static_cast<size_t>(channels);
        }
    };

    [[nodiscard]] bool load_openexr_image_from_memory(
        std::span<const uint8_t> bytes,
        HDRImageData& out,
        std::string& error);

    [[nodiscard]] bool load_openexr_image_from_file_cached(
        const std::string& path,
        std::shared_ptr<const HDRImageData>& out,
        std::string& error);

    [[nodiscard]] bool openexr_image_file_identity_key(
        const std::string& path,
        std::string& out,
        std::string& error);

    // Encode `image` (interleaved RGB or RGBA float, per its `channels`) as an
    // fp32 OpenEXR and write it to `path`. Encoding stays behind this boundary
    // (TinyEXR); the file write goes through wz::fs, mirroring the load path.
    // Returns false and sets `error` on failure.
    [[nodiscard]] bool save_openexr_image_to_file(
        const std::string& path,
        const HDRImageData& image,
        std::string& error);

} // namespace wz::engine::assets
