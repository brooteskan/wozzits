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
    // The geomorph band, as a fraction of a ring's world half-extent. MUST equal
    // CLIPMAP_MORPH_START / CLIPMAP_MORPH_END in clipmap_vs.hlsl -- the shader
    // tunes transition quality with these, and a mismatch moves the CPU
    // surface's transition band away from the drawn one.
    inline constexpr float kClipmapMorphStart = 0.80f;
    inline constexpr float kClipmapMorphEnd = 0.98f;

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

    // How the lattice is placed for a frame: the observer it is centred on plus
    // the schedule it was built from. c0 / base_resolution / level_count all
    // come from the ClipmapLatticeSchedule the lattice mesh was built from, so
    // a caller reading the same schedule describes the same rings.
    struct ClipmapDrawnSurfaceParams
    {
        float observer_xz[2]{ 0.0f, 0.0f };
        float c0 = 1.0f;                 // finest lattice cell, metres
        uint32_t base_resolution = 0;    // m: cells across a ring
        uint32_t level_count = 0;        // L: number of rings

        [[nodiscard]] bool valid() const noexcept
        {
            return c0 > 0.0f && base_resolution >= 2u && level_count >= 1u;
        }
    };

    // The coarser ring's TRIANGULATED height at a world XZ -- the geomorph
    // target. Mirrors sample_height_coarse: four taps on the level-(L+1) lattice
    // grid, bilinear-blended, NOT one texture tap at the point. The distinction
    // matters because the rasteriser interpolates linearly between the coarse
    // ring's vertices, and a texture bilinear has knots elsewhere.
    [[nodiscard]] float clipmap_sample_height_coarse(
        const ClipmapHeightFieldView& field,
        float world_x,
        float world_z,
        float two_cL,
        uint32_t coarse_mip) noexcept;

    // The height the VS gives a lattice vertex of ring `level` sitting at
    // lattice offset (g_x, g_z) from the ring centre: the per-level snap, the
    // mip-L tap, and the geomorph blend toward the coarser ring's triangulated
    // surface over the ring's outer band.
    //
    // Includes the interior position trim: at the ring's outer edge the vertex
    // itself MOVES, its snap blending toward the next coarser ring's, and it
    // reads the height wherever it lands. That was nearly left out on the
    // argument that the trim's endpoints coincide with the untrimmed grid --
    // true, but at intermediate blends the vertex sits up to a cell away, and
    // the pinning test measures that as a real height difference.
    //
    // Mirrors the shader including its quirks. In particular the OUTERMOST ring
    // still morphs toward a level that is not drawn -- the VS has no
    // level_count to know it is last -- so the outer band is coarsened a little
    // more than the ring itself. That is what is on screen, so it is what this
    // reproduces.
    [[nodiscard]] float clipmap_drawn_vertex_height(
        const ClipmapHeightFieldView& field,
        const ClipmapDrawnSurfaceParams& params,
        uint32_t level,
        float g_x,
        float g_z) noexcept;

    // The drawn surface at an arbitrary world XZ: pick the ring covering the
    // point, then blend the four surrounding lattice vertices of that ring the
    // way the rasteriser does across a cell.
    //
    // Unlike the functions above this is not a line-for-line shader mirror --
    // the GPU gets ring membership and cell topology from the lattice MESH, not
    // from code. It is the CPU statement of what that mesh means.
    //
    // Each vertex carries its own trim and geomorph (see above), so the one
    // term left out here is the exact diagonal split of each quad: this blends
    // the four corners bilinearly rather than picking the triangle the point
    // actually falls in. Measured at 0.03 m RMS against this clipmap's real
    // heightfield, versus the 1.2-3.2 m the reconstruction as a whole closes.
    [[nodiscard]] float clipmap_drawn_surface_height(
        const ClipmapHeightFieldView& field,
        const ClipmapDrawnSurfaceParams& params,
        float world_x,
        float world_z) noexcept;
}
