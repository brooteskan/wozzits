#include <gtest/gtest.h>

#include <type_traits>

#include <engine/benchmark_app.h>
#include <engine/frame_storage.h>
#include <engine/game_app.h>

TEST(FrameStorageTypes, AppAndBenchAliasesUseSharedEngineStorage)
{
    static_assert(std::is_same_v<
        wz::app::RenderPrepPath,
        wz::engine::RenderPrepPath>);
    static_assert(std::is_same_v<
        wz::bench::RenderPrepPath,
        wz::engine::RenderPrepPath>);
    static_assert(std::is_same_v<
        wz::app::FrameDirtyState,
        wz::engine::FrameDirtyState>);
    static_assert(std::is_same_v<
        wz::bench::FrameDirtyState,
        wz::engine::FrameDirtyState>);
    static_assert(std::is_same_v<
        wz::app::FrameStorage,
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        wz::bench::BenchFrameStorage,
        wz::engine::FrameStorage>);
}

TEST(FrameDirtyState, DefaultsToFullCompile)
{
    wz::engine::FrameDirtyState dirty{};

    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::FullCompile);
}

TEST(FrameDirtyState, TracksRenderPrepPathFromDirtyBits)
{
    wz::engine::FrameDirtyState dirty{};

    dirty.mark_render_view_only();
    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::ViewOnly);

    dirty.mark_render_transform_only();
    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::TransformOnly);

    dirty.mark_render_transform_and_view();
    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::TransformAndView);

    dirty.mark_render_full_compile();
    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::FullCompile);
}

TEST(FrameStorageTypes, AppAndBenchmarkFramesUseSharedStorageAndDirtyState)
{
    wz::app::GameApp app{};
    wz::bench::BenchmarkApp bench{};

    static_assert(std::is_same_v<
        decltype(app.frame),
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        decltype(bench.frame),
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        decltype(app.frame_dirty),
        wz::engine::FrameDirtyState>);
    static_assert(std::is_same_v<
        decltype(bench.frame_dirty),
        wz::engine::FrameDirtyState>);

    app.frame_dirty.mark_render_view_only();
    bench.frame_dirty.mark_render_transform_and_view();

    EXPECT_EQ(
        app.frame_dirty.render_prep_path(),
        wz::engine::RenderPrepPath::ViewOnly);
    EXPECT_EQ(
        bench.frame_dirty.render_prep_path(),
        wz::engine::RenderPrepPath::TransformAndView);
}
