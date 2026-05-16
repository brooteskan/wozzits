// src/engine/rendering/builtin_render_programs.cpp

#include <engine/rendering/builtin_render_programs.h>

namespace wz::engine::rendering
{
    bool get_builtin_shader_pair_desc(
        wz::engine::assets::BuiltinRenderProgram program,
        wz::engine::assets::ShaderPairDesc& out)
    {
        using wz::engine::assets::BuiltinRenderProgram;
        using wz::engine::assets::ShaderPairDesc;

        switch (program)
        {
        case BuiltinRenderProgram::MeshWireframeDebug:
            out = ShaderPairDesc{
                .name = "mesh_wireframe_debug",
                .vertex_path = "shaders/mesh_wireframe/mesh_wireframe_vs.hlsl",
                .pixel_path = "shaders/mesh_wireframe/mesh_wireframe_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::GaussianSplatDebug:
            out = ShaderPairDesc{
                .name = "gaussian_splat_debug",
                .vertex_path = "shaders/gaussian_splat/gaussian_splat_debug_vs.hlsl",
                .pixel_path = "shaders/gaussian_splat/gaussian_splat_debug_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;
        }

        out = {};
        return false;
    }
}