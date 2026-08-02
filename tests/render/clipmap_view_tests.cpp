#include <gtest/gtest.h>

#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/rendering/clipmap_view.h>

#include <cmath>

using wz::engine::assets::ClipmapLandscapeRenderSettings;
using wz::engine::assets::ClipmapLatticeParams;
using wz::engine::assets::MeshData;
using wz::engine::assets::make_clipmap_lattice_mesh;
using wz::engine::rendering::ClipmapViewTransform;
using wz::engine::rendering::clipmap_lattice_grid_extent;
using wz::engine::rendering::clipmap_lattice_mesh_width_x;
using wz::engine::rendering::compute_clipmap_placement;
using wz::engine::rendering::compute_clipmap_view;

namespace
{
    ClipmapLandscapeRenderSettings default_settings()
    {
        ClipmapLandscapeRenderSettings settings{};
        settings.world_origin[0] = -100.0f;
        settings.world_origin[1] = -100.0f;
        settings.world_size[0] = 200.0f;
        settings.world_size[1] = 200.0f;
        settings.vertical_scale = 30.0f;
        settings.base_height = 2.0f;
        settings.lattice_world_cell_size = 2.0f;  // c0 = 2 -> finest period 4
        return settings;
    }

    ClipmapLatticeParams default_lattice()
    {
        return ClipmapLatticeParams{
            .level_count = 4u,
            .base_resolution = 8u,
            .cell_size = 1.0f,
        };
    }

    // The per-level snap the VS applies, recomputed on the CPU from the
    // transform's raw inputs: T_L = floor(camera/(2*c_L))*(2*c_L), c_L = 2^L*c0.
    float per_level_snap(float camera, float c0, uint32_t level)
    {
        const float cL = std::ldexp(c0, static_cast<int>(level));  // 2^L * c0
        const float two_cL = 2.0f * cL;
        return std::floor(camera / two_cL) * two_cL;
    }
}

// The lattice world scale (== c0) is the authored world cell size, vertical
// scale/base pass through, and snap_step is the finest level's period (2*c0).
TEST(ClipmapView, PassesThroughScaleAndVerticalSettings)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapViewTransform view =
        compute_clipmap_view(
            0.0f, 0.0f, settings, default_lattice(), 256u, 256u);

    EXPECT_FLOAT_EQ(view.lattice_world_scale, 2.0f);
    EXPECT_FLOAT_EQ(view.vertical_scale, 30.0f);
    EXPECT_FLOAT_EQ(view.base_height, 2.0f);
    EXPECT_FLOAT_EQ(view.snap_step, 4.0f);  // 2 * lattice_world_cell_size
    EXPECT_TRUE(view.view_snapped);         // procedural lattice by default
}

// The transform carries the camera world XZ unchanged (the VS does the snap),
// plus the sanitized base resolution the VS needs to size the morph band.
TEST(ClipmapView, CarriesCameraAndBaseResolution)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapViewTransform view =
        compute_clipmap_view(
            13.5f, -7.25f, settings, default_lattice(), 256u, 256u);

    EXPECT_FLOAT_EQ(view.camera_world_xz[0], 13.5f);
    EXPECT_FLOAT_EQ(view.camera_world_xz[1], -7.25f);
    // base_resolution passes through sanitization unchanged (8 stays 8). The
    // generator tiles gap-free for any resolution, so there is no longer a
    // round-to-a-multiple-of-4 step.
    EXPECT_FLOAT_EQ(view.base_resolution, 8.0f);

    // A non-multiple-of-4 base resolution (6) is carried verbatim, not bumped.
    const ClipmapViewTransform view6 =
        compute_clipmap_view(
            0.0f, 0.0f, settings,
            ClipmapLatticeParams{
                .level_count = 3u, .base_resolution = 6u, .cell_size = 1.0f },
            256u, 256u);
    EXPECT_FLOAT_EQ(view6.base_resolution, 6.0f);
}

// view_snapped passes through (false = arbitrary supplied mesh, #205).
TEST(ClipmapView, ViewSnappedFlagPassesThrough)
{
    ClipmapLandscapeRenderSettings settings = default_settings();
    settings.view_snapped = false;
    const ClipmapViewTransform view =
        compute_clipmap_view(
            0.0f, 0.0f, settings, default_lattice(), 256u, 256u);
    EXPECT_FALSE(view.view_snapped);
}

