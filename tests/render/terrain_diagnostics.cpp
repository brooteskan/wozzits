#include <gtest/gtest.h>

#include <engine/rendering/render_resource_resolver.h>
#include <render/diagnostics/terrain_diagnostics.h>

#include <span>

using namespace wz::render;

TEST(TerrainDiagnostics, AccumulatesFrameSchemaCounters)
{
    TerrainFrameDiagnostics frame{};
    frame.source_triangles = 100u;
    frame.proxy_chunks = 8u;
    frame.visible_chunks = 5u;
    frame.submitted_triangles = 64u;
    frame.submitted_draw_calls = 5u;
    frame.lod_histogram[2] = 3u;
    frame.selector_cpu_us = 12.5;
    frame.terrain_gpu_us = 40.0;
    frame.terrain_gpu_us_valid = true;
    frame.budget_target_triangles = 80u;
    frame.budget_misses = 1u;
    frame.representation_counts.mesh_chunks = 5u;

    TerrainDiagnosticsAccumulator accumulator;
    accumulator.add_frame(frame);
    accumulator.add_frame(frame);

    const TerrainDiagnosticsSummary& summary = accumulator.summary();
    EXPECT_EQ(summary.frame_count, 2u);
    EXPECT_EQ(summary.totals.source_triangles, 200u);
    EXPECT_EQ(summary.totals.proxy_chunks, 16u);
    EXPECT_EQ(summary.totals.visible_chunks, 10u);
    EXPECT_EQ(summary.totals.submitted_triangles, 128u);
    EXPECT_EQ(summary.totals.submitted_draw_calls, 10u);
    EXPECT_EQ(summary.totals.lod_histogram[2], 6u);
    EXPECT_DOUBLE_EQ(summary.totals.selector_cpu_us, 25.0);
    EXPECT_TRUE(summary.totals.terrain_gpu_us_valid);
    EXPECT_DOUBLE_EQ(summary.totals.terrain_gpu_us, 80.0);
    EXPECT_EQ(summary.totals.budget_target_triangles, 160u);
    EXPECT_EQ(summary.totals.budget_misses, 2u);
    EXPECT_EQ(summary.totals.representation_counts.mesh_chunks, 10u);

    accumulator.reset();
    EXPECT_EQ(accumulator.summary().frame_count, 0u);
}

TEST(TerrainDiagnostics, ResolverProjectsFrameDiagnosticsFromRenderStats)
{
    wz::engine::rendering::RenderResourceResolver resolver;

    resolver.record_terrain_render_stats(
        4u,
        3u,
        120u,
        48u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        16.0f,
        0.0f,
        0.0f,
        0.0,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        2u,
        wz::engine::assets::TerrainVisualRepresentationKind::GridTiles,
        3u);
    resolver.record_terrain_visible_chunks(5u);
    resolver.record_terrain_selector_cpu_us(7.25);
    resolver.record_terrain_gpu_us(11.5);
    resolver.record_terrain_budget_diagnostics(64u, false);
    resolver.record_terrain_budget_diagnostics(64u, true);

    const TerrainFrameDiagnostics diagnostics =
        resolver.terrain_frame_diagnostics();
    EXPECT_EQ(diagnostics.source_triangles, 120u);
    EXPECT_EQ(diagnostics.proxy_chunks, 4u);
    EXPECT_EQ(diagnostics.visible_chunks, 5u);
    EXPECT_EQ(diagnostics.submitted_triangles, 48u);
    EXPECT_EQ(diagnostics.submitted_draw_calls, 3u);
    EXPECT_EQ(diagnostics.lod_histogram[2], 3u);
    EXPECT_DOUBLE_EQ(diagnostics.selector_cpu_us, 7.25);
    EXPECT_TRUE(diagnostics.terrain_gpu_us_valid);
    EXPECT_DOUBLE_EQ(diagnostics.terrain_gpu_us, 11.5);
    EXPECT_EQ(diagnostics.budget_target_triangles, 64u);
    EXPECT_EQ(diagnostics.budget_misses, 1u);
    EXPECT_EQ(diagnostics.representation_counts.grid_tiles, 3u);

    const auto stats = resolver.terrain_render_stats();
    EXPECT_EQ(stats.total_triangles, diagnostics.source_triangles);
    EXPECT_EQ(stats.total_chunks, diagnostics.proxy_chunks);
    EXPECT_EQ(stats.submitted_chunks, 3u);
    EXPECT_EQ(stats.visible_chunks, diagnostics.visible_chunks);
    EXPECT_EQ(stats.submitted_triangles, diagnostics.submitted_triangles);
    EXPECT_EQ(stats.lod_histogram[2], diagnostics.lod_histogram[2]);
    EXPECT_EQ(
        stats.representation_counts.grid_tiles,
        diagnostics.representation_counts.grid_tiles);
}

TEST(TerrainDiagnostics, ExplicitZeroSubmittedDrawCallsDoesNotFallbackToChunks)
{
    wz::engine::rendering::RenderResourceResolver resolver;

    resolver.record_terrain_render_stats(
        0u,
        3u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        3u,
        42u,
        0.0f,
        0.0f,
        0.0f,
        0.0,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        wz::engine::assets::TerrainVisualRepresentationKind::SurfelCloud,
        0u);

    const TerrainFrameDiagnostics diagnostics =
        resolver.terrain_frame_diagnostics();
    EXPECT_EQ(diagnostics.submitted_draw_calls, 0u);
    EXPECT_EQ(diagnostics.representation_counts.surfel_clouds, 3u);
}

