#include <engine/assets/terrain/terrain_lod_seams.h>

#include <gtest/gtest.h>

#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        TerrainVisualProxyBoundaryPoint point(
            float x,
            float y,
            float z) noexcept
        {
            return TerrainVisualProxyBoundaryPoint{ .position = { x, y, z } };
        }

        TerrainVisualProxyBounds bounds(
            float min_x,
            float min_y,
            float min_z,
            float max_x,
            float max_y,
            float max_z) noexcept
        {
            return TerrainVisualProxyBounds{
                .min = { min_x, min_y, min_z },
                .max = { max_x, max_y, max_z },
            };
        }

        TerrainVisualProxyLodRecord make_lod(
            TerrainLodId lod_id,
            TerrainVisualProxyBounds lod_bounds,
            TerrainVisualProxyBoundaryEdge edge,
            std::vector<TerrainVisualProxyBoundaryPoint> ring)
        {
            TerrainVisualProxyLodRecord lod{};
            lod.lod_id = lod_id;
            lod.representation_id =
                TerrainRepresentationId{ 10u + lod_id.value };
            lod.bounds = lod_bounds;
            lod.first_index = lod_id.value * 6u;
            lod.index_count = 6u;
            lod.first_vertex = lod_id.value * 4u;
            lod.vertex_count = static_cast<uint32_t>(ring.size());
            lod.triangle_count = 2u;
            lod.conservative_geometric_error = static_cast<float>(lod_id.value);

            switch (edge) {
            case TerrainVisualProxyBoundaryEdge::NegativeX:
                lod.boundary_ring.negative_x = std::move(ring);
                break;
            case TerrainVisualProxyBoundaryEdge::PositiveX:
                lod.boundary_ring.positive_x = std::move(ring);
                break;
            case TerrainVisualProxyBoundaryEdge::NegativeZ:
                lod.boundary_ring.negative_z = std::move(ring);
                break;
            case TerrainVisualProxyBoundaryEdge::PositiveZ:
                lod.boundary_ring.positive_z = std::move(ring);
                break;
            }
            return lod;
        }

        TerrainVisualProxyChunkRecord make_chunk(
            TerrainChunkId chunk_id,
            TerrainVisualProxyBounds chunk_bounds,
            TerrainVisualChunkBoundaryMetadata boundary,
            std::vector<TerrainVisualProxyLodRecord> lods)
        {
            TerrainVisualProxyChunkRecord chunk{};
            chunk.chunk_id = chunk_id;
            chunk.representation_id =
                TerrainRepresentationId{ 100u + chunk_id.value };
            chunk.bounds = chunk_bounds;
            chunk.triangle_count = 2u;
            chunk.vertex_count = 4u;
            chunk.boundary = boundary;
            chunk.lods = std::move(lods);
            return chunk;
        }

        TerrainVisualProxyData two_chunk_proxy()
        {
            const TerrainVisualProxyBounds left_bounds =
                bounds(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
            const TerrainVisualProxyBounds right_bounds =
                bounds(1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f);

            TerrainVisualProxyData proxy{};
            proxy.compiler_version = 1u;
            proxy.source_asset_key = wz::asset::AssetKey{ { 10u, 20u } };
            proxy.terrain_proxy_id =
                TerrainProxyId{ .key = wz::asset::AssetKey{ { 30u, 40u } } };
            proxy.bounds = bounds(0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f);

            proxy.chunks.push_back(make_chunk(
                TerrainChunkId{ 0u },
                left_bounds,
                TerrainVisualChunkBoundaryMetadata{
                    .boundary_flags = TerrainVisualChunkBoundary_PositiveX,
                    .positive_x_neighbor = TerrainChunkId{ 1u },
                },
                {
                    make_lod(
                        TerrainLodId{ 0u },
                        left_bounds,
                        TerrainVisualProxyBoundaryEdge::PositiveX,
                        { point(1.0f, 0.0f, 0.0f),
                          point(1.0f, 0.0f, 1.0f) }),
                    make_lod(
                        TerrainLodId{ 1u },
                        left_bounds,
                        TerrainVisualProxyBoundaryEdge::PositiveX,
                        { point(1.0f, 0.0f, 0.5f) }),
                }));

            proxy.chunks.push_back(make_chunk(
                TerrainChunkId{ 1u },
                right_bounds,
                TerrainVisualChunkBoundaryMetadata{
                    .boundary_flags = TerrainVisualChunkBoundary_NegativeX,
                    .negative_x_neighbor = TerrainChunkId{ 0u },
                },
                {
                    make_lod(
                        TerrainLodId{ 0u },
                        right_bounds,
                        TerrainVisualProxyBoundaryEdge::NegativeX,
                        { point(1.0f, 0.0f, 0.0f),
                          point(1.0f, 0.0f, 1.0f) }),
                }));

            return proxy;
        }
    }

    TEST(TerrainLodSeams, OppositeBoundaryEdgePairsAxes)
    {
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::PositiveX,
            opposite_terrain_boundary_edge(
                TerrainVisualProxyBoundaryEdge::NegativeX));
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::NegativeX,
            opposite_terrain_boundary_edge(
                TerrainVisualProxyBoundaryEdge::PositiveX));
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::PositiveZ,
            opposite_terrain_boundary_edge(
                TerrainVisualProxyBoundaryEdge::NegativeZ));
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::NegativeZ,
            opposite_terrain_boundary_edge(
                TerrainVisualProxyBoundaryEdge::PositiveZ));
    }

    TEST(TerrainLodSeams, EqualLodSharedBoundaryHasNoGapOrTransition)
    {
        const TerrainVisualProxyData proxy = two_chunk_proxy();

        const TerrainLodSeamReport report = analyze_terrain_lod_seam(
            proxy,
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 0u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::PositiveX,
            },
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 1u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::NegativeX,
            },
            0.001f);

        EXPECT_TRUE(report.valid());
        EXPECT_EQ(2u, report.a_boundary_points);
        EXPECT_EQ(2u, report.b_boundary_points);
        EXPECT_FLOAT_EQ(0.0f, report.max_boundary_gap);
        EXPECT_TRUE(report.has_boundary_data());
        EXPECT_FALSE(report.needs_transition);
        EXPECT_FALSE(report.gap_exceeds_tolerance);
    }

    TEST(TerrainLodSeams, MixedLodSharedBoundaryNeedsTransitionAndReportsGap)
    {
        const TerrainVisualProxyData proxy = two_chunk_proxy();

        const TerrainLodSeamReport report = analyze_terrain_lod_seam(
            proxy,
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 0u },
                .lod_id = TerrainLodId{ 1u },
                .edge = TerrainVisualProxyBoundaryEdge::PositiveX,
            },
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 1u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::NegativeX,
            },
            0.001f);

        EXPECT_TRUE(report.valid());
        EXPECT_EQ(1u, report.a_boundary_points);
        EXPECT_EQ(2u, report.b_boundary_points);
        EXPECT_FLOAT_EQ(0.5f, report.max_boundary_gap);
        EXPECT_TRUE(report.has_boundary_data());
        EXPECT_TRUE(report.needs_transition);
        EXPECT_TRUE(report.gap_exceeds_tolerance);
    }

    TEST(TerrainLodSeams, InvalidEndpointReportsToleranceFailure)
    {
        const TerrainVisualProxyData proxy = two_chunk_proxy();

        const TerrainLodSeamReport missing_chunk = analyze_terrain_lod_seam(
            proxy,
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 99u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::PositiveX,
            },
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 1u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::NegativeX,
            },
            0.001f);

        EXPECT_FALSE(missing_chunk.has_boundary_data());
        EXPECT_FALSE(missing_chunk.valid());
        EXPECT_TRUE(missing_chunk.gap_exceeds_tolerance);

        const TerrainLodSeamReport missing_lod = analyze_terrain_lod_seam(
            proxy,
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 0u },
                .lod_id = TerrainLodId{ 99u },
                .edge = TerrainVisualProxyBoundaryEdge::PositiveX,
            },
            TerrainLodSeamEndpoint{
                .chunk_id = TerrainChunkId{ 1u },
                .lod_id = TerrainLodId{ 0u },
                .edge = TerrainVisualProxyBoundaryEdge::NegativeX,
            },
            0.001f);

        EXPECT_FALSE(missing_lod.has_boundary_data());
        EXPECT_FALSE(missing_lod.valid());
        EXPECT_TRUE(missing_lod.gap_exceeds_tolerance);
    }

    TEST(TerrainLodSeams, AdjacentSeamAnalysisUsesNeighborMetadata)
    {
        const TerrainVisualProxyData proxy = two_chunk_proxy();
        const std::vector<TerrainLodSelection> selections{
            TerrainLodSelection{
                .chunk_id = TerrainChunkId{ 0u },
                .lod_id = TerrainLodId{ 1u },
            },
            TerrainLodSelection{
                .chunk_id = TerrainChunkId{ 1u },
                .lod_id = TerrainLodId{ 0u },
            },
        };

        const std::vector<TerrainLodSeamReport> reports =
            analyze_adjacent_terrain_lod_seams(proxy, selections, 0.001f);

        ASSERT_EQ(1u, reports.size());
        EXPECT_EQ(TerrainChunkId{ 0u }, reports[0].a.chunk_id);
        EXPECT_EQ(TerrainChunkId{ 1u }, reports[0].b.chunk_id);
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::PositiveX,
            reports[0].a.edge);
        EXPECT_EQ(
            TerrainVisualProxyBoundaryEdge::NegativeX,
            reports[0].b.edge);
        EXPECT_TRUE(reports[0].needs_transition);
        EXPECT_TRUE(reports[0].gap_exceeds_tolerance);
    }

    TEST(TerrainLodSeams, AdjacentSeamAnalysisSkipsUnselectedChunks)
    {
        const TerrainVisualProxyData proxy = two_chunk_proxy();
        const std::vector<TerrainLodSelection> selections{
            TerrainLodSelection{
                .chunk_id = TerrainChunkId{ 0u },
                .lod_id = TerrainLodId{ 1u },
            },
        };

        const std::vector<TerrainLodSeamReport> reports =
            analyze_adjacent_terrain_lod_seams(proxy, selections, 0.001f);

        EXPECT_TRUE(reports.empty());
    }
}
