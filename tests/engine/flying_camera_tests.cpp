#include <bench/flying_camera.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    bool near(float a, float b, float eps = 0.0001f)
    {
        return std::fabs(a - b) <= eps;
    }
}

TEST(FlyingCamera, MovementUsesOrientationAfterMouseLook)
{
    wz::bench::FlyingCamera camera{};
    camera.move_speed = 1.0f;
    camera.look_speed = 0.001f;

    wz::input::InputState input{};
    input.mouse.dx = 1571;
    input.keyboard.down['W'] = true;

    wz::bench::update_flying_camera(camera, input, 0.001f);

    const wz::bench::CameraBasis basis = wz::bench::camera_basis(camera);

    EXPECT_NEAR(camera.x, basis.forward.x * 0.001f, 0.00001f);
    EXPECT_NEAR(camera.y, basis.forward.y * 0.001f, 0.00001f);
    EXPECT_NEAR(camera.z, basis.forward.z * 0.001f, 0.00001f);
    EXPECT_FALSE(near(camera.z, 0.001f));
}

TEST(FlyingCamera, CombinedYawPitchUsesPostYawRightAxis)
{
    wz::bench::FlyingCamera combined{};
    combined.look_speed = 0.001f;

    wz::input::InputState combined_input{};
    combined_input.mouse.dx = 300;
    combined_input.mouse.dy = 200;
    wz::bench::update_flying_camera(combined, combined_input, 0.0f);

    wz::bench::FlyingCamera sequential{};
    sequential.look_speed = 0.001f;

    wz::input::InputState yaw_input{};
    yaw_input.mouse.dx = 300;
    wz::bench::update_flying_camera(sequential, yaw_input, 0.0f);

    wz::input::InputState pitch_input{};
    pitch_input.mouse.dy = 200;
    wz::bench::update_flying_camera(sequential, pitch_input, 0.0f);

    EXPECT_NEAR(combined.orientation.x, sequential.orientation.x, 0.00001f);
    EXPECT_NEAR(combined.orientation.y, sequential.orientation.y, 0.00001f);
    EXPECT_NEAR(combined.orientation.z, sequential.orientation.z, 0.00001f);
    EXPECT_NEAR(combined.orientation.w, sequential.orientation.w, 0.00001f);
}

// ─────────────────────────────────────────────────────────────────────────
// Adversarial pitch / yaw-direction tests.
//
// These pin the reported defect: after the view is pitched far enough to
// cross straight-up / straight-down, the camera flips upside down and a given
// mouse-look direction starts panning the *opposite* way on screen.
//
// Root cause: yaw is applied about WORLD up (intentional — see
// update_flying_camera) while pitch about the local right axis is UNBOUNDED.
// Once accumulated pitch passes ±90° the camera's up vector drops below the
// horizon (up.y < 0); because the screen-relative yaw sign equals sign(up.y),
// mouse-right then pans screen-left.
//
// The fix is to clamp pitch so the view can never cross vertical (the camera
// must never go upside down from mouse-look alone).  Until that clamp lands,
// every test below except the upright control is expected to FAIL — that is
// the point: they characterise the bug.
// ─────────────────────────────────────────────────────────────────────────

namespace
{
    using wz::bench::CameraBasis;
    using wz::bench::FlyingCamera;
    using wz::bench::camera_basis;
    using wz::bench::update_flying_camera;
    using wz::input::InputState;

    float dot3(const wz::math::Vec3& a, const wz::math::Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Apply one horizontal mouse delta to a *copy* of cam and report which way
    // the view panned relative to the screen:
    //   +1 -> panned toward the camera's own right (screen-right)
    //   -1 -> panned screen-left
    //    0 -> no measurable horizontal pan
    //
    // Measured as the projection of the change in the forward axis onto the
    // pre-move right axis, so it reflects what the user feels regardless of the
    // current world heading.
    int yaw_pan_sign(FlyingCamera cam, int mouse_dx)
    {
        const CameraBasis before = camera_basis(cam);

        InputState input{};
        input.mouse.dx = mouse_dx;
        update_flying_camera(cam, input, 0.0f);

        const CameraBasis after = camera_basis(cam);
        const wz::math::Vec3 delta_forward{
            after.forward.x - before.forward.x,
            after.forward.y - before.forward.y,
            after.forward.z - before.forward.z,
        };

        const float p = dot3(delta_forward, before.right);
        if (p >  1e-6f) return  1;
        if (p < -1e-6f) return -1;
        return 0;
    }

    // Feed `frames` identical mouse-look frames through the camera.
    void drive_mouse_look(FlyingCamera& cam, int dx, int dy, int frames)
    {
        for (int i = 0; i < frames; ++i) {
            InputState input{};
            input.mouse.dx = dx;
            input.mouse.dy = dy;
            update_flying_camera(cam, input, 0.0f);
        }
    }
}

// Control: establishes "the way it starts out".  This one passes today.
TEST(FlyingCameraAdversarial, YawPansScreenRightWhenUpright)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    EXPECT_EQ(yaw_pan_sign(cam, +100),  1) << "mouse-right should pan view right";
    EXPECT_EQ(yaw_pan_sign(cam, -100), -1) << "mouse-left should pan view left";
}

