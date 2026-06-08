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
        case BuiltinRenderProgram::MeshWireframeDepthDebug:
        case BuiltinRenderProgram::MeshDepthPrepassDebug:
        case BuiltinRenderProgram::MeshWireframeAlpha:
            out = ShaderPairDesc{
                .name =
                    program == BuiltinRenderProgram::MeshDepthPrepassDebug
                        ? "mesh_depth_prepass_debug"
                        : program == BuiltinRenderProgram::MeshWireframeAlpha
                            ? "mesh_wireframe_alpha"
                            : program == BuiltinRenderProgram::MeshWireframeDepthDebug
                                ? "mesh_wireframe_depth_debug"
                                : "mesh_wireframe_debug",
                .vertex_path = "shaders/mesh_wireframe/mesh_wireframe_vs.hlsl",
                .pixel_path = "shaders/mesh_wireframe/mesh_wireframe_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::MeshSurface:
        case BuiltinRenderProgram::MeshSurfaceAlpha:
            out = ShaderPairDesc{
                .name =
                    program == BuiltinRenderProgram::MeshSurfaceAlpha
                        ? "mesh_surface_alpha"
                        : "mesh_surface",
                .vertex_path = "shaders/mesh_surface/mesh_surface_vs.hlsl",
                .pixel_path = "shaders/mesh_surface/mesh_surface_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::MeshFieldHeatmap:
            out = ShaderPairDesc{
                .name = "mesh_field_heatmap",
                .vertex_path = "shaders/mesh_field_heatmap/mesh_field_heatmap_vs.hlsl",
                .pixel_path = "shaders/mesh_field_heatmap/mesh_field_heatmap_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::TerrainMeshSurface:
            out = ShaderPairDesc{
                .name = "terrain_mesh_surface",
                .vertex_path = "shaders/terrain_mesh_surface/terrain_mesh_surface_vs.hlsl",
                .pixel_path = "shaders/terrain_mesh_surface/terrain_mesh_surface_ps.hlsl",
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

        case BuiltinRenderProgram::TerrainSurfelSurface:
            out = ShaderPairDesc{
                .name = "terrain_surfel_surface",
                .vertex_path = "shaders/gaussian_splat/terrain_surfel_surface_vs.hlsl",
                .pixel_path = "shaders/gaussian_splat/terrain_surfel_surface_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::ScalarFieldDebug:
            out = ShaderPairDesc{
                .name = "scalar_field_debug",
                .vertex_path = "shaders/scalar_field/scalar_field_vs.hlsl",
                .pixel_path = "shaders/scalar_field/scalar_field_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::GaussianSplatPullDebug:
            out = ShaderPairDesc{
                .name = "gaussian_splat_pull_debug",
                .vertex_path = "shaders/gaussian_splat/gaussian_splat_pull_debug_vs.hlsl",
                .pixel_path  = "shaders/gaussian_splat/gaussian_splat_debug_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry  = "main",
                .vertex_target = "vs_5_0",
                .pixel_target  = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::GaussianSplatNeighborhoodColorBlend:
            out = ShaderPairDesc{
                .name = "gaussian_splat_neighborhood_color_blend",
                .vertex_path = "shaders/gaussian_splat/gaussian_splat_neighborhood_color_blend_vs.hlsl",
                .pixel_path  = "shaders/gaussian_splat/gaussian_splat_debug_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry  = "main",
                .vertex_target = "vs_5_0",
                .pixel_target  = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::GaussianSplatTerrainCoverageDebug:
            out = ShaderPairDesc{
                .name = "gaussian_splat_terrain_coverage_debug",
                .vertex_path = "shaders/gaussian_splat/gaussian_splat_terrain_coverage_vs.hlsl",
                .pixel_path  = "shaders/gaussian_splat/gaussian_splat_terrain_coverage_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry  = "main",
                .vertex_target = "vs_5_0",
                .pixel_target  = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::SkySurface:
            out = ShaderPairDesc{
                .name = "sky_surface",
                .vertex_path = "shaders/sky_surface/sky_surface_vs.hlsl",
                .pixel_path = "shaders/sky_surface/sky_surface_ps.hlsl",
                .vertex_entry = "main",
                .pixel_entry = "main",
                .vertex_target = "vs_5_0",
                .pixel_target = "ps_5_0",
            };
            return true;

        case BuiltinRenderProgram::Count:
            break;
        }

        out = {};
        return false;
    }
}