// Per-level snap math (the #207 core property): for the inputs the transform
// carries, each level L's snap T_L is a multiple of 2*c_L, and the levels are
// NESTED — T_{L+1} is itself a multiple of 2*c_L, so adjacent-level boundaries
// stay spatially coincident even though each level snaps on its own interval.
TEST(ClipmapView, PerLevelSnapIsAMultipleOfTwiceItsCellAndNested)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const float c0 = settings.lattice_world_cell_size;  // 2.0
    const ClipmapViewTransform view =
        compute_clipmap_view(
            37.3f, -53.1f, settings, default_lattice(), 256u, 256u);

    // The transform must hand the VS exactly c0 (and 2*c0 as snap_step).
    ASSERT_FLOAT_EQ(view.lattice_world_scale, c0);
    ASSERT_FLOAT_EQ(view.snap_step, 2.0f * c0);

    const float cam_x = view.camera_world_xz[0];
    const float cam_z = view.camera_world_xz[1];

    for (uint32_t level = 0; level < 6u; ++level) {
        const float cL = std::ldexp(c0, static_cast<int>(level));
        const float two_cL = 2.0f * cL;

        const float tx = per_level_snap(cam_x, c0, level);
        const float tz = per_level_snap(cam_z, c0, level);

        // T_L is an integer multiple of 2*c_L (no remainder => no sub-cell shift
        // => the level does not lurch as the camera pans within its own cell).
        EXPECT_NEAR(std::remainder(tx, two_cL), 0.0f, 1e-4f) << "level " << level;
        EXPECT_NEAR(std::remainder(tz, two_cL), 0.0f, 1e-4f) << "level " << level;

        // Nesting: T_{L+1} is a multiple of 2*c_L (its own period is 4*c_L).
        const float tx_next = per_level_snap(cam_x, c0, level + 1u);
        const float tz_next = per_level_snap(cam_z, c0, level + 1u);
        EXPECT_NEAR(std::remainder(tx_next, two_cL), 0.0f, 1e-4f)
            << "nest x at level " << level;
        EXPECT_NEAR(std::remainder(tz_next, two_cL), 0.0f, 1e-4f)
            << "nest z at level " << level;
    }
}

// Per-level snap stability: a camera move smaller than a level's own snap period
// (2*c_L) leaves THAT level's snap unchanged — the coarse rings stay put within
// their own cell instead of lurching every finest step.
TEST(ClipmapView, SubLevelCellCameraMoveLeavesThatLevelSnapUnchanged)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const float c0 = settings.lattice_world_cell_size;  // 2.0

    // Level 2: c_2 = 4*c0 = 8, period 2*c_2 = 16. A 3-unit nudge stays inside
    // one of its cells, so its snap must not move (the old single-snap path
    // shifted it by the finest period and lurched).
    const float base_cam = 40.0f;
    const float snap_base = per_level_snap(base_cam, c0, 2u);
    for (const float dx : { 0.0f, 1.0f, 3.0f, 7.9f }) {
        EXPECT_FLOAT_EQ(per_level_snap(base_cam + dx, c0, 2u), snap_base)
            << "dx=" << dx;
    }
}

// World->UV maps the heightmap world footprint corners onto [0,1]^2.
TEST(ClipmapView, WorldFootprintCornersMapToUnitUV)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapViewTransform view =
        compute_clipmap_view(
            0.0f, 0.0f, settings, default_lattice(), 256u, 256u);

    auto map_uv = [&](float wx, float wz, float& u, float& v) {
        u = view.world_to_uv_scale[0] * wx + view.world_to_uv_offset[0];
        v = view.world_to_uv_scale[1] * wz + view.world_to_uv_offset[1];
    };

    const float min_x = settings.world_origin[0];
    const float min_z = settings.world_origin[1];
    const float max_x = settings.world_origin[0] + settings.world_size[0];
    const float max_z = settings.world_origin[1] + settings.world_size[1];

    float u = 0.0f;
    float v = 0.0f;

    map_uv(min_x, min_z, u, v);
    EXPECT_FLOAT_EQ(u, 0.0f);
    EXPECT_FLOAT_EQ(v, 0.0f);

    map_uv(max_x, max_z, u, v);
    EXPECT_FLOAT_EQ(u, 1.0f);
    EXPECT_FLOAT_EQ(v, 1.0f);

    // Footprint center maps to (0.5, 0.5).
    map_uv((min_x + max_x) * 0.5f, (min_z + max_z) * 0.5f, u, v);
    EXPECT_FLOAT_EQ(u, 0.5f);
    EXPECT_FLOAT_EQ(v, 0.5f);
}

