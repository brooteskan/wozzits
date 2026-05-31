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
        wz::app::FrameStorage,
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        wz::bench::BenchFrameStorage,
        wz::engine::FrameStorage>);
}

TEST(FrameStorageTypes, DefaultsToFullCompile)
{
    wz::engine::FrameStorage frame{};

    EXPECT_EQ(
        frame.render_prep_path,
        wz::engine::RenderPrepPath::FullCompile);
}

TEST(FrameStorageTypes, AppAndBenchmarkFramesUseSharedStorage)
{
    wz::app::GameApp app{};
    wz::bench::BenchmarkApp bench{};

    static_assert(std::is_same_v<
        decltype(app.frame),
        wz::engine::FrameStorage>);
    static_assert(std::is_same_v<
        decltype(bench.frame),
        wz::engine::FrameStorage>);

    app.frame.render_prep_path = wz::engine::RenderPrepPath::ViewOnly;
    bench.frame.render_prep_path = wz::engine::RenderPrepPath::TransformAndView;

    EXPECT_EQ(
        app.frame.render_prep_path,
        wz::engine::RenderPrepPath::ViewOnly);
    EXPECT_EQ(
        bench.frame.render_prep_path,
        wz::engine::RenderPrepPath::TransformAndView);
}
