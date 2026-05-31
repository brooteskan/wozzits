#pragma once
// engine/frame_storage.h

#include <engine/frame_types.h>

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
        wz::render::RenderIRStorage     render_ir{};
        wz::render::RenderFrameStorage  render_frame{};

        RenderPrepPath render_prep_path = RenderPrepPath::FullCompile;
    };
}