// Per-texel world size is the footprint divided by the texel count.
TEST(ClipmapView, TexelWorldSizeIsFootprintOverTexelCount)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapViewTransform view =
        compute_clipmap_view(
            0.0f, 0.0f, settings, default_lattice(), 200u, 400u);

    EXPECT_FLOAT_EQ(view.texel_world_size[0], 200.0f / 200.0f);  // 1.0
    EXPECT_FLOAT_EQ(view.texel_world_size[1], 200.0f / 400.0f);  // 0.5
}

// The computation is deterministic: identical inputs give identical outputs.
TEST(ClipmapView, IsDeterministic)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapLatticeParams lattice = default_lattice();

    const ClipmapViewTransform a =
        compute_clipmap_view(13.37f, -42.0f, settings, lattice, 256u, 256u);
    const ClipmapViewTransform b =
        compute_clipmap_view(13.37f, -42.0f, settings, lattice, 256u, 256u);

    EXPECT_FLOAT_EQ(a.camera_world_xz[0], b.camera_world_xz[0]);
    EXPECT_FLOAT_EQ(a.camera_world_xz[1], b.camera_world_xz[1]);
    EXPECT_FLOAT_EQ(a.lattice_world_scale, b.lattice_world_scale);
    EXPECT_FLOAT_EQ(a.base_resolution, b.base_resolution);
    EXPECT_FLOAT_EQ(a.world_to_uv_scale[0], b.world_to_uv_scale[0]);
    EXPECT_FLOAT_EQ(a.world_to_uv_offset[0], b.world_to_uv_offset[0]);
    EXPECT_FLOAT_EQ(a.texel_world_size[0], b.texel_world_size[0]);
    EXPECT_FLOAT_EQ(a.snap_step, b.snap_step);
}

// The lattice grid extent mirrors the generator's geometry: for L levels and
// base resolution m, the lattice spans 2 * (m/2) * 2^(L-1) finest cells.
TEST(ClipmapView, GridExtentMatchesLatticeGeometry)
{
    // L=4, m=8 -> coarsest_step = 2^3 = 8, half_extent = 4 * 8 = 32,
    // grid_extent = 64.
    EXPECT_EQ(clipmap_lattice_grid_extent(default_lattice()), 64u);

    // L=1, m=8 -> coarsest_step = 1, half_extent = 4, grid_extent = 8.
    EXPECT_EQ(
        clipmap_lattice_grid_extent(ClipmapLatticeParams{
            .level_count = 1u,
            .base_resolution = 8u,
            .cell_size = 1.0f,
        }),
        8u);

    // base_resolution is carried verbatim now (no round-to-4): m=6, L=2 ->
    // half_extent = (6/2)*2^1 = 6, grid_extent = 12 (distinct from m=8's 16).
    EXPECT_EQ(
        clipmap_lattice_grid_extent(ClipmapLatticeParams{
            .level_count = 2u,
            .base_resolution = 6u,
            .cell_size = 1.0f,
        }),
        12u);

    // An odd resolution is also exact: m=5, L=3 -> half_extent = (5/2)*2^2 =
    // 2*4 = 8, grid_extent = 16. (floor(m/2) for odd m, matching the generator.)
    EXPECT_EQ(
        clipmap_lattice_grid_extent(ClipmapLatticeParams{
            .level_count = 3u,
            .base_resolution = 5u,
            .cell_size = 1.0f,
        }),
        16u);
}

// ── compute_clipmap_placement: mesh (world-sized) + node transform ───────────
//
// The lattice mesh is now WORLD-SIZED (make_clipmap_lattice_mesh bakes cell_size
// into the positions). compute_clipmap_placement recovers the finest cell world
// size c0 from the mesh width (c0 = width_x / grid_extent), sizes the terrain
// footprint as c0 * heightmap_dims, and PLACES it from the node translation —
// node scale X/Z no longer sizes it. These tests lock that contract down.
namespace
{
    // The width helper run on a freshly generated lattice equals
    // grid_extent * cell_size, so c0 round-trips back to cell_size exactly.
    float mesh_width(const ClipmapLatticeParams& lattice)
    {
        return clipmap_lattice_mesh_width_x(make_clipmap_lattice_mesh(lattice));
    }
}

