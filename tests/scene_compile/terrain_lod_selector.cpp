#include <gtest/gtest.h>

#include <math/projection.h>
#include <scene/compile/terrain_lod_selector.h>

using namespace wz::scene;
using namespace wz::math;

namespace
{
    constexpr float Pi = 3.14159265358979323846f;

    AABB box(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
    {
        return AABB{
            .min = { min_x, min_y, min_z },
            .max = { max_x, max_y, max_z },
        };
    }

    ViewData test_view()
    {
        ViewData view{};
        view.view = Mat4::identity();
        view.projection = projection_perspective_dx(
            Pi * 0.5f,
            1.0f,
            0.1f,
            100.0f);
        view.view_projection = view.projection;
        view.terrain_lod.viewport_width = 100.0f;
        view.terrain_lod.viewport_height = 100.0f;
        view.terrain_lod.fov_y_radians = Pi * 0.5f;
        view.terrain_lod.pixel_error_threshold = 2.0f;
        view.terrain_lod.hysteresis_fraction = 0.2f;
        return view;
    }

    TerrainLodRecord lod(
        uint32_t lod_id,
        uint32_t triangles,
        float error)
    {
        return TerrainLodRecord{
            .lod_id = TerrainLodId{ lod_id },
            .triangle_count = triangles,
            .conservative_geometric_error = error,
            .valid = true,
        };
    }

    TerrainChunkInfo chunk(
        uint32_t chunk_id,
        AABB bounds,
        std::span<const TerrainLodRecord> lods)
    {
        TerrainChunkInfo out{
            .terrain_instance_index = 0u,
            .chunk_id = TerrainChunkId{ chunk_id },
            .representation_kind =
                TerrainVisualRepresentationKind::MeshChunks,
            .world_bounds = bounds,
        };
        out.lods.assign(lods.begin(), lods.end());
        return out;
    }

    TerrainChunkInfo dense_chunk(
        uint32_t chunk_id,
        AABB bounds,
        float asset_triangle_density,
        std::span<const TerrainLodRecord> lods)
    {
        TerrainChunkInfo out = chunk(chunk_id, bounds, lods);
        out.asset_triangle_density = asset_triangle_density;
        return out;
    }

    TerrainSurfelDensityLevel surfel_level(
        uint32_t density_id,
        uint32_t count,
        float spacing,
        float radius)
    {
        return TerrainSurfelDensityLevel{
            .density_id = TerrainLodId{ density_id },
            .representative_radius = radius,
            .equivalent_triangle_cost = count * 2u,
            .valid = spacing > 0.0f && radius > 0.0f && count > 0u,
        };
    }
}

TEST(TerrainLodSelector, ZeroBudgetSelectsNoChunks)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 10u, 1.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 0u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    EXPECT_TRUE(choices.empty());
}

TEST(TerrainLodSelector, FlatPlaneChoosesCoarsestLod)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 25u, 0.0f),
        lod(2u, 4u, 0.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].lod_id.value, 2u);
}

TEST(TerrainLodSelector, UnlimitedBudgetRefinesToErrorThreshold)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 25u, 0.3f),
        lod(2u, 4u, 1.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].lod_id.value, 1u);
    EXPECT_LE(choices[0].projected_error_px, 2.0f);
}

TEST(TerrainLodSelector, BudgetConstrainsRefinementFromCoarseBase)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 20u, 0.2f),
        lod(2u, 5u, 1.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-3.0f, -1.0f, 10.0f, -1.0f, 1.0f, 11.0f), lods),
        chunk(1u, box(1.0f, -1.0f, 10.0f, 3.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 25u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 2u);
    uint32_t refined = 0u;
    for (const TerrainLodChoice& choice : choices) {
        if (choice.lod_id.value == 1u) {
            ++refined;
        }
    }
    EXPECT_EQ(refined, 1u);
}

TEST(TerrainLodSelector, CoarseBaseBudgetEvictsLowestPriorityChunk)
{
    const std::vector low_priority_large_lods{
        lod(0u, 20u, 0.0f),
    };
    const std::vector high_priority_small_lods{
        lod(0u, 6u, 4.0f),
    };
    const std::vector chunks{
        chunk(
            0u,
            box(-4.0f, -2.0f, 10.0f, -1.0f, 2.0f, 11.0f),
            low_priority_large_lods),
        chunk(
            1u,
            box(1.0f, -1.0f, 10.0f, 2.0f, 1.0f, 11.0f),
            high_priority_small_lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 6u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].chunk_id.value, 1u);
}

TEST(TerrainLodSelector, ProjectedAreaWeightsRefinementPriority)
{
    const std::vector lods{
        lod(0u, 12u, 0.0f),
        lod(1u, 10u, 1.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-4.0f, -2.0f, 10.0f, -1.0f, 2.0f, 11.0f), lods),
        chunk(1u, box(1.0f, -0.25f, 10.0f, 1.5f, 0.25f, 11.0f), lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 22u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 2u);
    EXPECT_EQ(choices[0].chunk_id.value, 0u);
    EXPECT_EQ(choices[0].lod_id.value, 0u);
    EXPECT_EQ(choices[1].chunk_id.value, 1u);
    EXPECT_EQ(choices[1].lod_id.value, 1u);
}

