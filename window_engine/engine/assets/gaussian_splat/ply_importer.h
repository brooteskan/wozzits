#pragma once

// engine/assets/gaussian_splat/ply_importer.h

#include <engine/assets/gaussian_splat/gaussian_splat.h>

#include <cstddef>
#include <cstdint>

namespace wz::engine::assets
{
    bool import_ascii_ply_gaussian_splats(
        const std::uint8_t* bytes,
        std::size_t byte_count,
        GaussianSplatCloudData& out);
}