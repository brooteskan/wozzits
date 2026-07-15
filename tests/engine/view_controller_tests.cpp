#include <gtest/gtest.h>

#include <engine/app/view_controller.h>

#include <bench/flying_camera.h>
#include <logging/logger.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <math/quaternion.h>

#include <optional>

using wz::app::ViewController;
using wz::math::Mat4;
using wz::math::Vec3;

namespace
{
    // A frame of input that walks the camera forward.
    wz::input::InputState forward_input()
    {
        wz::input::InputState input{};
        input.keyboard.down['W'] = true;
        return input;
    }

    // The host's per-frame call for the free-fly source: the scene arguments are
    // ignored, so a resolved/unresolved world matrix is irrelevant here.
    void materialize(ViewController& view, wz::Logger& logger)
    {
        view.update_active_view(std::nullopt, 0u, false, logger);
    }

    wz::engine::assets::SceneCameraAsset camera_params()
    {
        return {
            .fov_y      = 1.0f,
            .near_plane = 0.5f,
            .far_plane  = 500.0f,
        };
    }
}

// --- update_free_fly: movement + the editor-dirty policy ---------------------

TEST(ViewController, FreeFlyReportsMovementAndHoldStillStaysClean)
{
    ViewController view;
    wz::Logger logger;

    EXPECT_TRUE(view.update_free_fly(forward_input(), 0.1f));
    EXPECT_NE(view.free_fly_camera().z, 0.0f);

    // A frame with no input must not report movement -- that is what keeps a
    // still viewport from dirtying the editor camera every frame.
    const wz::input::InputState idle{};
    EXPECT_FALSE(view.update_free_fly(idle, 0.1f));
}

TEST(ViewController, MovingTheFreeFlyCameraDirtiesTheEditorCamera)
{
    ViewController view;   // editor mode: prefer_scene_camera defaults false

    ASSERT_FALSE(view.editor_camera_dirty());

    ASSERT_TRUE(view.update_free_fly(forward_input(), 0.1f));
    EXPECT_TRUE(view.editor_camera_dirty());

    view.clear_editor_camera_dirty();
    EXPECT_FALSE(view.editor_camera_dirty());
}

TEST(ViewController, IdleFrameDoesNotDirtyTheEditorCamera)
{
    ViewController view;

    const wz::input::InputState idle{};
    ASSERT_FALSE(view.update_free_fly(idle, 0.1f));
    EXPECT_FALSE(view.editor_camera_dirty());
}

TEST(ViewController, PlayModeMovementDoesNotDirtyTheEditorCamera)
{
    ViewController view;
    view.set_prefer_scene_camera(true);   // standalone play

    // The camera still moves -- play just does not author editor viewport state.
    EXPECT_TRUE(view.update_free_fly(forward_input(), 0.1f));
    EXPECT_FALSE(view.editor_camera_dirty());
}

// --- camera-source policy ---------------------------------------------------

TEST(ViewController, EditorRecordsTheAnchorButStaysOnFreeFly)
{
    ViewController view;
    wz::Logger logger;

    ASSERT_FALSE(view.has_scene_camera());
    ASSERT_FALSE(view.scene_source_active());

    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    // Anchor recorded so a later editor toggle is a cheap source flip...
    EXPECT_TRUE(view.has_scene_camera());
    EXPECT_EQ(view.active_scene_camera_id(), "cam_node");
    EXPECT_EQ(view.active_camera_entity(), 7u);

    // ...but the edit camera stays active, so the viewport is still navigable.
    EXPECT_FALSE(view.scene_source_active());
}

TEST(ViewController, PlayFlipsTheSourceToTheSelectedSceneCamera)
{
    ViewController view;
    wz::Logger logger;
    view.set_prefer_scene_camera(true);

    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    EXPECT_TRUE(view.has_scene_camera());
    EXPECT_TRUE(view.scene_source_active());
}

TEST(ViewController, ClearDropsTheAnchorAndFallsBackToFreeFly)
{
    ViewController view;
    wz::Logger logger;
    view.set_prefer_scene_camera(true);

    view.select_scene_camera("cam_node", 7u, camera_params(), logger);
    ASSERT_TRUE(view.scene_source_active());

    // Every (re)load re-decides the camera.
    view.clear_scene_camera();

    EXPECT_FALSE(view.has_scene_camera());
    EXPECT_TRUE(view.active_scene_camera_id().empty());
    EXPECT_FALSE(view.scene_source_active());
    EXPECT_EQ(view.active_camera_entity(), wz::scene::INVALID_RUNTIME_ENTITY);
}

TEST(ViewController, ReseatingTheEntityKeepsTheSceneSourceAlive)
{
    ViewController view;
    wz::Logger logger;
    view.set_prefer_scene_camera(true);

    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    // A behavior-scene rebuild renumbers the polytree; the id anchor is
    // unchanged and the source must survive the re-seat.
    view.set_active_camera_entity(42u);

    EXPECT_EQ(view.active_camera_entity(), 42u);
    EXPECT_EQ(view.active_scene_camera_id(), "cam_node");
    EXPECT_TRUE(view.scene_source_active());
}

// --- update_active_view: free-fly source ------------------------------------

TEST(ViewController, FreeFlyViewTracksTheCameraPosition)
{
    ViewController view;
    wz::Logger logger;

    view.free_fly_camera().x = 1.0f;
    view.free_fly_camera().y = 2.0f;
    view.free_fly_camera().z = 3.0f;

    materialize(view, logger);

    // world_position is what the clipmap lattice snaps against.
    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 1.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.y, 2.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.z, 3.0f);
}

