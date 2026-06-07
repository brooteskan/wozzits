#pragma once

#include <cstdint>

namespace wz::engine::assets::internal
{
    struct DiskCacheKeySpec
    {
        const char* subdirectory;
        uint64_t seed_lo;
        uint64_t seed_hi;
    };

    inline constexpr DiskCacheKeySpec kScalarFieldDiskCacheKey{
        "scalar_field",
        0x912a3f5dc8732019ull,
        0x6c8e9cf570932bd5ull,
    };

    inline constexpr DiskCacheKeySpec kGLBMeshDiskCacheKey{
        "glb_mesh",
        0x452821e638d01377ull,
        0xbe5466cf34e90c6cull,
    };

    inline constexpr DiskCacheKeySpec kMeshTerrainDiskCacheKey{
        "mesh_terrain",
        0xa4093822299f31d0ull,
        0x082efa98ec4e6c89ull,
    };

    inline constexpr DiskCacheKeySpec kCollisionTerrainDiskCacheKey{
        "collision_terrain",
        0x243f6a8885a308d3ull,
        0x13198a2e03707344ull,
    };

    inline constexpr DiskCacheKeySpec kTerrainVisualProxyDiskCacheKey{
        "terrain_visual_proxy",
        0xe7037ed1b5f41a29ull,
        0x9f86d081884c7d65ull,
    };
}
