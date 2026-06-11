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