TEST(ViewController, FreeFlyViewProjectionRespondsToTheCamera)
{
    ViewController view;
    wz::Logger logger;

    materialize(view, logger);
    const Mat4 at_origin = view.active_view().view_projection;

    view.free_fly_camera().z = 10.0f;
    materialize(view, logger);
    const Mat4 moved = view.active_view().view_projection;

    bool differs = false;
    for (int i = 0; i < 16; ++i) {
        differs = differs || at_origin.m[i] != moved.m[i];
    }
    EXPECT_TRUE(differs);
}

// --- update_active_view: scene source ---------------------------------------

TEST(ViewController, SceneViewTakesItsPositionFromTheNodeWorldMatrix)
{
    ViewController view;
    wz::Logger logger;
    view.set_prefer_scene_camera(true);
    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    // The free-fly camera sits somewhere else entirely; the scene source must
    // ignore it.
    view.free_fly_camera().x = -100.0f;

    const Mat4 world = wz::math::translation(Vec3{ 4.0f, 5.0f, 6.0f });
    view.update_active_view(world, 1u, true, logger);

    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 4.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.y, 5.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.z, 6.0f);
}

TEST(ViewController, UnresolvedSceneCameraHoldsTheLastViewInsteadOfFlipping)
{
    ViewController view;
    wz::Logger logger;
    view.set_prefer_scene_camera(true);
    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    const Mat4 world = wz::math::translation(Vec3{ 4.0f, 5.0f, 6.0f });
    view.update_active_view(world, 1u, true, logger);
    const Mat4 resolved_vp = view.active_view().view_projection;

    // Park the free-fly camera far away: if an unresolved handle wrongly fell
    // back to the free-fly source, the view would jump to it.
    view.free_fly_camera().x = -100.0f;

    // The handle could not be resolved this frame (mid-rebuild). The previous
    // view must be held -- a transient invalid handle cannot flip the camera.
    view.update_active_view(std::nullopt, 0u, false, logger);

    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 4.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.y, 5.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.z, 6.0f);
    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(view.active_view().view_projection.m[i], resolved_vp.m[i]);
    }

    // ...and the source is still Scene, so a re-resolve picks straight back up.
    EXPECT_TRUE(view.scene_source_active());

    const Mat4 back = wz::math::translation(Vec3{ 8.0f, 9.0f, 10.0f });
    view.update_active_view(back, 1u, true, logger);
    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 8.0f);
}

TEST(ViewController, SceneViewSurvivesDriftedNonOrthonormalWorldMatrix)
{
    wz::Logger logger;

    // The view a clean, orthonormal, unit-scale matrix at this pose produces.
    Mat4 clean_vp{};
    {
        ViewController clean;
        clean.set_prefer_scene_camera(true);
        clean.select_scene_camera("cam_node", 7u, camera_params(), logger);
        clean.update_active_view(
            wz::math::translation(Vec3{ 4.0f, 5.0f, 6.0f }), 1u, true, logger);
        clean_vp = clean.active_view().view_projection;
    }

    ViewController view;
    view.set_prefer_scene_camera(true);
    view.select_scene_camera("cam_node", 7u, camera_params(), logger);

    // The same rigid pose, but parent-scaled with a slightly drifted basis --
    // the tank's 0.5 scale plus the FP drift from per-frame terrain alignment.
    // decompose_trs REJECTS this (its dot(rx,ry) orthogonality gate is 1e-6, and
    // the drift is ~2e-4) and leaves an identity pose, snapping the camera to
    // the origin -- the intermittent #219 flip. rigid_pose_from_matrix
    // normalizes the basis instead, so the view must match the clean one.
    Mat4 world = wz::math::translation(Vec3{ 4.0f, 5.0f, 6.0f });
    world.m[0]  = 0.5f;
    world.m[5]  = 0.5f;
    world.m[10] = 0.5f;
    world.m[1]  = 0.0001f;

    view.update_active_view(world, 1u, true, logger);

    // world_position is read straight off the matrix, so it survives either way
    // -- it is the view_projection that a rejected decompose corrupts.
    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 4.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.y, 5.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.z, 6.0f);

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(view.active_view().view_projection.m[i], clean_vp.m[i], 1e-3f)
            << "view_projection element " << i
            << " diverged from the clean-matrix view -- the drifted basis was "
               "rejected rather than normalized";
    }
}

TEST(ViewController, FreeFlySourceIgnoresAResolvedSceneMatrix)
{
    ViewController view;   // editor: anchor recorded, source stays FreeFly
    wz::Logger logger;
    view.select_scene_camera("cam_node", 7u, camera_params(), logger);
    ASSERT_FALSE(view.scene_source_active());

    view.free_fly_camera().x = 1.0f;

    const Mat4 world = wz::math::translation(Vec3{ 4.0f, 5.0f, 6.0f });
    view.update_active_view(world, 1u, true, logger);

    // The scene matrix is ignored while the free-fly source is active.
    EXPECT_FLOAT_EQ(view.active_view().world_position.x, 1.0f);
    EXPECT_FLOAT_EQ(view.active_view().world_position.y, 0.0f);
}

// --- aspect -----------------------------------------------------------------

TEST(ViewController, AspectRoundTripsAndReachesTheProjection)
{
    ViewController view;
    wz::Logger logger;

    view.set_aspect(2.0f);
    EXPECT_FLOAT_EQ(view.aspect(), 2.0f);

    materialize(view, logger);
    const Mat4 wide = view.active_view().view_projection;

    view.set_aspect(1.0f);
    materialize(view, logger);
    const Mat4 square = view.active_view().view_projection;

    bool differs = false;
    for (int i = 0; i < 16; ++i) {
        differs = differs || wide.m[i] != square.m[i];
    }
    EXPECT_TRUE(differs);
}