// A world-sized lattice built with cell_size s spans grid_extent finest cells,
// so its X width is exactly grid_extent * s — the inverse of the c0 inference.
TEST(ClipmapPlacement, LatticeMeshWidthIsGridExtentTimesCellSize)
{
    for (const float s : { 0.5f, 1.0f, 2.0f, 7.25f }) {
        const ClipmapLatticeParams lattice{
            .level_count = 4u, .base_resolution = 8u, .cell_size = s };
        const float expected =
            static_cast<float>(clipmap_lattice_grid_extent(lattice)) * s;
        EXPECT_FLOAT_EQ(mesh_width(lattice), expected) << "s=" << s;
    }
}

// c0 inferred from the mesh equals the cell_size the lattice was built with,
// across cell sizes and lattice shapes (level_count / base_resolution).
TEST(ClipmapPlacement, InfersFinestCellSizeFromMesh)
{
    const float translation[3] = { 0.0f, 0.0f, 0.0f };
    const float scale[3] = { 1.0f, 1.0f, 1.0f };

    struct Case { uint32_t levels; uint32_t base; float s; };
    for (const Case c : { Case{ 4u, 8u, 2.0f },
                          Case{ 1u, 8u, 1.0f },
                          Case{ 3u, 6u, 0.5f },
                          Case{ 5u, 5u, 3.5f } }) {
        const ClipmapLatticeParams lattice{
            .level_count = c.levels, .base_resolution = c.base, .cell_size = c.s };
        const ClipmapLandscapeRenderSettings placement =
            compute_clipmap_placement(
                mesh_width(lattice),
                // The renderer passes cell_size = 1 (grid extent ignores it).
                ClipmapLatticeParams{
                    .level_count = c.levels,
                    .base_resolution = c.base,
                    .cell_size = 1.0f },
                64u, 64u, translation, scale, /*view_snapped*/ true);

        EXPECT_NEAR(placement.lattice_world_cell_size, c.s, 1e-4f * c.s)
            << "levels=" << c.levels << " base=" << c.base << " s=" << c.s;
    }
}

// world_size == c0 * heightmap_dims (the terrain world footprint), world_origin
// == node translation XZ, base_height == translation.y, vertical_scale ==
// node.scale.y. Node scale X/Z is deliberately IGNORED for horizontal sizing.
TEST(ClipmapPlacement, FootprintFromMeshOriginAndVerticalFromNode)
{
    const float s = 2.0f;
    const ClipmapLatticeParams lattice{
        .level_count = 4u, .base_resolution = 8u, .cell_size = s };

    const uint32_t tex_w = 256u;
    const uint32_t tex_h = 128u;
    const float translation[3] = { 10.0f, -3.0f, -20.0f };
    // Non-unit scale X/Z to prove they do NOT size the terrain; scale.y drives
    // vertical.
    const float scale[3] = { 5.0f, 4.0f, 9.0f };

    const ClipmapLandscapeRenderSettings placement =
        compute_clipmap_placement(
            mesh_width(lattice),
            ClipmapLatticeParams{
                .level_count = 4u, .base_resolution = 8u, .cell_size = 1.0f },
            tex_w, tex_h, translation, scale, /*view_snapped*/ true);

    // c0 == s, so footprint = s * dims, independent of node scale X/Z.
    EXPECT_NEAR(placement.lattice_world_cell_size, s, 1e-4f);
    EXPECT_NEAR(placement.world_size[0], s * static_cast<float>(tex_w), 1e-3f);
    EXPECT_NEAR(placement.world_size[1], s * static_cast<float>(tex_h), 1e-3f);

    // Placement from translation; vertical from translation.y / scale.y.
    EXPECT_FLOAT_EQ(placement.world_origin[0], 10.0f);
    EXPECT_FLOAT_EQ(placement.world_origin[1], -20.0f);   // translation.z
    EXPECT_FLOAT_EQ(placement.base_height, -3.0f);        // translation.y
    EXPECT_FLOAT_EQ(placement.vertical_scale, 4.0f);      // scale.y
}