TEST(TerrainLodSelector, BenefitCostRatioBeatsRawBenefit)
{
    const std::vector cheap_lods{
        lod(0u, 12u, 0.2f),
        lod(1u, 10u, 1.0f),
    };
    const std::vector expensive_lods{
        lod(0u, 20u, 0.0f),
        lod(1u, 10u, 2.4f),
    };
    const std::vector chunks{
        chunk(0u, box(-2.0f, -1.0f, 10.0f, -0.5f, 1.0f, 11.0f), cheap_lods),
        chunk(1u, box(0.5f, -1.0f, 10.0f, 2.0f, 1.0f, 11.0f), expensive_lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 30u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 2u);
    EXPECT_EQ(choices[0].lod_id.value, 0u);
    EXPECT_EQ(choices[1].lod_id.value, 1u);
}

TEST(TerrainLodSelector, HysteresisSuppressesThresholdFlicker)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 10u, 0.44f),
    };
    const std::vector chunks{
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();
    const std::vector previous{
        TerrainLodChoice{
            .terrain_instance_index = 0u,
            .chunk_id = TerrainChunkId{ 0u },
            .lod_id = TerrainLodId{ 1u },
        },
    };

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view, previous);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].lod_id.value, 1u);
}

TEST(TerrainLodSelector, SingleLodChunkIsSelectedWhenBudgetAllows)
{
    const std::vector lods{
        lod(0u, 7u, 8.0f),
    };
    const std::vector chunks{
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods),
    };
    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 7u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].lod_id.value, 0u);
}

TEST(TerrainLodSelector, AssetDensityThresholdMasksDenseChunks)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 10u, 0.0f),
    };
    const std::vector chunks{
        dense_chunk(
            0u,
            box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f),
            12.0f,
            lods),
        dense_chunk(
            1u,
            box(2.0f, -1.0f, 10.0f, 4.0f, 1.0f, 11.0f),
            3.0f,
            lods),
    };
    ViewData view = test_view();
    view.terrain_lod.max_asset_triangle_density = 5.0f;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].chunk_id.value, 1u);
    EXPECT_FLOAT_EQ(choices[0].asset_triangle_density, 3.0f);
}

TEST(TerrainLodSelector, ScreenDensityThresholdMasksOverdenseChunks)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 10u, 0.0f),
    };
    const std::vector chunks{
        dense_chunk(
            0u,
            box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f),
            2.0f,
            lods),
    };
    ViewData view = test_view();

    const std::vector<TerrainLodChoice> baseline =
        select_terrain_lods(chunks, view.terrain_lod, view);
    ASSERT_EQ(baseline.size(), 1u);
    ASSERT_GT(baseline[0].screen_triangle_density, 0.0f);

    view.terrain_lod.max_screen_triangle_density =
        baseline[0].screen_triangle_density * 0.5f;
    const std::vector<TerrainLodChoice> masked =
        select_terrain_lods(chunks, view.terrain_lod, view);
    EXPECT_TRUE(masked.empty());

    view.terrain_lod.max_screen_triangle_density =
        baseline[0].screen_triangle_density * 2.0f;
    const std::vector<TerrainLodChoice> allowed =
        select_terrain_lods(chunks, view.terrain_lod, view);
    ASSERT_EQ(allowed.size(), 1u);
    EXPECT_FLOAT_EQ(
        allowed[0].screen_triangle_density,
        baseline[0].screen_triangle_density);
}

TEST(TerrainLodSelector, SurfelLevelsAreOptIn)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 4u, 0.0f),
    };
    const std::vector surfels{
        surfel_level(0u, 4u, 1.0f, 0.25f),
        surfel_level(1u, 1u, 2.0f, 1.0f),
    };
    TerrainChunkInfo terrain_chunk =
        chunk(0u, box(-1.0f, -1.0f, 40.0f, 1.0f, 1.0f, 41.0f), lods);
    terrain_chunk.surfel_density_levels = surfels;
    const std::vector chunks{ terrain_chunk };
    ViewData view = test_view();

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(
        choices[0].representation_kind,
        TerrainVisualRepresentationKind::MeshChunks);
    EXPECT_EQ(choices[0].lod_id.value, 1u);
}

TEST(TerrainLodSelector, FarFieldChoosesSurfelDensityWhenEnabled)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 4u, 0.0f),
    };
    const std::vector surfels{
        surfel_level(0u, 4u, 1.0f, 0.25f),
        surfel_level(1u, 2u, 2.0f, 0.5f),
        surfel_level(2u, 1u, 4.0f, 1.0f),
    };
    TerrainChunkInfo terrain_chunk =
        chunk(0u, box(-1.0f, -1.0f, 40.0f, 1.0f, 1.0f, 41.0f), lods);
    terrain_chunk.surfel_density_levels = surfels;
    const std::vector chunks{ terrain_chunk };
    ViewData view = test_view();
    view.terrain_lod.enable_surfel_lods = true;
    view.terrain_lod.surfel_target_coverage_px = 64.0f;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(
        choices[0].representation_kind,
        TerrainVisualRepresentationKind::SurfelCloud);
    EXPECT_EQ(choices[0].lod_id.value, 2u);
}

