#pragma once
// window_engine/engine/rendering/clipmap_drawn_surface.h
//
// CPU mirror of the height tap in the clipmap vertex shader.
//
// The geometry clipmap DRAWS a heightfield through resources/shaders/clipmap/
// clipmap_vs.hlsl, which displaces each lattice vertex by sampling the resident
// height texture. Anything CPU-side that needs to agree with what is on screen
// -- a terrain constraint that must put an actor on the drawn ground, a ray that
// must strike the drawn hill -- has to reproduce that tap, not the true field.
//
// This is the tap and nothing else: one bilinear read of one mip, with the
// shader's exact uv convention. Ring selection, the per-level snap and the
// geomorph are built on top of it elsewhere.
//
// It is a MIRROR, not shared source: HLSL is compiled by D3DCompile at asset
// -resolve time and has no include path, so the two texts are separate by
// construction (see clipmap_vs.hlsl's sample_height_world). What keeps them
// honest is the on-device pinning test, which dispatches the real shader math
// over a probe grid and requires this function to reproduce it. Exact equality
// is NOT achievable and is not the bar: D3D guarantees only 8 bits of subtexel
// precision on the bilinear weight, so the tolerance is set by the sampler
// rather than by this arithmetic. Change either side and expect to change both.

#include <cstdint>
#include <span>

namespace wz::engine::rendering
{
    // One mip level of a height pyramid, row-major, index x + y*width.
    struct ClipmapHeightMipView
    {
        const float* values = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // A heightfield as the clipmap sees it: a mip chain plus the world-space
    // footprint it is stretched over. `levels[0]` is mip 0; coarser levels
    // follow. The footprint is the same origin/extent the renderable and the
    // collision both take from their shared Placement, so a caller on either
    // side describes the same surface.
    struct ClipmapHeightFieldView
    {
        std::span<const ClipmapHeightMipView> levels;
        float world_origin[2]{ 0.0f, 0.0f };
        float world_size[2]{ 1.0f, 1.0f };

        [[nodiscard]] bool valid() const noexcept
        {
            return !levels.empty()
                && levels[0].values != nullptr
                && levels[0].width > 0u
                && levels[0].height > 0u
                && world_size[0] > 0.0f
                && world_size[1] > 0.0f;
        }
    };

    // The height clipmap_vs.hlsl's sample_height_world returns at a world XZ,
    // reading `mip` (clamped to the chain). Bilinear, clamp addressing.
    //
    // The shader only ever passes an integral mip -- it comes from the lattice
    // vertex's LOD tag -- so this takes an integer rather than reproducing
    // trilinear blending that never happens.
    [[nodiscard]] float clipmap_sample_height_world(
        const ClipmapHeightFieldView& field,
        float world_x,
        float world_z,
        uint32_t mip) noexcept;
}
