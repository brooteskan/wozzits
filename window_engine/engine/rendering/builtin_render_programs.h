#pragma once

// engine/rendering/builtin_render_programs.h
//
// Maps BuiltinRenderProgram values to the shader asset recipes used by the
// existing shader asset system.
//
// This does not compile shaders directly and is not a new asset type. It only
// centralizes the default shader-pair description for built-in render programs.

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/shader_asset_module.h>

namespace wz::engine::rendering
{
    [[nodiscard]] bool get_builtin_shader_pair_desc(
        wz::engine::assets::BuiltinRenderProgram program,
        wz::engine::assets::ShaderPairDesc& out);
}