// Horizontal size is mesh-driven: changing node scale X/Z leaves c0 and
// world_size unchanged (the regression the double-scaling fix targets).
TEST(ClipmapPlacement, NodeScaleXZDoesNotSizeTheTerrain)
{
    const ClipmapLatticeParams lattice{
        .level_count = 4u, .base_resolution = 8u, .cell_size = 1.5f };
    const float width = mesh_width(lattice);
    const ClipmapLatticeParams grid{
        .level_count = 4u, .base_resolution = 8u, .cell_size = 1.0f };
    const float translation[3] = { 0.0f, 0.0f, 0.0f };

    const float scale_a[3] = { 1.0f, 1.0f, 1.0f };
    const float scale_b[3] = { 100.0f, 1.0f, 0.01f };

    const ClipmapLandscapeRenderSettings a = compute_clipmap_placement(
        width, grid, 64u, 64u, translation, scale_a, true);
    const ClipmapLandscapeRenderSettings b = compute_clipmap_placement(
        width, grid, 64u, 64u, translation, scale_b, true);

    EXPECT_FLOAT_EQ(a.lattice_world_cell_size, b.lattice_world_cell_size);
    EXPECT_FLOAT_EQ(a.world_size[0], b.world_size[0]);
    EXPECT_FLOAT_EQ(a.world_size[1], b.world_size[1]);
}

// view_snapped passes through both ways.
TEST(ClipmapPlacement, ViewSnappedPassesThrough)
{
    const ClipmapLatticeParams grid{
        .level_count = 4u, .base_resolution = 8u, .cell_size = 1.0f };
    const float translation[3] = { 0.0f, 0.0f, 0.0f };
    const float scale[3] = { 1.0f, 1.0f, 1.0f };
    EXPECT_TRUE(compute_clipmap_placement(
        64.0f, grid, 64u, 64u, translation, scale, true).view_snapped);
    EXPECT_FALSE(compute_clipmap_placement(
        64.0f, grid, 64u, 64u, translation, scale, false).view_snapped);
}

// A degenerate mesh width (empty mesh -> 0, or non-finite) falls back to c0 = 1
// so the snap step / footprint stay sane instead of collapsing to zero.
TEST(ClipmapPlacement, DegenerateMeshWidthFallsBackToUnitCell)
{
    EXPECT_FLOAT_EQ(clipmap_lattice_mesh_width_x(MeshData{}), 0.0f);

    const ClipmapLatticeParams grid{
        .level_count = 4u, .base_resolution = 8u, .cell_size = 1.0f };
    const float translation[3] = { 0.0f, 0.0f, 0.0f };
    const float scale[3] = { 1.0f, 1.0f, 1.0f };
    const ClipmapLandscapeRenderSettings placement =
        compute_clipmap_placement(
            0.0f, grid, 32u, 32u, translation, scale, true);
    EXPECT_FLOAT_EQ(placement.lattice_world_cell_size, 1.0f);
    EXPECT_FLOAT_EQ(placement.world_size[0], 32.0f);  // 1.0 * 32
    EXPECT_FLOAT_EQ(placement.world_size[1], 32.0f);
}

// ── level_count must be clamped at BOTH ends (issue #314, C1-H1) ────────────
//
// clipmap_lattice_grid_extent computes `1u << (level_count - 1u)`, which is
// UNDEFINED for level_count >= 33 and overflows uint32 well before that, while
// sanitize_clipmap_lattice_params clamped only the low end. Measured with
// base_resolution 64: level_count 31 and 32 gave a grid extent of 0, and 33
// wrapped to 64 -- indistinguishable from level_count 1, which is the failure
// mode worth naming, because a silently-tiny lattice looks like an authoring
// mistake rather than a bug.
//
// The clamp lives in the sanitizer because BOTH authoring paths pass through
// it: the physical-parameter resolver (which already searched only to 24) and
// the explicit typed-desc path, which is the one that could reach 33.
TEST(ClipmapView, GridExtentIsBoundedForAnyAuthoredLevelCount)
{
    uint32_t previous = 0u;
    for (uint32_t level_count = 1u; level_count <= 64u; ++level_count) {
        ClipmapLatticeParams params{};
        params.level_count = level_count;
        params.base_resolution = 64u;
        params.cell_size = 1.0f;

        const uint32_t extent = clipmap_lattice_grid_extent(params);

        EXPECT_GT(extent, 0u)
            << "level_count " << level_count << " produced an EMPTY lattice";
        EXPECT_GE(extent, previous)
            << "level_count " << level_count << " went BACKWARDS -- the shift "
            << "wrapped";
        previous = extent;
    }
}
