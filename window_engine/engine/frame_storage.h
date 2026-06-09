#pragma once
// engine/frame_storage.h

#include <engine/behavior/behavior_commands.h>
#include <engine/behavior/behavior_gpu_compute.h>
#include <engine/collision/collision_frame.h>
#include <engine/input_events.h>

#include <scene/compile/scene_compiler.h>
#include <render/frame/render_frame.h>
#include <render/ir/render_ir.h>

namespace wz::engine
{
    // Owns CPU-side per-frame products. Allocated once and overwritten each frame.
    struct FrameStorage
    {
        wz::scene::ViewData             view{};
        wz::scene::CompiledSceneStorage compiled_scene{};
        wz::engine::collision::CollisionFrameStorage collision{};
        wz::engine::input_events::InputEventStorage input_events{};
        wz::engine::behavior::BehaviorCommandBuffer behavior_commands{};
        wz::engine::behavior::BehaviorGpuComputeBuffer behavior_gpu_compute{};
        wz::render::RenderIRStorage     render_ir{};
        wz::render::RenderFrameStorage  render_frame{};
    };
}
