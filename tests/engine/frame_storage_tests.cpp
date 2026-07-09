#include <gtest/gtest.h>

#include <type_traits>

#include <bench/benchmark_app.h>
#include <engine/frame_storage.h>

TEST(FrameStorageTypes, BenchAliasesUseSharedEngineStorage)
{
    static_assert(std::is_same_v<
        wz::bench::RenderPrepPath,
        wz::engine::RenderPrepPath>);
    static_assert(std::is_same_v<
        wz::bench::FrameDirtyState,
        wz::engine::FrameDirtyState>);
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

TEST(FrameDirtyState, NamesRenderPrepPaths)
{
    EXPECT_STREQ(
        wz::engine::render_prep_path_name(wz::engine::RenderPrepPath::FullCompile),
        "FullCompile");
    EXPECT_STREQ(
        wz::engine::render_prep_path_name(wz::engine::RenderPrepPath::ViewOnly),
        "ViewOnly");
    EXPECT_STREQ(
        wz::engine::render_prep_path_name(wz::engine::RenderPrepPath::TransformOnly),
        "TransformOnly");
    EXPECT_STREQ(
        wz::engine::render_prep_path_name(wz::engine::RenderPrepPath::TransformAndView),
        "TransformAndView");
}

TEST(FrameStorageTypes, BenchmarkFrameUsesSharedStorageAndDirtyState)
{
    wz::bench::BenchmarkApp bench{};

    static_assert(std::is_same_v<
        decltype(bench.frame),
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        decltype(bench.frame_dirty),
        wz::engine::FrameDirtyState>);

    bench.frame_dirty.mark_render_transform_and_view();

    EXPECT_EQ(
        bench.frame_dirty.render_prep_path(),
        wz::engine::RenderPrepPath::TransformAndView);
}
