// src/engine/rendering/clipmap_view.cpp

#include <engine/rendering/clipmap_view.h>

#include <cmath>

namespace wz::engine::rendering
{
    uint32_t clipmap_lattice_grid_extent(
        const assets::ClipmapLatticeParams& lattice) noexcept
    {
        // Mirror the lattice generator's extent math so view placement and the
        // emitted geometry agree (see make_clipmap_lattice_mesh): sanitize the
        // params, then the lattice spans fine_extent = 2 * half_extent finest
        // cells, where half_extent = (m/2) * 2^(L-1).
        const assets::ClipmapLatticeParams params =
            assets::sanitize_clipmap_lattice_params(
                lattice.level_count,
                lattice.base_resolution,
                lattice.cell_size);

        const uint32_t coarsest_step = 1u << (params.level_count - 1u);
        const uint32_t half_extent =
            (params.base_resolution / 2u) * coarsest_step;
        return 2u * half_extent;
    }

    ClipmapViewTransform compute_clipmap_view(
        float camera_world_x,
        float camera_world_z,
        const assets::ClipmapLandscapeRenderSettings& settings,
        const assets::ClipmapLatticeParams& lattice,
        uint32_t heightmap_width,
        uint32_t heightmap_height) noexcept
    {
        ClipmapViewTransform out{};

        // World meters per finest lattice cell. Guard against a non-positive /
        // non-finite authored value so the snap step and scale stay sane.
        const float cell =
            (std::isfinite(settings.lattice_world_cell_size)
             && settings.lattice_world_cell_size > 0.0f)
                ? settings.lattice_world_cell_size
                : 1.0f;

        out.lattice_world_scale = cell;
        out.vertical_scale = settings.vertical_scale;
        out.base_height = settings.base_height;

        // ── View snapping ──────────────────────────────────────────────────
        // The lattice is authored centered at the origin; we translate it to
        // follow the camera. If the translation tracked the camera continuously
        // the lattice vertices (and therefore the heightmap texels they sample)
        // would slide under the camera every frame, making the fine center
        // shimmer ("swimming"). Snapping the translation to a fixed world grid
        // makes the lattice appear stationary while the camera moves within one
        // grid step, then jump by exactly one step when the camera crosses a
        // boundary — the texels stay put.
        //
        // Snap step = 2 * cell (two finest cells), NOT one. The lattice nests
        // power-of-two LOD rings whose cell sizes are cell, 2*cell, 4*cell, ...
        // A single-finest-cell shift would move the coarser rings by a fraction
        // of their own cell and misalign every ring above level 0, reintroducing
        // swimming at the LOD seams. 2*cell is the coarsest common divisor that
        // keeps level 0 (period cell) and level 1 (period 2*cell) — and hence
        // every coarser power-of-two level — aligned through one shift. The
        // finest grid also stays texel-aligned as long as the texel world size
        // is an integer multiple of cell (the renderer sizes the lattice to the
        // heightmap so this holds); the heightmap sample then never drifts
        // sub-texel as the camera pans.
        const float snap_step = 2.0f * cell;
        out.snap_step = snap_step;

        out.lattice_translation[0] =
            std::round(camera_world_x / snap_step) * snap_step;
        out.lattice_translation[1] = 0.0f;
        out.lattice_translation[2] =
            std::round(camera_world_z / snap_step) * snap_step;

        // ── World XZ -> heightmap UV ────────────────────────────────────────
        // The texture's [0,1] UV maps linearly onto the world footprint
        // [world_origin, world_origin + world_size]:
        //   uv = (world_xz - world_origin) / world_size
        //      = world_xz / world_size  -  world_origin / world_size
        // Guard a degenerate (<= 0 / non-finite) footprint so the mapping does
        // not divide by zero; fall back to a unit footprint.
        for (int axis = 0; axis < 2; ++axis) {
            const float size =
                (std::isfinite(settings.world_size[axis])
                 && settings.world_size[axis] > 0.0f)
                    ? settings.world_size[axis]
                    : 1.0f;
            const float inv_size = 1.0f / size;
            out.world_to_uv_scale[axis] = inv_size;
            out.world_to_uv_offset[axis] =
                -settings.world_origin[axis] * inv_size;
        }

        // ── Per-texel world size ────────────────────────────────────────────
        const uint32_t tex_w = heightmap_width == 0u ? 1u : heightmap_width;
        const uint32_t tex_h = heightmap_height == 0u ? 1u : heightmap_height;
        const float footprint_x =
            (std::isfinite(settings.world_size[0])
             && settings.world_size[0] > 0.0f)
                ? settings.world_size[0]
                : 1.0f;
        const float footprint_z =
            (std::isfinite(settings.world_size[1])
             && settings.world_size[1] > 0.0f)
                ? settings.world_size[1]
                : 1.0f;
        out.texel_world_size[0] =
            footprint_x / static_cast<float>(tex_w);
        out.texel_world_size[1] =
            footprint_z / static_cast<float>(tex_h);

        // Silence the unused-extent warning while keeping the lattice param in
        // the signature: the extent is part of the geometry contract callers
        // and tests reason about, and 3b uses it to size per-ring constants.
        (void)clipmap_lattice_grid_extent(lattice);

        return out;
    }
}
