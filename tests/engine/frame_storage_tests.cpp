#include <gtest/gtest.h>

#include <type_traits>

#include <engine/frame_storage.h>
#include <engine/frame_types.h>   // was reached transitively via benchmark_app.h

// The `wz::bench::` alias-identity test that used to live here went with
// BenchmarkApp: it asserted that benchmark_app.h's RenderPrepPath /
// FrameDirtyState / BenchFrameStorage typedefs were the engine's own types.
// With no second definition left to drift from, there is nothing to pin.

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

// Kept from the BenchmarkApp version of this test, minus the host. What it
// actually covered was FrameDirtyState's mark -> render_prep_path mapping; the
// app was only the thing that happened to own one.
TEST(FrameStorageTypes, MarkingTransformAndViewSelectsThatPrepPath)
{
    wz::engine::FrameDirtyState dirty{};

    dirty.mark_render_transform_and_view();

    EXPECT_EQ(
        dirty.render_prep_path(),
        wz::engine::RenderPrepPath::TransformAndView);
}
