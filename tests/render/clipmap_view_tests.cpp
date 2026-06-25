#include <gtest/gtest.h>

#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/rendering/clipmap_view.h>

#include <cmath>

using wz::engine::assets::ClipmapLandscapeRenderSettings;
using wz::engine::assets::ClipmapLatticeParams;
using wz::engine::rendering::ClipmapViewTransform;
using wz::engine::rendering::clipmap_lattice_grid_extent;
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
    // base_resolution is sanitized to a multiple of 4 (8 stays 8).
    EXPECT_FLOAT_EQ(view.base_resolution, 8.0f);

    // A base resolution of 6 sanitizes up to 8.
    const ClipmapViewTransform view6 =
        compute_clipmap_view(
            0.0f, 0.0f, settings,
            ClipmapLatticeParams{
                .level_count = 3u, .base_resolution = 6u, .cell_size = 1.0f },
            256u, 256u);
    EXPECT_FLOAT_EQ(view6.base_resolution, 8.0f);
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

    // base_resolution is sanitized up to a multiple of 4: m=6 -> 8.
    EXPECT_EQ(
        clipmap_lattice_grid_extent(ClipmapLatticeParams{
            .level_count = 2u,
            .base_resolution = 6u,
            .cell_size = 1.0f,
        }),
        clipmap_lattice_grid_extent(ClipmapLatticeParams{
            .level_count = 2u,
            .base_resolution = 8u,
            .cell_size = 1.0f,
        }));
}
