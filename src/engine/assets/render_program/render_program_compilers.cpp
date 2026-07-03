#include <engine/assets/render_program/render_program_compilers.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

// include whatever header owns compile_failed_node(...)
#include <engine/assets/engine_asset_library_internal.h>

#include <engine/rendering/rhi_render_program_bridge.h>

#include <wozzits/rhi/shader_module.h>

#include <array>
#include <any>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, kBuiltinRenderProgramCount>
            kBuiltinProgramOptions = {
            "Mesh wireframe debug",
            "Mesh wireframe depth debug",
            "Mesh depth prepass debug",
            "Mesh wireframe alpha",
            "Mesh surface",
            "Mesh surface alpha",
            "Mesh field heatmap",
            "Mesh mask style",
            "Terrain mesh surface",
            "Gaussian splat debug",
            "Terrain surfel surface",
            "Scalar field debug",
            "Gaussian splat pull debug",
            "Gaussian splat neighborhood color blend",
            "Gaussian splat terrain coverage debug",
            "Sky surface",
        };
        static_assert(
            kBuiltinProgramOptions.size() == kBuiltinRenderProgramCount);

        constexpr std::array<std::string_view, 7> kBindingModelOptions = {
            "Mesh IA",
            "Splat vertex instanced",
            "Splat pull",
            "Mesh vertex pull",
            "Scalar field texture",
            "Fullscreen",
            "Particle pull",
        };

        constexpr std::array<std::string_view, 2> kTopologyOptions = {
            "Triangle list",
            "Triangle strip",
        };

        constexpr std::array<std::string_view, 5> kRenderDomainOptions = {
            "Debug",
            "Sky",
            "Opaque",
            "Transparent",
            "Splat",
        };

        constexpr std::array<std::string_view, 4> kInputLayoutOptions = {
            "None",
            "Mesh position only",
            "Mesh position normal UV",
            "Gaussian splat vertex",
        };

        constexpr std::array<std::string_view, 2> kBlendModeOptions = {
            "Opaque",
            "Alpha blend",
        };

        constexpr std::array<std::string_view, 3> kDepthModeOptions = {
            "Disabled",
            "Test no write",
            "Test write",
        };

        constexpr std::array<std::string_view, 3> kRasterModeOptions = {
            "Solid cull back",
            "Solid cull none",
            "Wireframe cull none",
        };

        // One label per implemented numbered preset (values 0-4 below). These
        // presets remain the port-less fallback; the authored path is the
        // optional binding_layout port (issue #227).
        constexpr std::array<std::string_view, 5> kBindingLayoutOptions = {
            "Manual",
            "RHI pull mesh MVP",
            "Clipmap landscape",
            "Gaussian splat cloud",
            "Styled RHI pull mesh",
        };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        BuiltinRenderProgramDesc builtin_render_program_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            BuiltinRenderProgramDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.program =
                enum_param(
                    params,
                    "program",
                    desc.program,
                    kBuiltinProgramOptions);
            if (dep_nodes.size() > 0u) {
                desc.vertex_shader = dep_nodes[0].key;
            }
            if (dep_nodes.size() > 1u) {
                desc.pixel_shader = dep_nodes[1].key;
            }
            return desc;
        }

        CustomRenderProgramDesc custom_render_program_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            CustomRenderProgramDesc desc{};
            desc.name = params.get<std::string>("name", {});
            if (dep_nodes.size() > 0u) {
                desc.vertex_shader = dep_nodes[0].key;
            }
            if (dep_nodes.size() > 1u) {
                desc.pixel_shader = dep_nodes[1].key;
            }

            desc.binding_model =
                enum_param(
                    params,
                    "binding_model",
                    desc.binding_model,
                    kBindingModelOptions);
            desc.topology =
                enum_param(
                    params,
                    "topology",
                    desc.topology,
                    kTopologyOptions);
            desc.default_domain =
                enum_param(
                    params,
                    "default_domain",
                    desc.default_domain,
                    kRenderDomainOptions);
            desc.default_policy_flags =
                params.get<uint32_t>(
                    "default_policy_flags",
                    desc.default_policy_flags);
            desc.input_layout =
                enum_param(
                    params,
                    "input_layout",
                    desc.input_layout,
                    kInputLayoutOptions);
            desc.blend_mode =
                enum_param(
                    params,
                    "blend_mode",
                    desc.blend_mode,
                    kBlendModeOptions);
            desc.depth_mode =
                enum_param(
                    params,
                    "depth_mode",
                    desc.depth_mode,
                    kDepthModeOptions);
            desc.raster_mode =
                enum_param(
                    params,
                    "raster_mode",
                    desc.raster_mode,
                    kRasterModeOptions);

            // Range-checked like every other enum param (out-of-range authored
            // values collapse to 0 = Manual instead of silently selecting a
            // preset branch below).
            const int64_t binding_layout =
                enum_param(
                    params,
                    "binding_layout",
                    int64_t{ 0 },
                    kBindingLayoutOptions);
            if (binding_layout == 1) {
                desc.root_constants.push_back(RootConstantBinding{
                    .visibility = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space = 2,
                    .value_count = 16,
                    .semantic = "mvp",
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshPositions,
                    .shader_register = 0,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshIndices,
                    .shader_register = 1,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
            }
            else if (binding_layout == 2) {
                // Clipmap landscape (#198 slice 3b). One object-SRG (space2)
                // root-constant block holding view_projection (16 floats)
                // followed by the packed ClipmapViewTransform fields, then the
                // pulled lattice positions/indices and the resident R32 height
                // texture (#197). The constant block is sized to the exact 32
                // floats ClipmapDrawConstants packs (rhi_scene_renderer.cpp):
                //   view_projection[16] + snap_params[4]
                //   + world_to_uv[4] + texel_and_vertical[4]
                //   + texel_dims_extent[4]   (#207 per-level snap inputs).
                // Visibility All: the VS displaces with all of it, the PS reads
                // the vertical scale/base for height-band debug shading.
                desc.root_constants.push_back(RootConstantBinding{
                    .visibility = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space = 2,
                    .value_count = 32,
                    .semantic = "clipmap",
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshPositions,
                    .shader_register = 0,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshIndices,
                    .shader_register = 1,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::TextureSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::ScalarFieldTexture,
                    .shader_register = 2,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
                // Static linear-clamp sampler (s0, space2) for the VS height tap:
                // the clipmap surface is bilinearly filtered instead of point-
                // Load'd (#211), smoothing the stepped terrain between texels.
                // Baked into the root signature — no sampler heap or descriptor.
                desc.static_samplers.push_back(StaticSamplerBinding{
                    .kind = StaticSamplerKind::LinearClamp,
                    .visibility = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space = 2,
                });
            }
            else if (binding_layout == 3) {
                // Gaussian-splat cloud (issue #208). A SplatPull program slotted
                // into the SAME object SRG (space2) RhiSceneRenderer binds, so it
                // rides the existing clipmap-style realize path. One 36-float
                // root-constant block holding world[16] + view_proj[16] +
                // camera_and_diameter[4] (SplatCloudDrawConstants, packed BYTE-
                // FOR-BYTE in rhi_scene_renderer.cpp and mirrored by the SplatView
                // cbuffer in gaussian_splat_field_cloud_vs.hlsl), then the
                // resident decoded splat StructuredBuffer at the SplatCloud
                // semantic (t0). No index/position pull buffers: the draw is a
                // non-indexed DrawInstanced of 4 * splat_count vertices.
                // Vertex-only: the splat PS reads no cbuffer.
                desc.root_constants.push_back(RootConstantBinding{
                    .visibility = ShaderVisibility::Vertex,
                    .shader_register = 0,
                    .register_space = 2,
                    .value_count = 36,
                    .semantic = "splat_view",
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::SplatCloud,
                    .shader_register = 0,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
            }
            else if (binding_layout == 4) {
                // Styled RHI pull mesh (issue #195 slice A). Identical object-SRG
                // (space2) shape to binding_layout==1, except the single root-
                // constant block is 28 dwords instead of 16: the 16-float MVP the
                // VS uses, followed by the 12-float mesh-style shading the PS
                // reads (wireframe_color[4], surface_color[4], params[4] where
                // params = (wireframe_emissive, surface_emissive, alpha, mode)).
                // Packed BYTE-FOR-BYTE by MeshStyleDrawConstants in
                // rhi_scene_renderer.cpp and mirrored by the MeshStyle cbuffer in
                // mesh_style_pull_vs/ps.hlsl. Visibility All: the block is one b0
                // cbuffer bound to both stages (the VS ignores the trailing style
                // fields; the PS ignores the leading MVP). The "mesh_style"
                // semantic is registered by NAME (open vocabulary) — no enum.
                desc.root_constants.push_back(RootConstantBinding{
                    .visibility = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space = 2,
                    .value_count = 28,
                    .semantic = "mesh_style",
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshPositions,
                    .shader_register = 0,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
                desc.descriptor_bindings.push_back(DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshIndices,
                    .shader_register = 1,
                    .register_space = 2,
                    .descriptor_count = 1,
                });
            }
            return desc;
        }

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

            case BuiltinRenderProgram::MeshMaskStyle:
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
                    .value_count     = 48,
                }};
                out.descriptor_bindings = {
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::All,
                        .semantic         =
                            DescriptorSemantic::MeshFieldVisualization,
                        .shader_register  = 0,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                    {
                        .kind             = DescriptorKind::StructuredBufferSRV,
                        .visibility       = ShaderVisibility::All,
                        .semantic         = DescriptorSemantic::MeshMaskRules,
                        .shader_register  = 1,
                        .register_space   = 0,
                        .descriptor_count = 1,
                    },
                };
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

            case BuiltinRenderProgram::TerrainSurfelSurface:
                return false;

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

        // When the shared rhi registries are present, the custom-render-program
        // compiler produces the rhi program: convert the authored description to
        // a RenderProgramDesc that references its shaders by shader_ref, and
        // register it under program_ref(program_key) so the renderer binds it by
        // find. The shaders themselves are produced by the shader compiler (rhi
        // ShaderModule under shader_ref(key)); the bytes are resolved at PSO time
        // via resolve_program_bytecode — no D3DCompile here (#193). Best-effort:
        // on a miss the renderer's find-then-fallback bridges at render time.
        void publish_custom_rhi_render_program(
            const wz::asset::AssetKey& program_key,
            const CustomRenderProgramDesc& authored,
            const wz::asset::AssetKey& vertex_key,
            const wz::asset::AssetKey& pixel_key,
            wz::rhi::RenderProgramRegistry& programs,
            wz::rhi::DescriptorSemanticRegistry& descriptor_semantics,
            wz::rhi::ConstantSemanticRegistry& constant_semantics,
            wz::Logger& logger)
        {
            // Force the shader keys to the resolved deps so the desc references
            // the shader modules the shader compiler registered, then name the
            // program by its AssetKey so producer (here) and consumer (renderer)
            // agree on program_ref.
            CustomRenderProgramDesc resolved = authored;
            resolved.vertex_shader = vertex_key;
            resolved.pixel_shader = pixel_key;
            std::optional<wz::rhi::RenderProgramDesc> rhi_desc =
                wz::engine::rendering::to_rhi_render_program_desc(
                    resolved, descriptor_semantics, constant_semantics);
            if (!rhi_desc) {
                logger.warn(
                    "rhi render-program: desc conversion failed; "
                    "renderer will bridge at render time");
                return;
            }
            rhi_desc->name = wz::engine::rendering::program_ref(program_key);
            if (!programs.register_program(std::move(*rhi_desc)).valid()) {
                logger.warn(
                    "rhi render-program: program registry full; "
                    "renderer will bridge at render time");
            }
        }
    }

    void register_render_program_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderProgramTable& table,
        RenderBindingLayoutTable& binding_layouts,
        wz::rhi::RenderProgramRegistry* programs,
        wz::rhi::DescriptorSemanticRegistry* descriptor_semantics,
        wz::rhi::ConstantSemanticRegistry* constant_semantics)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kBuiltinRenderProgramSchema,
            .output_type = kAssetTypeRenderProgram,
            .input_ports = {
                { "vertex_shader", wz::asset::AssetType::Shader },
                { "pixel_shader", wz::asset::AssetType::Shader },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "program",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Program",
                    .default_num =
                        static_cast<double>(
                            BuiltinRenderProgram::MeshWireframeDebug),
                    .options = kBuiltinProgramOptions,
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                BuiltinRenderProgramDesc param_desc{};
                const auto* desc =
                    std::any_cast<BuiltinRenderProgramDesc>(&input.meta);

                if (dep_handles.size() != 2) {
                    logger.error("render program requires exactly two shader dependencies (vertex, pixel)");
                    return compile_failed_node(input);
                }

                if (!dep_handles[0].valid() || !dep_handles[1].valid()) {
                    logger.error("render program shader dependencies did not resolve");
                    return compile_failed_node(input);
                }

                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            builtin_render_program_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error(
                        "render program missing BuiltinRenderProgramDesc or "
                        "ParamBlock");
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

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCustomRenderProgramSchema,
            .output_type = kAssetTypeRenderProgram,
            .input_ports = {
                { "vertex_shader", wz::asset::AssetType::Shader },
                { "pixel_shader", wz::asset::AssetType::Shader },
                // Authored SRG layout (issue #227). When wired, the layout
                // defines the program's root constants / descriptor bindings /
                // static samplers and the numbered binding_layout presets are
                // ignored. Unwired, presets 0-4 stay the fallback.
                {
                    "binding_layout",
                    kAssetTypeRenderBindingLayout,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "binding_model",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Binding model",
                    .default_num =
                        static_cast<double>(RenderBindingModel::MeshIA),
                    .options = kBindingModelOptions,
                },
                {
                    .name = "topology",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Topology",
                    .default_num =
                        static_cast<double>(
                            RenderPrimitiveTopology::TriangleList),
                    .options = kTopologyOptions,
                },
                {
                    .name = "default_domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Default domain",
                    .default_num =
                        static_cast<double>(RenderDomain::Debug),
                    .options = kRenderDomainOptions,
                },
                {
                    .name = "default_policy_flags",
                    .type = wz::asset::ParamType::Int,
                    .label = "Default policy flags",
                    .default_num = RenderPolicy_None,
                    .min = 0.0,
                    .max = 15.0,
                },
                {
                    .name = "input_layout",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Input layout",
                    .default_num =
                        static_cast<double>(InputLayoutKind::None),
                    .options = kInputLayoutOptions,
                },
                {
                    .name = "blend_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Blend mode",
                    .default_num =
                        static_cast<double>(BlendMode::Opaque),
                    .options = kBlendModeOptions,
                },
                {
                    .name = "depth_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Depth mode",
                    .default_num =
                        static_cast<double>(DepthMode::Disabled),
                    .options = kDepthModeOptions,
                },
                {
                    .name = "raster_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Raster mode",
                    .default_num =
                        static_cast<double>(RasterMode::SolidCullBack),
                    .options = kRasterModeOptions,
                },
                {
                    .name = "binding_layout",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Binding layout",
                    .default_num = 0.0,
                    .options = kBindingLayoutOptions,
                },
            },
            .compile = [&logger, &table, &binding_layouts, programs,
                        descriptor_semantics, constant_semantics](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                CustomRenderProgramDesc param_desc{};
                const auto* desc =
                    std::any_cast<CustomRenderProgramDesc>(&input.meta);

                // Two required shader deps at ports 0/1, plus at most the
                // optional binding_layout dep.
                if (dep_handles.size() < 2 || dep_handles.size() > 3) {
                    logger.error("custom render program requires two shader dependencies (vertex, pixel) plus an optional binding layout");
                    return compile_failed_node(
                        input,
                        "custom render program requires two shader "
                        "dependencies (vertex, pixel) plus an optional "
                        "binding layout");
                }

                if (!dep_handles[0].valid() || !dep_handles[1].valid()) {
                    logger.error("custom render program shader dependencies did not resolve");
                    return compile_failed_node(
                        input,
                        "shader dependencies did not resolve");
                }

                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            custom_render_program_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error(
                        "render program missing CustomRenderProgramDesc or "
                        "ParamBlock");
                    return compile_failed_node(
                        input,
                        "missing CustomRenderProgramDesc or ParamBlock");
                }

                // Optional binding-layout dep (issue #227): when wired, the
                // authored layout defines the whole SRG — root constants,
                // descriptor bindings, static samplers — replacing both the
                // numbered presets and any desc-carried vectors. Located by
                // TYPE, not position: dep ordering is not guaranteed once an
                // optional port exists (renderable_compilers.cpp documents
                // the hazard).
                CustomRenderProgramDesc effective = *desc;
                const RenderBindingLayoutData* wired_layout = nullptr;
                for (size_t i = 0; i < dep_nodes.size(); ++i) {
                    if (dep_nodes[i].type != kAssetTypeRenderBindingLayout) {
                        continue;
                    }
                    if (i >= dep_handles.size() || !dep_handles[i].valid()) {
                        logger.error(
                            "custom render program binding layout dependency "
                            "did not resolve");
                        return compile_failed_node(
                            input,
                            "binding layout dependency did not resolve");
                    }
                    const RenderBindingLayoutData* layout =
                        binding_layouts.get(dep_handles[i]);
                    if (!layout) {
                        logger.error(
                            "custom render program binding layout payload "
                            "missing");
                        return compile_failed_node(
                            input,
                            "binding layout payload missing");
                    }
                    RenderBindingLayoutSrg srg{};
                    std::string error;
                    if (!build_render_binding_layout_srg(
                            *layout, srg, error))
                    {
                        logger.error(
                            "custom render program binding layout: " + error);
                        return compile_failed_node(input, std::move(error));
                    }
                    effective.root_constants = std::move(srg.root_constants);
                    effective.descriptor_bindings =
                        std::move(srg.descriptor_bindings);
                    effective.static_samplers =
                        std::move(srg.static_samplers);
                    wired_layout = layout;
                    break;
                }

                // Produce the rhi program when the shared registries are present.
                // The renderer's find-then-fallback covers the null/builtin path;
                // the shaders are produced by the shader compiler (referenced here
                // by shader_ref), and the reconcile keeps both across rebinds.
                if (programs && descriptor_semantics
                    && constant_semantics && dep_nodes.size() >= 2u)
                {
                    publish_custom_rhi_render_program(
                        input.key,
                        effective,
                        dep_nodes[0].key,
                        dep_nodes[1].key,
                        *programs,
                        *descriptor_semantics,
                        *constant_semantics,
                        logger);
                }

                RenderProgramData data{};
                data.binding_model    = effective.binding_model;
                data.topology         = effective.topology;
                data.default_domain   = effective.default_domain;
                data.default_policy_flags = effective.default_policy_flags;
                data.input_layout     = effective.input_layout;
                data.blend_mode       = effective.blend_mode;
                data.depth_mode       = effective.depth_mode;
                data.raster_mode      = effective.raster_mode;
                data.root_constants   = effective.root_constants;
                data.descriptor_bindings = effective.descriptor_bindings;
                data.static_samplers  = effective.static_samplers;
                // Carry the layout's constants contract (issue #228) so a
                // custom renderable (0x70A) can validate authored constant
                // names and select the head packer from the program alone.
                if (wired_layout) {
                    data.has_authored_binding_layout = true;
                    data.constants_head = wired_layout->constants_head;
                    data.constant_fields = wired_layout->constant_fields;
                }
                data.vertex_shader    = dep_handles[0];
                data.pixel_shader     = dep_handles[1];

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store custom render program");
                    return compile_failed_node(
                        input,
                        "failed to store custom render program");
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
