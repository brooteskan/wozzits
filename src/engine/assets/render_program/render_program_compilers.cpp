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
                out.default_policy_flags = RenderPolicy_Wireframe;
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

            case BuiltinRenderProgram::MeshWireframeDepthDebug:
                out.binding_model     = RenderBindingModel::MeshIA;
                out.topology          = RenderPrimitiveTopology::TriangleList;
                out.default_domain    = RenderDomain::Debug;
                out.default_policy_flags =
                    RenderPolicy_Wireframe
                    | RenderPolicy_DepthTest
                    | RenderPolicy_DepthWrite;
                out.input_layout = InputLayoutKind::MeshPositionOnly;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestNoWrite;
                out.raster_mode  = RasterMode::WireframeCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                return true;

            case BuiltinRenderProgram::MeshWireframeAlpha:
                out.binding_model     = RenderBindingModel::MeshIA;
                out.topology          = RenderPrimitiveTopology::TriangleList;
                out.default_domain    = RenderDomain::Transparent;
                out.default_policy_flags =
                    RenderPolicy_Wireframe
                    | RenderPolicy_AlphaBlend
                    | RenderPolicy_DepthTest;
                out.input_layout = InputLayoutKind::MeshPositionOnly;
                out.blend_mode   = BlendMode::AlphaBlend;
                out.depth_mode   = DepthMode::TestNoWrite;
                out.raster_mode  = RasterMode::WireframeCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                return true;

            case BuiltinRenderProgram::MeshDepthPrepassDebug:
                out.binding_model     = RenderBindingModel::MeshIA;
                out.topology          = RenderPrimitiveTopology::TriangleList;
                out.default_domain    = RenderDomain::Debug;
                out.default_policy_flags = RenderPolicy_DepthTest | RenderPolicy_DepthWrite;
                out.input_layout = InputLayoutKind::MeshPositionOnly;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestWrite;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                return true;

            case BuiltinRenderProgram::MeshSurface:
                out.binding_model        = RenderBindingModel::MeshIA;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Opaque;
                out.default_policy_flags =
                    RenderPolicy_DepthTest
                    | RenderPolicy_DepthWrite;
                out.input_layout = InputLayoutKind::MeshPositionNormalUV;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestWrite;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                return true;

            case BuiltinRenderProgram::MeshSurfaceAlpha:
                out.binding_model        = RenderBindingModel::MeshIA;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Transparent;
                out.default_policy_flags =
                    RenderPolicy_AlphaBlend
                    | RenderPolicy_DepthTest;
                out.input_layout = InputLayoutKind::MeshPositionNormalUV;
                out.blend_mode   = BlendMode::AlphaBlend;
                out.depth_mode   = DepthMode::TestNoWrite;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                return true;

            case BuiltinRenderProgram::MeshFieldHeatmap:
                out.binding_model        = RenderBindingModel::MeshIA;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Opaque;
                out.default_policy_flags =
                    RenderPolicy_DepthTest
                    | RenderPolicy_DepthWrite;
                out.input_layout = InputLayoutKind::MeshPositionNormalUV;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestWrite;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 40,
                }};
                out.descriptor_bindings = {{
                    .kind             = DescriptorKind::StructuredBufferSRV,
                    .visibility       = ShaderVisibility::Vertex,
                    .semantic         =
                        DescriptorSemantic::MeshFieldVisualization,
                    .shader_register  = 0,
                    .register_space   = 0,
                    .descriptor_count = 1,
                }};
                return true;

            case BuiltinRenderProgram::TerrainMeshSurface:
                out.binding_model        = RenderBindingModel::MeshIA;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Opaque;
                out.default_policy_flags =
                    RenderPolicy_DepthTest
                    | RenderPolicy_DepthWrite;
                out.input_layout = InputLayoutKind::MeshPositionNormalUV;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestWrite;
                out.raster_mode  = RasterMode::SolidCullBack;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::Vertex,
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
                out.binding_model        = RenderBindingModel::ScalarFieldTexture;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Debug;
                out.default_policy_flags = RenderPolicy_None;
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullBack;
                return true;

            case BuiltinRenderProgram::GaussianSplatPullDebug:
                // Pull-based splat path: no IA vertex buffer; splat data arrives
                // via a StructuredBuffer SRV bound at t0.
                out.binding_model        = RenderBindingModel::SplatPull;
                out.topology             = RenderPrimitiveTopology::TriangleStrip;
                out.default_domain       = RenderDomain::Splat;
                out.default_policy_flags = RenderPolicy_AlphaBlend;
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::AlphaBlend;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space  = 0,
                    .value_count     = 36,  // world[16] + view_proj[16] + viewport_and_size[4]
                }};
                out.descriptor_bindings = {
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SplatCloud,
                        .shader_register  = 0,  // t0
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SortedSplatIndices,
                        .shader_register  = 1,  // t1
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                };
                return true;

            case BuiltinRenderProgram::GaussianSplatNeighborhoodColorBlend:
                // Same binding model as PullDebug, but with extended root
                // constants for the LOD blend params (mode, strength, near/far,
                // stride ratio + max, use_confidence, pad).
                out.binding_model        = RenderBindingModel::SplatPull;
                out.topology             = RenderPrimitiveTopology::TriangleStrip;
                out.default_domain       = RenderDomain::Splat;
                out.default_policy_flags = RenderPolicy_AlphaBlend;
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::AlphaBlend;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space  = 0,
                    // world[16] + view_proj[16] + viewport_and_size[4]
                    // + lod_params0[4] + lod_params1[4] + lod_pad[4] = 48
                    .value_count     = 48,
                }};
                out.descriptor_bindings = {
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SplatCloud,
                        .shader_register  = 0,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SortedSplatIndices,
                        .shader_register  = 1,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                };
                return true;

            case BuiltinRenderProgram::GaussianSplatTerrainCoverageDebug:
                // Same SplatPull binding as the other splat programs but
                // Opaque blend + TestWrite depth so coverage modes 1/2/3
                // can write a depth signal that participates in occlusion
                // of subsequent draws.  Mode 0 (TransparentBlend) is still
                // selectable but the lack of alpha blending under this
                // state means it shows visible halos — kept for debug
                // comparison only.
                out.binding_model        = RenderBindingModel::SplatPull;
                out.topology             = RenderPrimitiveTopology::TriangleStrip;
                out.default_domain       = RenderDomain::Splat;
                out.default_policy_flags = RenderPolicy_None;
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::TestWrite;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    // world[16] + view_proj[16] + viewport_and_size[4]
                    // + reserved[12] (matches NeighborhoodColorBlend layout)
                    // + coverage_params0[4] + coverage_params1[4]
                    // + coverage_params2[4] = 60 dwords total
                    .value_count     = 60,
                }};
                out.descriptor_bindings = {
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SplatCloud,
                        .shader_register  = 0,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::Vertex,
                        .semantic         = DescriptorSemantic::SortedSplatIndices,
                        .shader_register  = 1,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                };
                return true;

            case BuiltinRenderProgram::SkySurface:
                // A sky surface is an encompassing, camera-relative projection
                // lane. Sphere is the first authored projection, but the render
                // program is deliberately fullscreen/no-IA so later projections
                // can be evaluated from view direction instead of being tied to
                // a particular mesh.
                out.binding_model        = RenderBindingModel::Fullscreen;
                out.topology             = RenderPrimitiveTopology::TriangleList;
                out.default_domain       = RenderDomain::Sky;
                out.default_policy_flags = RenderPolicy_None;
                out.input_layout = InputLayoutKind::None;
                out.blend_mode   = BlendMode::Opaque;
                out.depth_mode   = DepthMode::Disabled;
                out.raster_mode  = RasterMode::SolidCullNone;
                out.root_constants = {{
                    .visibility      = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space  = 0,
                    // Packed as seven float4 groups: solid color/exposure,
                    // gradient top/bottom, mode/rotation, rotation/right, up,
                    // and forward.
                    .value_count     = 28,
                }};
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