// Core defect: a few large downward steps carry the view past vertical, and
// the very same mouse-right input now pans the other way.
TEST(FlyingCameraAdversarial, YawDirectionStableAfterPitchingPastVertical)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    const int baseline = yaw_pan_sign(cam, +100);
    ASSERT_EQ(baseline, 1);

    // Pitch straight down past vertical: 20 frames * 0.1 rad ≈ 2.0 rad > pi/2.
    drive_mouse_look(cam, /*dx*/ 0, /*dy*/ 100, /*frames*/ 20);
    ASSERT_GE(camera_basis(cam).up.y, -1e-4f)
        << "the camera flipped past vertical instead of clamping";

    EXPECT_EQ(yaw_pan_sign(cam, +100), baseline)
        << "mouse-right reversed to screen-left after pitching past vertical";
}

// Same defect reached the way a user actually hits it: a long, smooth downward
// drag in small per-frame steps rather than a few big jumps.
TEST(FlyingCameraAdversarial, YawDirectionStableUnderFineGrainedPitch)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    const int baseline = yaw_pan_sign(cam, +50);
    ASSERT_EQ(baseline, 1);

    drive_mouse_look(cam, /*dx*/ 0, /*dy*/ 10, /*frames*/ 250); // ≈ 2.5 rad

    EXPECT_EQ(yaw_pan_sign(cam, +50), baseline)
        << "yaw direction inverted after a smooth downward drag past vertical";
}

// The camera must never go upside down from looking down alone.
TEST(FlyingCameraAdversarial, CameraNeverFlipsLookingDown)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    for (int i = 0; i < 40; ++i) {
        InputState input{};
        input.mouse.dy = 100; // look down
        update_flying_camera(cam, input, 0.0f);

        EXPECT_GE(camera_basis(cam).up.y, -1e-4f)
            << "up vector dropped below the horizon at frame " << i
            << " (downward pitch crossed vertical and should be clamped)";
    }
}

// Symmetric guard for looking up.
TEST(FlyingCameraAdversarial, CameraNeverFlipsLookingUp)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    for (int i = 0; i < 40; ++i) {
        InputState input{};
        input.mouse.dy = -100; // look up
        update_flying_camera(cam, input, 0.0f);

        EXPECT_GE(camera_basis(cam).up.y, -1e-4f)
            << "up vector dropped below the horizon at frame " << i
            << " (upward pitch crossed vertical and should be clamped)";
    }
}

// While the mouse is held down, the view should approach straight-down and
// stop — forward.y must not turn back upward (which would mean it wrapped over
// the top).
TEST(FlyingCameraAdversarial, ContinuousDownPitchDoesNotWrapOverTheTop)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    float prev_fy = camera_basis(cam).forward.y; // 0 while looking at horizon
    for (int i = 0; i < 40; ++i) {
        InputState input{};
        input.mouse.dy = 100;
        update_flying_camera(cam, input, 0.0f);

        const float fy = camera_basis(cam).forward.y;
        EXPECT_LE(fy, prev_fy + 1e-4f)
            << "forward.y rose at frame " << i
            << " while the mouse was still pushed down — the view wrapped past "
               "straight-down instead of stopping at it";
        prev_fy = fy;
    }
}

// A diagonal drag (yaw + pitch every frame) must not flip the view either.
TEST(FlyingCameraAdversarial, DiagonalDragDoesNotFlipCamera)
{
    FlyingCamera cam{};
    cam.look_speed = 0.001f;

    for (int i = 0; i < 40; ++i) {
        InputState input{};
        input.mouse.dx = 60;
        input.mouse.dy = 100;
        update_flying_camera(cam, input, 0.0f);

        EXPECT_GE(camera_basis(cam).up.y, -1e-4f)
            << "diagonal drag flipped the camera upside down at frame " << i;
    }
}
