#pragma once
// engine/runtime_camera.h

#include <input/input.h>

namespace wz::app
{
    struct RuntimeCamera
    {
        float x = 0.0f;
        float y = 0.0f;

        float yaw = 0.0f;
        float pitch = 0.0f;

        float move_speed = 2.0f;
        float look_speed = 0.0025f;
    };

    void update_camera(RuntimeCamera& cam, const wz::input::InputState& input, float dt);
}
