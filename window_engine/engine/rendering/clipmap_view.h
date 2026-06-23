#pragma once

// engine/rendering/clipmap_view.h
//
// Pure, renderer-agnostic view-transform math for a geometry-clipmap
// landscape (issue #198 step 3). No GPU, no rhi, no DX12 — this is the
// render-time CPU math that slice 3b packs into shader constants.
//
// The clipmap lattice mesh (kProceduralClipmapLatticeMeshSchema) is authored
// in unitless grid space, centered at the origin, lying flat in XZ. Each frame
// the renderer must:
//   * scale the lattice to world units and translate it to follow the camera,
//     snapping the translation so the lattice appears stationary as the camera
//     moves within a cell and shifts by exactly one step when it crosses one
//     (no "swimming" of the fine center / heightmap texels), and
//   * map every world XZ position the displaced lattice reaches onto the
//     height texture's [0,1] UV space.
//
// compute_clipmap_view() takes the camera XZ, the authored render settings, the
// lattice params (for the grid extent) and the heightmap texel dimensions, and
// returns a plain ClipmapViewTransform struct carrying both transforms plus the
// vertical scale/base passthrough and per-texel world size.

#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/renderable/renderable.h>

#include <cstdint>

namespace wz::engine::rendering
{
    // Renderer-agnostic output of the clipmap view computation. All values are
    // in world units unless noted. 3b packs these into the clipmap shader's
    // per-draw constant block.
    struct ClipmapViewTransform
    {
        // Lattice world transform: world_pos = lattice_translation
        //                                    + lattice_world_scale * grid_pos
        // (grid_pos is the unitless lattice vertex position). Uniform scale,
        // because the lattice is authored isotropically in XZ.
        float lattice_translation[3]{ 0.0f, 0.0f, 0.0f };
        float lattice_world_scale = 1.0f;

        // World XZ -> heightmap UV transform:
        //   uv = world_to_uv_scale * world_xz + world_to_uv_offset
        // Maps the heightmap's world footprint onto [0,1]^2.
        float world_to_uv_scale[2]{ 1.0f, 1.0f };
        float world_to_uv_offset[2]{ 0.0f, 0.0f };

        // World size of a single heightmap texel (footprint / texel count).
        float texel_world_size[2]{ 1.0f, 1.0f };

        // Passthrough from the render settings so 3b has everything it needs
        // to displace Y in one constant block.
        float vertical_scale = 1.0f;
        float base_height = 0.0f;

        // The world distance the lattice translation is quantized to. Exposed
        // so callers/tests can reason about (and assert on) snapping.
        float snap_step = 1.0f;
    };

    // Compute the per-frame clipmap view transform.
    //
    //   camera_world_x / camera_world_z  camera position in world space (the Y
    //                                    is irrelevant to lattice placement).
    //   settings                         authored world placement / mapping.
    //   lattice                          the lattice description (its total
    //                                    grid extent sizes the world->UV and
    //                                    texel math is independent of it).
    //   heightmap_width / heightmap_height  height texture texel dimensions
    //                                    (>= 1; 0 is treated as 1).
    //
    // Snap rule: the lattice translation's XZ is snapped to a multiple of
    // `snap_step = 2 * settings.lattice_world_cell_size` (see .cpp for why 2x).
    [[nodiscard]] ClipmapViewTransform compute_clipmap_view(
        float camera_world_x,
        float camera_world_z,
        const assets::ClipmapLandscapeRenderSettings& settings,
        const assets::ClipmapLatticeParams& lattice,
        uint32_t heightmap_width,
        uint32_t heightmap_height) noexcept;

    // Total number of finest cells along one side of the lattice grid, i.e. the
    // lattice's full extent in grid units (the mesh spans
    // [-grid_extent/2, +grid_extent/2] * cell_size centered at the origin).
    // Shared by the view computation and its tests so the geometry contract
    // stays in one place.
    [[nodiscard]] uint32_t clipmap_lattice_grid_extent(
        const assets::ClipmapLatticeParams& lattice) noexcept;
}
