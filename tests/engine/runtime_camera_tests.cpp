// tests/engine/runtime_camera_tests.cpp
#include <gtest/gtest.h>
#include <engine/runtime_camera.h>

// Virtual key codes (Windows) — numeric to avoid pulling in <windows.h>
constexpr int KEY_W      = 0x57;
constexpr int KEY_S      = 0x53;
constexpr int KEY_A      = 0x41;
constexpr int KEY_D      = 0x44;
constexpr int KEY_UP     = 0x26;
constexpr int KEY_DOWN   = 0x28;
constexpr int KEY_LEFT   = 0x25;
constexpr int KEY_RIGHT  = 0x27;

namespace
{
    wz::input::InputState make_input()
    {
        return wz::input::InputState{};
    }
}

// ── WASD translation ──────────────────────────────────────────────────────────

TEST(RuntimeCameraTest, WMovesForward)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_W] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_FLOAT_EQ(cam.y, cam.move_speed);
}

TEST(RuntimeCameraTest, SMovesBackward)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_S] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_FLOAT_EQ(cam.y, -cam.move_speed);
}

TEST(RuntimeCameraTest, AMovesLeft)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_A] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_FLOAT_EQ(cam.x, -cam.move_speed);
}

TEST(RuntimeCameraTest, DMovesRight)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_D] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_FLOAT_EQ(cam.x, cam.move_speed);
}

// ── Arrow key look ────────────────────────────────────────────────────────────

TEST(RuntimeCameraTest, UpArrowIncreasesPitch)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_UP] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_GT(cam.pitch, 0.0f);
}

TEST(RuntimeCameraTest, DownArrowDecreasesPitch)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_DOWN] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_LT(cam.pitch, 0.0f);
}

TEST(RuntimeCameraTest, LeftArrowDecreasesYaw)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_LEFT] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_LT(cam.yaw, 0.0f);
}

TEST(RuntimeCameraTest, RightArrowIncreasesYaw)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.keyboard.down[KEY_RIGHT] = true;

    update_camera(cam, input, 1.0f);

    EXPECT_GT(cam.yaw, 0.0f);
}

// ── Mouse look ────────────────────────────────────────────────────────────────

TEST(RuntimeCameraTest, MouseDxRotatesYaw)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.mouse.dx = 100;

    update_camera(cam, input, 0.0f);

    EXPECT_FLOAT_EQ(cam.yaw, 100 * cam.look_speed);
}

TEST(RuntimeCameraTest, MouseDyRotatesPitch)
{
    wz::app::RuntimeCamera cam{};
    auto input = make_input();
    input.mouse.dy = 100;

    update_camera(cam, input, 0.0f);

    EXPECT_FLOAT_EQ(cam.pitch, 100 * cam.look_speed);
}

// ── Pitch clamp ───────────────────────────────────────────────────────────────

TEST(RuntimeCameraTest, PitchClampsAtUpperLimit)
{
    wz::app::RuntimeCamera cam{};
    cam.pitch = 1.4f;
    auto input = make_input();
    input.keyboard.down[KEY_UP] = true;

    update_camera(cam, input, 10.0f); // large dt to push well past the limit

    EXPECT_FLOAT_EQ(cam.pitch, 1.5f);
}

TEST(RuntimeCameraTest, PitchClampsAtLowerLimit)
{
    wz::app::RuntimeCamera cam{};
    cam.pitch = -1.4f;
    auto input = make_input();
    input.keyboard.down[KEY_DOWN] = true;

    update_camera(cam, input, 10.0f);

    EXPECT_FLOAT_EQ(cam.pitch, -1.5f);
}

// ── dt scaling ────────────────────────────────────────────────────────────────

TEST(RuntimeCameraTest, MovementScalesWithDt)
{
    wz::app::RuntimeCamera cam_half{};
    wz::app::RuntimeCamera cam_full{};
    auto input = make_input();
    input.keyboard.down[KEY_W] = true;

    update_camera(cam_half, input, 0.5f);
    update_camera(cam_full, input, 1.0f);

    EXPECT_FLOAT_EQ(cam_full.y, cam_half.y * 2.0f);
}

TEST(RuntimeCameraTest, NoInputLeavesStateUnchanged)
{
    wz::app::RuntimeCamera cam{};
    cam.x = 3.0f;
    cam.y = -1.0f;
    cam.yaw = 0.5f;
    cam.pitch = 0.2f;

    auto input = make_input();
    update_camera(cam, input, 1.0f);

    EXPECT_FLOAT_EQ(cam.x, 3.0f);
    EXPECT_FLOAT_EQ(cam.y, -1.0f);
    EXPECT_FLOAT_EQ(cam.yaw, 0.5f);
    EXPECT_FLOAT_EQ(cam.pitch, 0.2f);
}
