#include <engine/assets/render_program/render_program_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

// include whatever header owns compile_failed_node(...)
#include <engine/assets/engine_asset_library_internal.h>

#include <any>
#include <span>

namespace wz::engine::assets::internal
{
    namespace
    {
        bool fill_builtin_render_program_defaults(
            BuiltinRenderProgram program,
            RenderProgramData& out)
        {
            out.builtin_program = program;

            switch (program)
            {
            case BuiltinRenderProgram::MeshWireframeDebug:
                out.binding_model     = RenderBindingModel::MeshIA;
                out.topology          = RenderPrimitiveTopology::TriangleList;
                out.default_domain    = RenderDomain::Debug;
                out.default_policy_flags =
                    RenderPolicy_Wireframe |
                    RenderPolicy_DepthTest |
                    RenderPolicy_DepthWrite;
                // Declarative pipeline state.
                out.input_layout = InputLayoutKind::MeshPositionOnly;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::WireframeCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 32,  // world[16] + view_proj[16]
                }};
                return true;

            case BuiltinRenderProgram::GaussianSplatDebug:
                // Current path — vertex-instanced, not SplatPull.
                out.binding_model  = RenderBindingModel::SplatVertexInstanced;
                out.topology       = RenderPrimitiveTopology::TriangleStrip;
                out.default_domain = RenderDomain::Splat;
                out.default_policy_flags =
                    RenderPolicy_AlphaBlend |
                    RenderPolicy_DepthTest;
                // Declarative pipeline state.
                out.input_layout = InputLayoutKind::GaussianSplatVertex;
                out.blend_mode   = BlendMode::AlphaBlend;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 36,  // world[16] + view_proj[16] + viewport_and_size[4]
                }};
                return true;

            case BuiltinRenderProgram::ScalarFieldDebug:
                out.binding_model    = RenderBindingModel::ScalarFieldTexture;
                out.topology         = RenderPrimitiveTopology::TriangleList;
                out.default_domain   = RenderDomain::Debug;
                out.default_policy_flags = RenderPolicy_None;
                // Declarative pipeline state.
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullBack;
                return true;

            case BuiltinRenderProgram::Count:
                return false;
            }

            return false;
        }
    }

    void register_render_program_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderProgramTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kBuiltinRenderProgramSchema,
            .output_type = kAssetTypeRenderProgram,
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<BuiltinRenderProgramDesc>(&input.meta);

                if (!desc) {
                    logger.error("render program missing BuiltinRenderProgramDesc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 2) {
                    logger.error("render program requires exactly two shader dependencies (vertex, pixel)");
                    return compile_failed_node(input);
                }

                if (!dep_handles[0].valid() || !dep_handles[1].valid()) {
                    logger.error("render program shader dependencies did not resolve");
                    return compile_failed_node(input);
                }

                RenderProgramData data{};
                if (!fill_builtin_render_program_defaults(desc->program, data)) {
                    logger.error("render program has invalid builtin program");
                    return compile_failed_node(input);
                }

                data.vertex_shader = dep_handles[0];
                data.pixel_shader  = dep_handles[1];

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store render program");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}