TEST(TerrainDiagnostics, ResolverRecordsTerrainSubmitCpuProfile)
{
    wz::engine::rendering::RenderResourceResolver resolver;

    resolver.record_terrain_submit_cpu_profile(
        100.0,
        10.0,
        11.0,
        12.0,
        13.0,
        14.0,
        15.0,
        7u,
        3u,
        2u,
        1u);
    resolver.record_terrain_submit_cpu_profile(
        50.0,
        1.0,
        2.0,
        3.0,
        4.0,
        5.0,
        6.0,
        5u,
        4u,
        3u,
        2u);

    const auto stats = resolver.terrain_render_stats();
    EXPECT_DOUBLE_EQ(stats.terrain_submit_cpu_us, 150.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_resolve_cpu_us, 11.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_resource_cpu_us, 13.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_constants_cpu_us, 15.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_bind_cpu_us, 17.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_draw_cpu_us, 19.0);
    EXPECT_DOUBLE_EQ(stats.terrain_submit_stats_cpu_us, 21.0);
    EXPECT_EQ(stats.terrain_submit_surfel_draw_calls, 12u);
    EXPECT_EQ(stats.terrain_submit_mesh_draw_calls, 7u);
    EXPECT_EQ(stats.terrain_submit_fallback_mesh_draw_calls, 5u);
    EXPECT_EQ(stats.terrain_submit_surfel_fallbacks, 3u);
}

TEST(TerrainDiagnostics, ResolverExposesProxyWideDiagnostics)
{
    using namespace wz::engine::assets;

    wz::engine::rendering::RenderResourceResolver resolver;

    TerrainVisualChunk chunks[2]{};
    chunks[0].first_index = 0u;
    chunks[0].index_count = 6u;
    chunks[0].aggregate.triangle_count = 2u;
    chunks[1].first_index = 6u;
    chunks[1].index_count = 12u;
    chunks[1].aggregate.triangle_count = 4u;

    const wz::gpu::GPUHandle gpu_mesh{
        .id = 3u,
        .epoch = 1u,
        .type = wz::gpu::GPUResourceType::Mesh,
    };

    wz::asset::AssetKey proxy_key{};
    proxy_key.content_hash = { 0x2222u, 0x3333u };
    const TerrainProxyId proxy_id{ proxy_key };

    ASSERT_TRUE(resolver.register_terrain_proxy(
        proxy_id,
        gpu_mesh,
        BuiltinRenderProgram::TerrainSurfelSurface,
        {},
        {},
        0.0f,
        {},
        std::span<const TerrainVisualChunk>(chunks, 2u)));

    const auto diagnostics =
        resolver.resolve_terrain_proxy_diagnostics(proxy_id);
    ASSERT_TRUE(diagnostics.has_value());
    EXPECT_EQ(diagnostics->proxy_chunks, 2u);
    EXPECT_EQ(diagnostics->source_triangles, 6u);
    EXPECT_EQ(diagnostics->visible_chunks, 0u);
    EXPECT_EQ(diagnostics->submitted_triangles, 0u);
}

TEST(TerrainDiagnostics, ComputesProjectedErrorPercentiles)
{
    const float samples[]{ 8.0f, 1.0f, 4.0f, 16.0f, 2.0f };

    const TerrainFrameDiagnostics diagnostics =
        terrain_projected_error_diagnostics(samples);

    EXPECT_EQ(diagnostics.projected_error_sample_count, 5u);
    EXPECT_FLOAT_EQ(diagnostics.max_projected_error_px, 16.0f);
    EXPECT_FLOAT_EQ(diagnostics.median_projected_error_px, 4.0f);
    EXPECT_FLOAT_EQ(diagnostics.p95_projected_error_px, 16.0f);
}

TEST(TerrainDiagnostics, ResolverRecordsProjectedErrorSamples)
{
    wz::engine::rendering::RenderResourceResolver resolver;
    const float samples[]{ 8.0f, 1.0f, 4.0f, -1.0f, 2.0f, 12.0f };

    resolver.record_terrain_projected_error_samples(samples);

    const TerrainFrameDiagnostics diagnostics =
        resolver.terrain_frame_diagnostics();
    const auto stats = resolver.terrain_render_stats();

    EXPECT_EQ(diagnostics.projected_error_sample_count, 5u);
    EXPECT_FLOAT_EQ(diagnostics.max_projected_error_px, 12.0f);
    EXPECT_FLOAT_EQ(diagnostics.median_projected_error_px, 4.0f);
    EXPECT_FLOAT_EQ(diagnostics.p95_projected_error_px, 12.0f);
    EXPECT_EQ(
        stats.projected_error_sample_count,
        diagnostics.projected_error_sample_count);
    EXPECT_FLOAT_EQ(
        stats.max_projected_error_px,
        diagnostics.max_projected_error_px);
}
