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
        settings.lattice_world_cell_size = 2.0f;  // snap step = 4.0
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
}

// The lattice world scale is the authored world cell size, and vertical
// scale/base pass through untouched.
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
}

// Snap rule: a camera move smaller than one snap step leaves the snapped
// lattice translation unchanged (the lattice appears stationary / no swimming).
TEST(ClipmapView, SubStepCameraMoveLeavesTranslationUnchanged)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapLatticeParams lattice = default_lattice();
    const float step = 2.0f * settings.lattice_world_cell_size;  // 4.0

    const ClipmapViewTransform base =
        compute_clipmap_view(
            20.0f, -8.0f, settings, lattice, 256u, 256u);

    // Several sub-step nudges in both axes — all must snap to the same cell.
    for (const float dx : { 0.0f, 0.1f, 0.9f, step * 0.49f, -step * 0.49f }) {
        for (const float dz : { 0.0f, -0.3f, step * 0.25f, step * 0.49f }) {
            const ClipmapViewTransform moved =
                compute_clipmap_view(
                    20.0f + dx, -8.0f + dz, settings, lattice, 256u, 256u);
            EXPECT_FLOAT_EQ(
                moved.lattice_translation[0], base.lattice_translation[0])
                << "dx=" << dx << " dz=" << dz;
            EXPECT_FLOAT_EQ(
                moved.lattice_translation[2], base.lattice_translation[2])
                << "dx=" << dx << " dz=" << dz;
        }
    }
}

// Snap rule: a one-step camera move shifts the translation by exactly one step.
TEST(ClipmapView, OneStepCameraMoveShiftsByExactlyOneStep)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapLatticeParams lattice = default_lattice();
    const float step = 2.0f * settings.lattice_world_cell_size;  // 4.0

    // Start on a snap boundary so the move lands cleanly on the next cell.
    const ClipmapViewTransform base =
        compute_clipmap_view(
            0.0f, 0.0f, settings, lattice, 256u, 256u);
    EXPECT_FLOAT_EQ(base.lattice_translation[0], 0.0f);
    EXPECT_FLOAT_EQ(base.lattice_translation[2], 0.0f);

    const ClipmapViewTransform moved_x =
        compute_clipmap_view(
            step, 0.0f, settings, lattice, 256u, 256u);
    EXPECT_FLOAT_EQ(
        moved_x.lattice_translation[0] - base.lattice_translation[0], step);
    EXPECT_FLOAT_EQ(
        moved_x.lattice_translation[2], base.lattice_translation[2]);

    const ClipmapViewTransform moved_z =
        compute_clipmap_view(
            0.0f, -step, settings, lattice, 256u, 256u);
    EXPECT_FLOAT_EQ(
        moved_z.lattice_translation[2] - base.lattice_translation[2], -step);
    EXPECT_FLOAT_EQ(
        moved_z.lattice_translation[0], base.lattice_translation[0]);

    // Three steps in X -> exactly three steps of translation.
    const ClipmapViewTransform moved_3x =
        compute_clipmap_view(
            3.0f * step, 0.0f, settings, lattice, 256u, 256u);
    EXPECT_FLOAT_EQ(
        moved_3x.lattice_translation[0] - base.lattice_translation[0],
        3.0f * step);
}

// Snap rule: the snapped lattice always stays within half a snap step of the
// camera (round-to-nearest), hence well within one step — the fine center
// never drifts away from the viewer.
TEST(ClipmapView, LatticeStaysWithinOneSnapStepOfCamera)
{
    const ClipmapLandscapeRenderSettings settings = default_settings();
    const ClipmapLatticeParams lattice = default_lattice();
    const float step = 2.0f * settings.lattice_world_cell_size;  // 4.0

    for (float cx = -53.3f; cx <= 53.3f; cx += 1.7f) {
        for (float cz = -47.9f; cz <= 47.9f; cz += 2.3f) {
            const ClipmapViewTransform view =
                compute_clipmap_view(
                    cx, cz, settings, lattice, 256u, 256u);
            EXPECT_LE(
                std::abs(view.lattice_translation[0] - cx), step * 0.5f + 1e-3f)
                << "cx=" << cx;
            EXPECT_LE(
                std::abs(view.lattice_translation[2] - cz), step * 0.5f + 1e-3f)
                << "cz=" << cz;
        }
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

    EXPECT_FLOAT_EQ(a.lattice_translation[0], b.lattice_translation[0]);
    EXPECT_FLOAT_EQ(a.lattice_translation[1], b.lattice_translation[1]);
    EXPECT_FLOAT_EQ(a.lattice_translation[2], b.lattice_translation[2]);
    EXPECT_FLOAT_EQ(a.lattice_world_scale, b.lattice_world_scale);
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
