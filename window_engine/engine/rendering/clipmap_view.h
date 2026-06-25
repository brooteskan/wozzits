#pragma once

// engine/rendering/clipmap_view.h
//
// Pure, renderer-agnostic view-transform math for a geometry-clipmap
// landscape (issue #198 step 3). No GPU, no rhi, no DX12 — this is the
// render-time CPU math that slice 3b packs into shader constants.
//
// The clipmap lattice mesh (kProceduralClipmapLatticeMeshSchema) is authored
// in unitless grid space (finest cell == 1 grid unit), centered at the origin,
// lying flat in XZ, with each vertex's LOD level stored in position.y. Each
// frame the renderer must:
//   * give the vertex shader what it needs to place each LOD level in world
//     space and snap it INDEPENDENTLY to a multiple of that level's own world
//     cell (issue #207). Level L's world cell is c_L = 2^L * c0, where c0 is
//     the world size of the finest cell, and the per-level snap is
//       T_L = floor(camera_xz / (2*c_L)) * (2*c_L).
//     The 2x makes the levels' grids nest (T_{L+1} is a multiple of 2*c_L), so
//     adjacent-level boundary vertices stay spatially coincident even though
//     each level snaps on its own interval — no horizontal crack, and the seam
//     is closed by a purely VERTICAL geomorph in the VS. This replaces the old
//     single whole-lattice snap, which only kept levels 0-1 aligned and made
//     every coarser ring lurch.
//   * map every world XZ position the displaced lattice reaches onto the
//     height texture's [0,1] UV space.
//
// c0 reconciliation: the lattice mesh bakes cell_size into its positions as
// (ix-center)*cell_size, so it is WORLD-SIZED and the VS places g.xz directly
// (it does NOT scale them by lattice_world_scale — that double-scaled the mesh).
// c0 = the world size of the finest cell is still carried in lattice_world_scale
// (compute_clipmap_view passes settings.lattice_world_cell_size through), but the
// VS uses it ONLY as the per-level snap quantum (c_L = 2^L * c0). The renderer
// recovers that c0 from the mesh itself (mesh_width_x / grid_extent — see
// compute_clipmap_placement) and writes it into settings.lattice_world_cell_size.
//
// compute_clipmap_view() takes the camera XZ, the authored render settings, the
// lattice params (for the grid extent / base resolution) and the heightmap
// texel dimensions, and returns a plain ClipmapViewTransform carrying the
// per-level snap inputs (camera XZ + c0 + base resolution), the world->UV
// mapping, and the vertical scale/base passthrough + per-texel world size.

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
        // Per-level snap inputs (issue #207). The VS reconstructs each level's
        // world placement itself: for a vertex at WORLD-offset xz with level L,
        //   c_L      = 2^L * c0
        //   T_L      = floor(camera_world_xz / (2*c_L)) * (2*c_L)
        //   world_xz = T_L + local_xz
        // The lattice mesh is world-sized, so local_xz is already in metres and is
        // NOT scaled by lattice_world_scale; c0 (== lattice_world_scale) feeds the
        // snap quantum 2*c_L only.
        float camera_world_xz[2]{ 0.0f, 0.0f };
        float lattice_world_scale = 1.0f;  // == c0 (finest cell world size)

        // base_resolution (m) of the lattice — the VS sizes each level's morph
        // band from its world half-extent (m/2)*c_L.
        float base_resolution = 8.0f;

        // World XZ -> heightmap UV transform:
        //   uv = world_to_uv_scale * world_xz + world_to_uv_offset
        // Maps the heightmap's world footprint onto [0,1]^2.
        float world_to_uv_scale[2]{ 1.0f, 1.0f };
        float world_to_uv_offset[2]{ 0.0f, 0.0f };

        // World size of a single heightmap texel (footprint / texel count).
        float texel_world_size[2]{ 1.0f, 1.0f };

        // Passthrough from the render settings so the VS has everything it needs
        // to displace Y in one constant block.
        float vertical_scale = 1.0f;
        float base_height = 0.0f;

        // The finest level's snap period (2*c0). Exposed so callers/tests can
        // reason about (and assert on) the per-level snap. Level L snaps to a
        // multiple of 2^L * snap_step.
        float snap_step = 1.0f;

        // True when the geometry is the procedural, view-snapped lattice
        // (position.y is a LOD level). False for an arbitrary supplied static
        // mesh, where the VS must NOT do per-level snapping and treats Y as real
        // geometry (#205).
        bool view_snapped = true;
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
    // Snap rule (per-level, applied in the VS): level L's grid snaps to a
    // multiple of `2^L * snap_step`, where `snap_step = 2 * c0` and
    // `c0 = settings.lattice_world_cell_size`. The transform carries the raw
    // inputs (camera XZ + c0); the VS computes each T_L. settings.view_snapped
    // is passed through; when false the VS skips per-level snapping entirely.
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

    // World-space width of a clipmap lattice mesh along X: max(position.x) -
    // min(position.x) over its vertices. The lattice generator bakes the finest
    // cell size into the positions ((ix-center)*cell_size), so this width is the
    // mesh's real world footprint in X (== grid_extent * cell_size); dividing it
    // by the unitless grid extent recovers the finest cell world size c0 even
    // though the mesh carries no cell_size of its own. position.y holds the LOD
    // level tag and position.z the Z coordinate, so only X is inspected. Returns
    // 0 for an empty mesh (the caller guards a zero/degenerate width).
    [[nodiscard]] float clipmap_lattice_mesh_width_x(
        const assets::MeshData& mesh) noexcept;

    // Derive a clipmap landscape's render-time placement from the WORLD-SIZED
    // lattice mesh and the owning scene node's local transform — the renderable
    // no longer carries a horizontal world size of its own (it is world-absolute;
    // node SCALE X/Z does NOT size it). Pure so it is unit-testable.
    //
    //   c0 (lattice_world_cell_size) = mesh_width_x / clipmap_lattice_grid_extent
    //       — the finest cell world size, inferred from the mesh. The mesh spans
    //       grid_extent finest cells across mesh_width_x world metres, so this is
    //       the metres-per-finest-cell the generator baked in. A zero/degenerate
    //       extent or width falls back to 1.0 so the snap step stays sane.
    //   world_size = c0 * heightmap_dims  — the terrain's world footprint
    //       (world_extent), so world_to_uv = 1/world_extent maps the lattice's
    //       world XZ onto the height texture's [0,1] UV.
    //   world_origin = node_translation.xz  — node translation PLACES the terrain.
    //   vertical_scale = node_scale[1], base_height = node_translation[1]
    //       — carried through unchanged (vertical sizing is a SEPARATE concern;
    //       see the renderer note — node.scale.y still drives vertical for now).
    //   view_snapped passes through from the recipe (the helper does not infer it).
    //
    // node_translation / node_scale are 3-float xyz arrays (the node's
    // AuthoredTransform fields). heightmap dims of 0 are treated as 1.
    [[nodiscard]] assets::ClipmapLandscapeRenderSettings compute_clipmap_placement(
        float mesh_width_x,
        const assets::ClipmapLatticeParams& lattice,
        uint32_t heightmap_width,
        uint32_t heightmap_height,
        const float node_translation[3],
        const float node_scale[3],
        bool view_snapped) noexcept;
}
