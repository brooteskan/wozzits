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
        // params (base_resolution passes through >= 1; no round-to-4), then the
        // lattice spans fine_extent = 2 * half_extent finest cells, where
        // half_extent = floor(m/2) * 2^(L-1).
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

        // Sanitize the lattice params once (level_count >= 1, base_resolution
        // >= 1 and carried verbatim — the generator tiles gap-free for any
        // resolution) so the emitted base_resolution matches the geometry the
        // generator actually produced.
        const assets::ClipmapLatticeParams lattice_params =
            assets::sanitize_clipmap_lattice_params(
                lattice.level_count,
                lattice.base_resolution,
                lattice.cell_size);

        // World meters per finest lattice cell (c0). Guard against a
        // non-positive / non-finite value so the snap step stays sane. The
        // renderer derives this from the WORLD-SIZED lattice mesh itself
        // (mesh_width_x / grid_extent — see compute_clipmap_placement) and writes
        // it into lattice_world_cell_size; it is the world step between adjacent
        // finest-grid vertices, i.e. c0. The VS uses it only as the per-level snap
        // quantum (the world-sized g.xz are placed directly, not scaled by it).
        const float cell =
            (std::isfinite(settings.lattice_world_cell_size)
             && settings.lattice_world_cell_size > 0.0f)
                ? settings.lattice_world_cell_size
                : 1.0f;

        out.lattice_world_scale = cell;
        out.vertical_scale = settings.vertical_scale;
        out.base_height = settings.base_height;
        out.view_snapped = settings.view_snapped;

        // ── Per-level view snapping (issue #207) ───────────────────────────
        // The lattice is authored centered at the origin with each vertex's LOD
        // level in position.y. The VS places + snaps each level INDEPENDENTLY:
        // level L (world cell c_L = 2^L * c0) snaps its grid to a multiple of
        // 2*c_L, i.e. T_L = floor(camera_xz/(2*c_L))*(2*c_L). The 2x makes the
        // levels nest (T_{L+1} is always a multiple of 2*c_L), so adjacent
        // boundaries stay spatially coincident and only need a vertical morph.
        //
        // This struct carries the raw inputs the VS needs to compute every T_L
        // itself (one set of constants for all levels): the camera world XZ and
        // c0. snap_step here is the FINEST level's period (2*c0); a test can
        // assert each level L snaps to a multiple of 2^L * snap_step. The single
        // pre-snapped translation the old uniform-snap path emitted is gone — it
        // could not keep both the fine center and the coarse rings stable.
        out.camera_world_xz[0] = camera_world_x;
        out.camera_world_xz[1] = camera_world_z;
        out.base_resolution =
            static_cast<float>(lattice_params.base_resolution);
        const float snap_step = 2.0f * cell;
        out.snap_step = snap_step;

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

        return out;
    }

    float clipmap_lattice_mesh_width_x(const assets::MeshData& mesh) noexcept
    {
        if (mesh.vertices.empty()) {
            return 0.0f;
        }
        float min_x = mesh.vertices.front().position[0];
        float max_x = min_x;
        for (const assets::MeshVertex& vertex : mesh.vertices) {
            const float x = vertex.position[0];
            min_x = x < min_x ? x : min_x;
            max_x = x > max_x ? x : max_x;
        }
        return max_x - min_x;
    }

    assets::ClipmapLandscapeRenderSettings compute_clipmap_placement(
        float mesh_width_x,
        const assets::ClipmapLatticeParams& lattice,
        uint32_t heightmap_width,
        uint32_t heightmap_height,
        const float node_translation[3],
        const float node_scale[3],
        bool view_snapped,
        const assets::ClipmapLandscapeRenderSettings* footprint_override) noexcept
    {
        assets::ClipmapLandscapeRenderSettings out{};

        // Finest cell world size c0, inferred FROM THE MESH: the world-sized
        // lattice spans grid_extent finest cells across mesh_width_x world
        // metres, so c0 = mesh_width_x / grid_extent. The grid extent mirrors the
        // generator's fine_extent and is independent of cell_size (passed in as 1
        // here — only level_count/base_resolution matter). Guard a non-finite /
        // non-positive width or a zero extent so the snap step stays sane.
        //
        // This is computed UNCONDITIONALLY — even with a footprint override
        // (issue #218 Phase 2), c0 stays mesh-derived. The override governs only
        // the texture->world footprint, never the lattice geometry snap, so this
        // c0 math lives in exactly one place.
        const uint32_t grid_extent = clipmap_lattice_grid_extent(lattice);
        const float c0 =
            (std::isfinite(mesh_width_x) && mesh_width_x > 0.0f
             && grid_extent > 0u)
                ? mesh_width_x / static_cast<float>(grid_extent)
                : 1.0f;
        out.lattice_world_cell_size = c0;
        out.view_snapped = view_snapped;

        if (footprint_override != nullptr) {
            // Authoritative footprint baked from a connected Placement: use it
            // verbatim and do NOT consult the node transform or heightmap dims
            // for the footprint. c0 (above) and view_snapped still hold.
            out.world_origin[0] = footprint_override->world_origin[0];
            out.world_origin[1] = footprint_override->world_origin[1];
            out.world_size[0] = footprint_override->world_size[0];
            out.world_size[1] = footprint_override->world_size[1];
            out.vertical_scale = footprint_override->vertical_scale;
            out.base_height = footprint_override->base_height;
            out.placement_authoritative = true;
            return out;
        }

        // Terrain world footprint = c0 * heightmap_dims (world_extent). This sets
        // world_to_uv = 1/world_extent in compute_clipmap_view. Treat 0 dims as 1.
        const uint32_t tex_w = heightmap_width == 0u ? 1u : heightmap_width;
        const uint32_t tex_h = heightmap_height == 0u ? 1u : heightmap_height;
        out.world_size[0] = c0 * static_cast<float>(tex_w);
        out.world_size[1] = c0 * static_cast<float>(tex_h);

        // Node TRANSLATION places the terrain (XZ -> world origin, Y -> base
        // height). Node SCALE X/Z no longer sizes it (deliberate: the clipmap is
        // world-absolute, extent comes from world_extent above). Vertical sizing
        // is intentionally still node.scale.y for now (a follow-up turns it into
        // a param); see the renderer's clipmap branch.
        out.world_origin[0] = node_translation[0];
        out.world_origin[1] = node_translation[2];
        out.vertical_scale = node_scale[1];
        out.base_height = node_translation[1];

        return out;
    }
}