TEST(TerrainLodSelector, ProjectedRadiusCanSelectMiddleSurfelDensity)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 4u, 0.0f),
    };
    const std::vector surfels{
        surfel_level(0u, 4u, 1.0f, 0.25f),
        surfel_level(1u, 2u, 2.0f, 0.5f),
        surfel_level(2u, 1u, 4.0f, 1.0f),
    };
    TerrainChunkInfo terrain_chunk =
        chunk(0u, box(-1.0f, -1.0f, 20.0f, 1.0f, 1.0f, 21.0f), lods);
    terrain_chunk.surfel_density_levels = surfels;
    const std::vector chunks{ terrain_chunk };
    ViewData view = test_view();
    view.terrain_lod.enable_surfel_lods = true;
    view.terrain_lod.surfel_target_coverage_px = 1.0f;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(
        choices[0].representation_kind,
        TerrainVisualRepresentationKind::SurfelCloud);
    EXPECT_EQ(choices[0].lod_id.value, 1u);
}

TEST(TerrainLodSelector, SurfelFallbackParticipatesInCoarseBudget)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 4u, 0.0f),
    };
    const std::vector surfels{
        surfel_level(0u, 4u, 1.0f, 0.25f),
        surfel_level(1u, 2u, 2.0f, 0.5f),
        surfel_level(2u, 1u, 4.0f, 1.0f),
    };
    TerrainChunkInfo terrain_chunk =
        chunk(0u, box(-1.0f, -1.0f, 40.0f, 1.0f, 1.0f, 41.0f), lods);
    terrain_chunk.surfel_density_levels = surfels;
    const std::vector chunks{ terrain_chunk };
    ViewData view = test_view();
    view.terrain_lod.enable_surfel_lods = true;
    view.terrain_lod.triangle_budget = 2u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(
        choices[0].representation_kind,
        TerrainVisualRepresentationKind::SurfelCloud);
    EXPECT_EQ(choices[0].lod_id.value, 2u);
}

TEST(TerrainLodSelector, SurfelSelectionKeepsMeshWhenCoarseErrorIsTooHigh)
{
    const std::vector lods{
        lod(0u, 100u, 0.0f),
        lod(1u, 4u, 8.0f),
    };
    const std::vector surfels{
        surfel_level(0u, 4u, 1.0f, 0.25f),
        surfel_level(1u, 1u, 2.0f, 1.0f),
    };
    TerrainChunkInfo terrain_chunk =
        chunk(0u, box(-1.0f, -1.0f, 10.0f, 1.0f, 1.0f, 11.0f), lods);
    terrain_chunk.surfel_density_levels = surfels;
    const std::vector chunks{ terrain_chunk };
    ViewData view = test_view();
    view.terrain_lod.enable_surfel_lods = true;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(
        choices[0].representation_kind,
        TerrainVisualRepresentationKind::MeshChunks);
    EXPECT_EQ(choices[0].lod_id.value, 0u);
}

TEST(TerrainLodSelector, NeighborConstraintRefinesCoarseNeighborWhenEnabled)
{
    const std::vector left_lods{
        lod(0u, 20u, 0.0f),
        lod(1u, 10u, 0.0f),
        lod(2u, 5u, 0.0f),
    };
    const std::vector right_lods{
        lod(0u, 20u, 0.0f),
        lod(1u, 10u, 0.2f),
        lod(2u, 5u, 1.0f),
    };
    TerrainChunkInfo left =
        chunk(0u, box(-2.0f, -1.0f, 10.0f, 0.0f, 1.0f, 11.0f), left_lods);
    TerrainChunkInfo right =
        chunk(1u, box(0.0f, -1.0f, 10.0f, 2.0f, 1.0f, 11.0f), right_lods);
    left.boundary.positive_x_neighbor = TerrainChunkId{ 1u };
    right.boundary.negative_x_neighbor = TerrainChunkId{ 0u };
    const std::vector chunks{ left, right };

    ViewData view = test_view();
    view.terrain_lod.triangle_budget = 30u;
    view.terrain_lod.enforce_neighbor_lod_delta = true;
    view.terrain_lod.max_neighbor_lod_delta = 1u;

    const std::vector<TerrainLodChoice> choices =
        select_terrain_lods(chunks, view.terrain_lod, view);

    ASSERT_EQ(choices.size(), 2u);
    EXPECT_LE(
        std::abs(
            static_cast<int>(choices[0].lod_id.value)
            - static_cast<int>(choices[1].lod_id.value)),
        1);
}
