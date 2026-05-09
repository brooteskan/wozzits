// src/engine/runtime_camera.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>

#include <engine/runtime_camera.h>

namespace wz::app
{
    void update_camera(RuntimeCamera& cam, const wz::input::InputState& input, float dt)
    {
        const float move = cam.move_speed * dt;

        if (input.keyboard.down['W']) cam.y += move;
        if (input.keyboard.down['S']) cam.y -= move;
        if (input.keyboard.down['A']) cam.x -= move;
        if (input.keyboard.down['D']) cam.x += move;

        if (input.keyboard.down[VK_UP])    cam.pitch += move;
        if (input.keyboard.down[VK_DOWN])  cam.pitch -= move;
        if (input.keyboard.down[VK_LEFT])  cam.yaw   -= move;
        if (input.keyboard.down[VK_RIGHT]) cam.yaw   += move;

        cam.yaw   += static_cast<float>(input.mouse.dx) * cam.look_speed;
        cam.pitch += static_cast<float>(input.mouse.dy) * cam.look_speed;
        cam.pitch  = std::clamp(cam.pitch, -1.5f, 1.5f);
    }
}
