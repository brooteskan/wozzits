#pragma once

// engine/assets/hdri/hdri_image_loader.h
//
// Engine-facing HDR image decode boundary. Third-party EXR/HDR loaders stay
// behind this interface so HDRI assets can work with decoded float pixels
// without depending on a particular library's API or allocation rules.

#include <cstdint>
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

} // namespace wz::engine::assets
