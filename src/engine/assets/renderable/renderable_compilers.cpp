// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/renderable/render_binding_sources.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/system.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wz::engine::assets::internal
{
    namespace
    {



        // DrawLayer option order == the enum (World, Overlay). Overlay draws last.
        constexpr std::array<std::string_view, 2> kDrawLayerOptions = {
            "world",
            "overlay",
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

        wz::asset::AssetKey dep_key(
            std::span<const wz::asset::AssetNode> dep_nodes,
            size_t index)
        {
            if (index < dep_nodes.size()) {
                return dep_nodes[index].key;
            }
            return {};
        }


        RhiPullMeshRenderableCompileDesc rhi_pull_mesh_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            RhiPullMeshRenderableCompileDesc desc{};
            desc.mesh_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            // Optional style dependency (issue #195 slice A) at index 2; empty
            // when the port is unconnected (dep_key returns a zero key).
            desc.style_asset = dep_key(dep_nodes, 2);
            return desc;
        }

        // Bake the SHADING subset of a MeshRenderStyle into the recipe's style
        // POD (issue #195 slice A). Only colours / emissive / alpha / layer-enable
        // flow — the style's depth/blend/raster properties are program properties
        // now, and its field_visualization + mask (geometry-generating) parts are
        // OUT OF SCOPE: they are intentionally ignored here (the style asset keeps
        // them; the rhi mesh recipe does not consume them).
        MeshRenderStyleShading bake_mesh_render_style_shading(
            const MeshRenderStyleData& style) noexcept
        {
            MeshRenderStyleShading out{};
            out.has_style = true;
            for (int i = 0; i < 4; ++i) {
                out.wireframe_color[i] = style.wireframe.color[i];
                out.surface_color[i] = style.surface.color[i];
            }
            out.wireframe_emissive = style.wireframe.emissive_strength;
            out.surface_emissive = style.surface.emissive_strength;
            out.alpha = style.alpha;
            out.wireframe_enabled = style.wireframe.enabled;
            out.surface_enabled = style.surface.enabled;
            return out;
        }

        GpuSparseMeshRenderableCompileDesc
        gpu_sparse_mesh_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            GpuSparseMeshRenderableCompileDesc desc{};
            desc.sparse_mesh_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        GaussianSplatCloudRhiRenderableCompileDesc
        gaussian_splat_cloud_rhi_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            // Dependency order matches the create API + compiler input ports:
            // splat cloud, render program. The splat size is not recoverable
            // from dependencies; an editor/JSON-authored path supplies it via a
            // ParamBlock, the create-API path via the typed compile desc.
            GaussianSplatCloudRhiRenderableCompileDesc desc{};
            desc.splat_cloud_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        GaussianSplatCloudRenderSettings
        gaussian_splat_cloud_render_settings_from_params(
            const wz::asset::ParamBlock& params)
        {
            GaussianSplatCloudRenderSettings s{};
            s.splat_size = params.get<float>("splat_size", s.splat_size);
            return s;
        }

        StarFieldRhiRenderableCompileDesc
        star_field_rhi_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            // Dependency order matches the create API + compiler input ports:
            // star catalog, render program. The star size arrives via a
            // ParamBlock (editor/JSON) or the typed compile desc (create API).
            StarFieldRhiRenderableCompileDesc desc{};
            desc.star_catalog_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        StarFieldRenderSettings
        star_field_render_settings_from_params(
            const wz::asset::ParamBlock& params)
        {
            StarFieldRenderSettings s{};
            s.star_size = params.get<float>("star_size", s.star_size);
            s.intensity = params.get<float>("intensity", s.intensity);
            s.twinkle_amount = params.get<float>("twinkle_amount", s.twinkle_amount);
            s.twinkle_speed = params.get<float>("twinkle_speed", s.twinkle_speed);
            return s;
        }

        // The 0x708 clipmap-landscape desc/settings builders were retired with
        // the schema (issue #234); the clipmap is a 0x70A custom renderable.
        ScalarFieldDebugRenderableCompileDesc
        scalar_field_debug_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            ScalarFieldDebugRenderableCompileDesc desc{};
            desc.scalar_field_asset = dep_key(dep_nodes, 0);
            return desc;
        }

        // ── Custom renderable (issue #228, schema 0x70A) ─────────────────────
        //
        // Indexed param rows within the scalar param model (the #227 pattern):
        // bindingN_semantic maps binding port N to a layout row's semantic;
        // constN_name / constN_value / constN_w author a value for one of the
        // layout's declared constant tail fields (xyz from the float3, w from
        // the scalar — the row's field type selects how many components pack).

        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutBindings> kCustomBindingSemanticParams = {
            "binding0_semantic", "binding1_semantic",
            "binding2_semantic", "binding3_semantic",
            "binding4_semantic", "binding5_semantic",
            "binding6_semantic", "binding7_semantic",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutConstantFields> kCustomConstNameParams = {
            "const0_name", "const1_name", "const2_name", "const3_name",
            "const4_name", "const5_name", "const6_name", "const7_name",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutConstantFields> kCustomConstValueParams = {
            "const0_value", "const1_value", "const2_value", "const3_value",
            "const4_value", "const5_value", "const6_value", "const7_value",
        };
        constexpr std::array<std::string_view,
            kMaxRenderBindingLayoutConstantFields> kCustomConstWParams = {
            "const0_w", "const1_w", "const2_w", "const3_w",
            "const4_w", "const5_w", "const6_w", "const7_w",
        };

        // Builds the desc for a GRAPH-authored 0x70A node: binding sources
        // come from the node's PORT-ORDERED dep keys (ports 2+i = binding_i;
        // an empty key = unwired port) and their semantics from the indexed
        // params. A row is kept when EITHER side is present so validation can
        // name a half-authored row (semantic without a source, or a wired
        // port with no semantic) instead of silently dropping it.
        CustomRenderableCompileDesc custom_renderable_desc_from_ports(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetKey> port_dep_keys)
        {
            CustomRenderableCompileDesc desc{};
            if (!port_dep_keys.empty()) {
                desc.mesh_asset = port_dep_keys[0];
            }
            if (port_dep_keys.size() > 1) {
                desc.render_program_asset = port_dep_keys[1];
            }
            desc.draw_layer = enum_param(
                params, "draw_layer", desc.draw_layer, kDrawLayerOptions);
            for (uint32_t i = 0; i < kMaxRenderBindingLayoutBindings; ++i) {
                const size_t port = 2u + i;
                const wz::asset::AssetKey key =
                    port < port_dep_keys.size()
                        ? port_dep_keys[port]
                        : wz::asset::AssetKey{};
                std::string semantic = params.get<std::string>(
                    kCustomBindingSemanticParams[i], {});
                if (semantic.empty() && key == wz::asset::AssetKey{}) {
                    continue;
                }
                desc.bindings.push_back({ std::move(semantic), key });
            }
            for (uint32_t i = 0;
                 i < kMaxRenderBindingLayoutConstantFields;
                 ++i)
            {
                CustomRenderableCompileDesc::Constant constant{};
                constant.name = params.get<std::string>(
                    kCustomConstNameParams[i], {});
                if (constant.name.empty()) {
                    continue;
                }
                const auto xyz = params.get<std::array<float, 3>>(
                    kCustomConstValueParams[i], { 0.0f, 0.0f, 0.0f });
                constant.value[0] = xyz[0];
                constant.value[1] = xyz[1];
                constant.value[2] = xyz[2];
                constant.value[3] =
                    params.get<float>(kCustomConstWParams[i], 0.0f);
                desc.constants.push_back(std::move(constant));
            }
            // The optional Placement port follows geometry(0) + program(1) + the
            // 8 binding ports (issue #233): port 2 + kMaxRenderBindingLayoutBindings.
            const size_t placement_port =
                2u + kMaxRenderBindingLayoutBindings;
            if (placement_port < port_dep_keys.size()) {
                desc.placement_asset = port_dep_keys[placement_port];
            }
            // The optional PlacedField port follows the Placement port (issue
            // #223): port 3 + kMaxRenderBindingLayoutBindings.
            const size_t placed_field_port =
                3u + kMaxRenderBindingLayoutBindings;
            if (placed_field_port < port_dep_keys.size()) {
                desc.placed_field_asset = port_dep_keys[placed_field_port];
            }
            return desc;
        }

        [[nodiscard]] constexpr bool render_binding_kind_matches_descriptor(
            RenderBindingKind source_kind,
            DescriptorKind row_kind) noexcept
        {
            return (source_kind == RenderBindingKind::TextureSrv
                    && row_kind == DescriptorKind::TextureSRV)
                || (source_kind == RenderBindingKind::StructuredSrv
                    && row_kind == DescriptorKind::StructuredBufferSRV);
        }

        [[nodiscard]] constexpr std::string_view descriptor_kind_label(
            DescriptorKind kind) noexcept
        {
            switch (kind) {
            case DescriptorKind::TextureSRV:          return "a texture SRV";
            case DescriptorKind::StructuredBufferSRV: return "a structured SRV";
            case DescriptorKind::Sampler:             return "a sampler";
            case DescriptorKind::UAV:                 return "a UAV";
            }
            return "an unknown descriptor";
        }

        [[nodiscard]] constexpr std::string_view render_binding_kind_label(
            RenderBindingKind kind) noexcept
        {
            return kind == RenderBindingKind::TextureSrv
                ? "a texture SRV"
                : "a structured SRV";
        }

        // Validates the authored desc against the wired program's layout and
        // bakes the recipe's custom fields (bindings with resolved variants,
        // constants with resolved offsets, head + block size). Returns a
        // human-readable failure reason instead when anything is unbound,
        // unknown, duplicated, or of the wrong kind — the caller surfaces it
        // through the two-arg compile_failed_node so it reaches the node
        // inspector. `dep_nodes` supplies each binding source's asset TYPE.
        // `terrain_footprint`, when non-null, is the world footprint derived
        // from a connected Placement dep (issue #233); it makes the recipe's
        // terrain settings placement-authoritative. Null = no Placement (a
        // default, node-transform-derived footprint at render time).
        [[nodiscard]] std::optional<std::string>
        build_custom_renderable_recipe(
            const CustomRenderableCompileDesc& desc,
            const RenderProgramData& program,
            std::span<const wz::asset::AssetNode> dep_nodes,
            const ClipmapLandscapeRenderSettings* terrain_footprint,
            RhiRenderableRecipe& out)
        {
            if (!program.has_authored_binding_layout) {
                return "the render program does not carry an authored "
                       "binding layout (#227); a custom renderable requires "
                       "one (numbered binding_layout presets are not "
                       "introspectable)";
            }
            if (program.binding_model != RenderBindingModel::MeshVertexPull) {
                return "the render program's binding model must be "
                       "MeshVertexPull (the custom renderable's geometry is "
                       "a pull mesh)";
            }

            const auto source_type_of =
                [&](const wz::asset::AssetKey& key)
                    -> std::optional<wz::asset::AssetType>
                {
                    for (const wz::asset::AssetNode& dep : dep_nodes) {
                        if (dep.key == key) {
                            return dep.type;
                        }
                    }
                    return std::nullopt;
                };

            // The geometry port owns the mesh-pull semantics: the renderer
            // binds the pull buffers it uploads for the mesh dep at these
            // rows (when declared), so authored bindings may not claim them.
            const auto geometry_owned =
                [](DescriptorSemantic semantic) noexcept
                {
                    return semantic == DescriptorSemantic::PulledMeshPositions
                        || semantic == DescriptorSemantic::PulledMeshIndices
                        || semantic == DescriptorSemantic::PulledMeshNormals
                        || semantic == DescriptorSemantic::PulledMeshUvs;
                };

            // The RENDERER owns the view-frequency rows (register space 0):
            // they carry the observer and the frame's atmosphere, which are
            // per-FRAME state that no renderable has a copy of. RhiSceneRenderer
            // packs them once per frame and binds the group on any program whose
            // layout declares the row (da6c952), so — exactly like the geometry
            // port above — an authored binding may not claim one, and a declared
            // row is satisfied without any port supplying it.
            const auto renderer_owned =
                [](DescriptorSemantic semantic) noexcept
                {
                    // View-frequency blocks the renderer fills each frame (space
                    // 0), not port bindings: the camera/fog block and the 2D
                    // overlay's viewport block.
                    return semantic == DescriptorSemantic::ViewConstants
                        || semantic == DescriptorSemantic::ScreenConstants;
                };

            std::vector<DescriptorSemantic> bound;
            for (const CustomRenderableCompileDesc::Binding& binding :
                 desc.bindings)
            {
                if (binding.semantic.empty()) {
                    return "a connected binding port has no semantic set";
                }
                if (binding.asset == wz::asset::AssetKey{}) {
                    return "binding '" + binding.semantic
                        + "' has no connected source";
                }
                const std::optional<DescriptorSemantic> semantic =
                    descriptor_semantic_from_name(binding.semantic);
                if (!semantic) {
                    return "unknown binding semantic '" + binding.semantic
                        + "'";
                }
                if (geometry_owned(*semantic)) {
                    return "semantic '" + binding.semantic
                        + "' is bound by the geometry port";
                }
                if (renderer_owned(*semantic)) {
                    return "semantic '" + binding.semantic
                        + "' is view-frequency state the renderer fills each "
                          "frame; it cannot be bound from a port";
                }
                const DescriptorBinding* row = nullptr;
                for (const DescriptorBinding& candidate :
                     program.descriptor_bindings)
                {
                    if (candidate.semantic == *semantic) {
                        row = &candidate;
                        break;
                    }
                }
                if (!row) {
                    return "the program's layout does not declare semantic '"
                        + binding.semantic + "'";
                }
                if (std::find(bound.begin(), bound.end(), *semantic)
                    != bound.end())
                {
                    return "semantic '" + binding.semantic
                        + "' is bound more than once";
                }
                const std::optional<wz::asset::AssetType> source_type =
                    source_type_of(binding.asset);
                if (!source_type) {
                    return "binding '" + binding.semantic
                        + "' source is not among the node's dependencies";
                }
                const std::optional<RenderBindingSource> source =
                    render_binding_source_for(*source_type, *semantic);
                if (!source) {
                    return "a "
                        + std::string(
                            asset_type_display_name_view(*source_type))
                        + " cannot back binding '" + binding.semantic
                        + "' (no published GPU resource for that semantic; "
                          "see render_binding_sources.h)";
                }
                if (!render_binding_kind_matches_descriptor(
                        source->kind, row->kind))
                {
                    return "binding '" + binding.semantic
                        + "' kind mismatch: the layout row is "
                        + std::string(descriptor_kind_label(row->kind))
                        + " but a "
                        + std::string(
                            asset_type_display_name_view(*source_type))
                        + " publishes "
                        + std::string(render_binding_kind_label(source->kind));
                }

                bound.push_back(*semantic);
                out.bindings.push_back(RhiRenderableBinding{
                    .semantic = std::string(descriptor_semantic_name(*semantic)),
                    .key = binding.asset,
                    .variant = std::string(source->variant),
                });
            }

            // Every SRV row of the layout must now be satisfied — by the
            // geometry port (mesh-pull rows), the renderer (view-frequency
            // rows), or an authored binding.
            for (const DescriptorBinding& row : program.descriptor_bindings) {
                if (row.kind != DescriptorKind::TextureSRV
                    && row.kind != DescriptorKind::StructuredBufferSRV)
                {
                    continue;
                }
                if (geometry_owned(row.semantic) || renderer_owned(row.semantic))
                {
                    continue;
                }
                if (std::find(bound.begin(), bound.end(), row.semantic)
                    == bound.end())
                {
                    return "unbound semantic '"
                        + std::string(descriptor_semantic_name(row.semantic))
                        + "': the program's layout declares it but no "
                          "binding port supplies it";
                }
            }

            // Constants: authored values fill the layout's declared tail
            // fields; offsets derive from the head packer + declaration order.
            const uint32_t block_dwords =
                program.root_constants.empty()
                    ? 0u
                    : program.root_constants.front().value_count;
            if (block_dwords == 0u && !desc.constants.empty()) {
                return "constant values are authored but the program's "
                       "layout declares no root-constant block";
            }

            const auto declared_field_of =
                [&program](std::string_view name)
                    -> const RenderBindingConstantField*
                {
                    for (const RenderBindingConstantField& field :
                         program.constant_fields)
                    {
                        if (field.name == name) {
                            return &field;
                        }
                    }
                    return nullptr;
                };

            std::vector<std::string_view> authored;
            for (const CustomRenderableCompileDesc::Constant& constant :
                 desc.constants)
            {
                if (!declared_field_of(constant.name)) {
                    return "unknown constant name '" + constant.name
                        + "': the program's layout does not declare it";
                }
                if (std::find(
                        authored.begin(), authored.end(),
                        std::string_view(constant.name))
                    != authored.end())
                {
                    return "constant '" + constant.name
                        + "' is authored more than once";
                }
                authored.push_back(constant.name);
            }

            // Emit EVERY declared tail field in declaration order (issue
            // #229), not just the authored ones: an authored value fills the
            // entry, an unauthored one keeps the zero default. The full table
            // is what lets a per-instance scene-node override address any
            // declared field by name at pack time — the renderer never
            // re-reads the layout. Offsets follow HLSL cbuffer packing
            // (render_binding_constant_field_offset), the same rule the
            // generated binding prelude emits as packoffset (issue #231) —
            // the shader reads these dwords through a cbuffer, so tight
            // packing would misaddress any field the no-straddle rule moves.
            uint32_t running = render_binding_constants_head_dwords(
                program.constants_head);
            for (const RenderBindingConstantField& field :
                 program.constant_fields)
            {
                const uint32_t dwords =
                    render_binding_constant_type_dwords(field.type);
                running = render_binding_constant_field_offset(
                    running, field.type);

                RhiRenderableConstant baked{};
                baked.name = field.name;
                baked.offset_dwords = running;
                baked.dwords = dwords;
                for (const CustomRenderableCompileDesc::Constant& constant :
                     desc.constants)
                {
                    if (constant.name != field.name) {
                        continue;
                    }
                    for (uint32_t c = 0; c < dwords && c < 4u; ++c) {
                        baked.value[c] = constant.value[c];
                    }
                    break;
                }
                out.constants.push_back(std::move(baked));
                running += dwords;
            }

            out.mesh_key = desc.mesh_asset;
            out.program_key = desc.render_program_asset;
            out.draw_layer  = desc.draw_layer;
            out.custom = true;
            out.constants_head = program.constants_head;
            out.constants_dwords = block_dwords;

            // Camera-follow terrain (issue #233): the CameraSnappedTerrain head
            // is filled per frame by the renderer from the camera + the height
            // field's dims + the lattice geometry + these settings — so the
            // recipe must name the height field and carry the world footprint.
            // The height field is the layout's scalar_field_texture binding
            // (already resolved into out.bindings); the footprint comes from the
            // optional Placement dep. c0 (finest cell) + heightmap dims stay
            // render-time-derived from the mesh + field, exactly like 0x708.
            if (program.constants_head
                == RenderBindingConstantsHead::CameraSnappedTerrain)
            {
                const RhiRenderableBinding* height = nullptr;
                for (const RhiRenderableBinding& binding : out.bindings) {
                    if (binding.semantic
                        == descriptor_semantic_name(
                            DescriptorSemantic::ScalarFieldTexture))
                    {
                        height = &binding;
                        break;
                    }
                }
                if (!height) {
                    return "the camera-snapped terrain head requires a "
                           "'scalar_field_texture' binding (the height field "
                           "the VS displaces the lattice by)";
                }
                out.height_texture_key = height->key;

                ClipmapLandscapeRenderSettings settings{};
                settings.view_snapped = true;
                if (terrain_footprint) {
                    // A connected Placement is authoritative for the world
                    // footprint (shared with a collision on the same Placement).
                    settings = *terrain_footprint;
                }
                out.clipmap = settings;
            }
            return std::nullopt;
        }

        std::vector<wz::asset::ParamDecl> make_custom_renderable_parameters()
        {
            using wz::asset::ParamDecl;
            using wz::asset::ParamType;

            std::vector<ParamDecl> parameters;
            for (uint32_t i = 0; i < kMaxRenderBindingLayoutBindings; ++i) {
                parameters.push_back({
                    .name = kCustomBindingSemanticParams[i],
                    .type = ParamType::String,
                    .label = kCustomBindingSemanticParams[i],
                });
            }
            for (uint32_t i = 0;
                 i < kMaxRenderBindingLayoutConstantFields;
                 ++i)
            {
                parameters.push_back({
                    .name = kCustomConstNameParams[i],
                    .type = ParamType::String,
                    .label = kCustomConstNameParams[i],
                });
                parameters.push_back({
                    .name = kCustomConstValueParams[i],
                    .type = ParamType::Float3,
                    .label = kCustomConstValueParams[i],
                });
                parameters.push_back({
                    .name = kCustomConstWParams[i],
                    .type = ParamType::Float,
                    .label = kCustomConstWParams[i],
                });
            }
            parameters.push_back({
                .name = "draw_layer",
                .type = ParamType::Enum,
                .label = "Draw layer",
                .default_num = 0,   // world
                .options = kDrawLayerOptions,
            });
            return parameters;
        }









    }

    void register_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        const EngineAssetContext& ctx)
    {
        auto* logger = &ctx.logger;
        auto* mesh_table = &ctx.mesh_table;
        auto* mesh_render_style_table = &ctx.mesh_render_style_table;
        auto* scalar_fields_table = &ctx.scalar_fields_table;
        auto* gaussian_splat_cloud_table = &ctx.gaussian_splat_cloud_table;
        auto* puppet_table = &ctx.puppet_table;
        auto* renderable_table = &ctx.renderable_table;
        auto* rhi_renderable_table = &ctx.rhi_renderable_table;
        auto* render_program_table = &ctx.render_program_table;
        auto* gpu_sparse_mesh_table = &ctx.gpu_sparse_mesh_table;
        auto* placement_table = &ctx.placement_table;
        auto* placed_field_table = &ctx.placed_field_table;
        const auto* asset_system = ctx.asset_system;


        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kRhiPullMeshRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "geometry", kAssetTypeMesh },
                { "program", kAssetTypeRenderProgram },
                // Optional MeshRenderStyle (issue #195 slice A). When connected,
                // its SHADING constants are baked into the recipe and flow to the
                // shader as space2 root constants (the program must declare the
                // "mesh_style" root constant, binding_layout preset 4). Absent =
                // the recipe's zero "no style" default, rendered as a plain MVP
                // pull mesh exactly as before.
                {
                    "style",
                    kAssetTypeMeshRenderStyle,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .compile = [logger, mesh_table, render_program_table,
                        mesh_render_style_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                RhiPullMeshRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<RhiPullMeshRenderableCompileDesc>(
                        &input.meta);

                // Geometry + program are required (indices 0, 1); style is an
                // optional 3rd dep. So 2 or 3 handles are valid.
                if (!desc) {
                    editor_desc =
                        rhi_pull_mesh_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() < 2 || dep_handles.size() > 3) {
                        logger->error(
                            "RHI pull mesh renderable missing compile desc");
                        return compile_failed_node(
                            input,
                            "missing compile desc");
                    }
                }

                if (dep_handles.size() < 2 || dep_handles.size() > 3) {
                    logger->error(
                        "RHI pull mesh renderable requires geometry and program "
                        "dependencies, with an optional style dependency");
                    return compile_failed_node(
                        input,
                        "requires geometry and program dependencies, with an "
                        "optional style dependency");
                }

                const MeshData* mesh = mesh_table->get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger->error(
                        "RHI pull mesh renderable source mesh is invalid");
                    return compile_failed_node(
                        input,
                        "source mesh is invalid");
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program) {
                    logger->error(
                        "RHI pull mesh renderable program is invalid");
                    return compile_failed_node(
                        input,
                        "render program is invalid");
                }

                // Bake the optional style's shading constants. The style dep, when
                // present, is index 2 (matching the input-port order). A recipe
                // with no style dep keeps the default zero "no style" POD.
                MeshRenderStyleShading style_shading{};
                const bool has_style_dep =
                    !(desc->style_asset == wz::asset::AssetKey{});
                if (has_style_dep) {
                    if (dep_handles.size() < 3) {
                        logger->error(
                            "RHI pull mesh renderable style dependency missing");
                        return compile_failed_node(
                            input,
                            "style dependency missing");
                    }
                    const MeshRenderStyleData* style =
                        mesh_render_style_table->get(dep_handles[2]);
                    if (!style || !style->valid()) {
                        logger->error(
                            "RHI pull mesh renderable style is invalid");
                        return compile_failed_node(
                            input,
                            "mesh render style is invalid");
                    }
                    style_shading = bake_mesh_render_style_shading(*style);
                }

                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .mesh_key = desc->mesh_asset,
                        .program_key = desc->render_program_asset,
                        .style = style_shading,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store RHI pull mesh renderable recipe");
                    return compile_failed_node(
                        input,
                        "failed to store RHI pull mesh renderable recipe");
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGpuSparseMeshRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "gpu_sparse_mesh", kAssetTypeGpuSparseMesh },
                { "program", kAssetTypeRenderProgram },
            },
            .compile = [logger, gpu_sparse_mesh_table, render_program_table,
                        rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                GpuSparseMeshRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<GpuSparseMeshRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        gpu_sparse_mesh_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "GPU sparse mesh renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "GPU sparse mesh renderable requires sparse mesh and program dependencies");
                    return compile_failed_node(input);
                }
                if (dep_nodes.empty()
                    || dep_nodes[0].key != desc->sparse_mesh_asset)
                {
                    logger->error(
                        "GPU sparse mesh renderable source asset key does not match sparse mesh dependency");
                    return compile_failed_node(input);
                }

                const GpuSparseMeshData* sparse_mesh =
                    gpu_sparse_mesh_table->get(dep_handles[0]);
                if (!sparse_mesh || !sparse_mesh->valid()) {
                    logger->error(
                        "GPU sparse mesh renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error(
                        "GPU sparse mesh renderable program is invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model != RenderBindingModel::MeshVertexPull) {
                    logger->error(
                        "GPU sparse mesh renderable program must use MeshVertexPull");
                    return compile_failed_node(input);
                }

                // The geometry (gpu_sparse_mesh, #190) and program (#192/#193)
                // are already rhi-resident; emit an rhi renderable recipe so the
                // renderer binds them by identity instead of reconstructing the
                // draw from a legacy RenderableAssetData. The resident pull
                // buffers stay owned by the gpu_sparse_mesh asset.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .gpu_sparse_mesh_key = desc->sparse_mesh_asset,
                        .program_key = desc->render_program_asset,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store GPU sparse mesh renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatCloudRhiRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "splat_cloud", kAssetTypeGaussianSplatCloud },
                { "program", kAssetTypeRenderProgram },
            },
            .parameters = {
                { .name = "splat_size", .type = wz::asset::ParamType::Float,
                  .label = "Splat size", .default_num = 1.0,
                  .min = 0.0, .max = 100000.0 },
            },
            .compile = [logger, gaussian_splat_cloud_table,
                        render_program_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                GaussianSplatCloudRhiRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<GaussianSplatCloudRhiRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        gaussian_splat_cloud_rhi_renderable_desc_from_deps(
                            dep_nodes);
                    // Graph/editor authoring supplies the splat size via a
                    // ParamBlock; the deps fallback only recovers the two keys.
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc.settings =
                            gaussian_splat_cloud_render_settings_from_params(
                                *params);
                    }
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "gaussian splat cloud RHI renderable missing "
                            "compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "gaussian splat cloud RHI renderable requires splat "
                        "cloud and program dependencies");
                    return compile_failed_node(input);
                }

                const GaussianSplatCloudData* cloud =
                    gaussian_splat_cloud_table->get(dep_handles[0]);
                if (!cloud || !cloud->valid()) {
                    logger->error(
                        "gaussian splat cloud RHI renderable source cloud is "
                        "invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error(
                        "gaussian splat cloud RHI renderable program is "
                        "invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model != RenderBindingModel::SplatPull) {
                    logger->error(
                        "gaussian splat cloud RHI renderable program must use "
                        "SplatPull");
                    return compile_failed_node(input);
                }

                // The splat cloud is published resident as a decoded splat
                // StructuredBuffer (#208) and the program (#192/#193) is rhi-
                // resident; emit an rhi renderable recipe so the renderer binds
                // the cloud by identity and records the splat draw. The resident
                // buffer stays owned by the splat-cloud asset.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .program_key = desc->render_program_asset,
                        .gaussian_splat_cloud_key = desc->splat_cloud_asset,
                        .splat = desc->settings,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store gaussian splat cloud RHI renderable "
                        "recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPuppetRhiRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "puppet", kAssetTypePuppet },
                { "program", kAssetTypeRenderProgram },
            },
            .compile = [logger, puppet_table,
                        render_program_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                PuppetRhiRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<PuppetRhiRenderableCompileDesc>(&input.meta);
                if (!desc) {
                    // Editor/JSON path: recover the two keys from the deps.
                    editor_desc.puppet_asset = dep_key(dep_nodes, 0);
                    editor_desc.render_program_asset = dep_key(dep_nodes, 1);
                    desc = &editor_desc;
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "puppet RHI renderable requires puppet and program "
                        "dependencies");
                    return compile_failed_node(input);
                }

                const auto* puppet = puppet_table->get(dep_handles[0]);
                if (!puppet) {
                    logger->error(
                        "puppet RHI renderable source puppet is invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error("puppet RHI renderable program is invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model
                    != RenderBindingModel::MeshVertexPull) {
                    logger->error(
                        "puppet RHI renderable program must use MeshVertexPull");
                    return compile_failed_node(input);
                }

                // The puppet is resident (atlases + per-Part pull buffers, owned
                // by the puppet asset) and the program is rhi-resident; emit an
                // rhi renderable recipe carrying puppet_key so the renderer looks
                // up the resident draw metadata and records one Part packet per
                // Part in the Overlay layer.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .program_key = desc->render_program_asset,
                        .draw_layer = DrawLayer::Overlay,
                        .puppet_key = desc->puppet_asset,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store puppet RHI renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        // Star-field RHI renderable (issue #266). A near-verbatim mirror of the
        // gaussian-splat-cloud recipe above: a StarCatalog (resident under
        // "star_catalog") + a SplatPull program, emitted as an RhiRenderableRecipe
        // carrying star_catalog_key so the renderer takes the star branch and
        // records the instanced billboard draw. The star count is recovered at
        // render time from the catalog, so no catalog-table lookup is needed here
        // (the dep is type-enforced to kAssetTypeStarCatalog by the input port).
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kStarFieldRhiRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "star_catalog", kAssetTypeStarCatalog },
                { "program", kAssetTypeRenderProgram },
            },
            .parameters = {
                { .name = "star_size", .type = wz::asset::ParamType::Float,
                  .label = "Star size", .default_num = 1.0,
                  .min = 0.0, .max = 100000.0 },
                { .name = "intensity", .type = wz::asset::ParamType::Float,
                  .label = "Intensity", .default_num = 1.0,
                  .min = 0.0, .max = 100000.0 },
                { .name = "twinkle_amount", .type = wz::asset::ParamType::Float,
                  .label = "Twinkle amount", .default_num = 0.0,
                  .min = 0.0, .max = 1.0 },
                { .name = "twinkle_speed", .type = wz::asset::ParamType::Float,
                  .label = "Twinkle speed", .default_num = 3.0,
                  .min = 0.0, .max = 1000.0 },
            },
            .compile = [logger, render_program_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                StarFieldRhiRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<StarFieldRhiRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        star_field_rhi_renderable_desc_from_deps(dep_nodes);
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc.settings =
                            star_field_render_settings_from_params(*params);
                    }
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "star field RHI renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "star field RHI renderable requires star catalog and "
                        "program dependencies");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error(
                        "star field RHI renderable program is invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model != RenderBindingModel::SplatPull) {
                    logger->error(
                        "star field RHI renderable program must use SplatPull");
                    return compile_failed_node(input);
                }

                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .program_key = desc->render_program_asset,
                        .star_catalog_key = desc->star_catalog_asset,
                        .star = desc->settings,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store star field RHI renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        // The 0x708 clipmap-landscape compiler was retired here (issue #234);
        // the clipmap is now a 0x70A CameraSnappedTerrain custom renderable.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldDebugRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "scalar_field", kAssetTypeScalarField },
            },
            .compile = [logger, scalar_fields_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                ScalarFieldDebugRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<ScalarFieldDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    editor_desc =
                        scalar_field_debug_renderable_desc_from_deps(
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 1) {
                        logger->error("scalar field debug renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 1) {
                    logger->error("scalar field debug renderable requires one scalar field dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field =
                    scalar_fields_table->get(dep_handles[0]);

                if (!field || !field->valid()) {
                    logger->error("scalar field debug renderable source field is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::ScalarField;
                data.source_asset = desc->scalar_field_asset;
                data.program = BuiltinRenderProgram::ScalarFieldDebug;
                data.domain = RenderDomain::Debug;
                data.policy_flags = RenderPolicy_None;

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store scalar field debug renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        // Custom renderable recipe (issue #228, schema 0x70A): a pull mesh +
        // a layout-authored custom program (#227) + up to eight Any-typed
        // binding ports mapped to the layout's semantics by indexed params,
        // plus authored values for the layout's declared constant tail
        // fields. Every authored binding is validated against the program's
        // layout at compile time (unbound row, unknown semantic, kind vs
        // source type, unknown constant name) with the reason surfaced on
        // the node; the renderer binds the compiled recipe GENERICALLY.
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCustomRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "geometry", kAssetTypeMesh },
                { "program", kAssetTypeRenderProgram },
                // Generic optional binding ports. Any-typed: binding sources
                // legitimately span ScalarField / VectorField /
                // GaussianSplatCloud / GpuSparseMesh, which a single-typed
                // port cannot express — the edge-time type check is skipped
                // and the compile step validates the source KIND against the
                // wired layout row (render_binding_sources.h). Port N's
                // semantic comes from the bindingN_semantic param.
                {
                    "binding0",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding1",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding2",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding3",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding4",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding5",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding6",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "binding7",
                    wz::asset::AssetType::Any,
                    wz::asset::InputPortRequirement::Optional,
                },
                // Optional world footprint for a CameraSnappedTerrain-head
                // program (issue #233): a typed Placement port (not Any — a
                // Placement is not a resource binding). Follows the 8 binding
                // ports so custom_renderable_desc_from_ports reads it at port
                // 2 + kMaxRenderBindingLayoutBindings.
                {
                    "placement",
                    kAssetTypePlacement,
                    wz::asset::InputPortRequirement::Optional,
                },
                // Optional PlacedField (issue #223): one upstream bundling the
                // height field + its Placement. Follows the placement port, so
                // custom_renderable_desc_from_ports reads it at port
                // 3 + kMaxRenderBindingLayoutBindings. When connected it
                // SUPERSEDES the separate scalar_field_texture binding +
                // placement port.
                {
                    "placed_field",
                    kAssetTypePlacedField,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .parameters = make_custom_renderable_parameters(),
            .compile = [logger, mesh_table, render_program_table,
                        placement_table, placed_field_table,
                        rhi_renderable_table, asset_system](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                CustomRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<CustomRenderableCompileDesc>(&input.meta);
                if (!desc) {
                    const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta);
                    if (!params) {
                        logger->error(
                            "custom renderable missing compile desc");
                        return compile_failed_node(
                            input,
                            "missing CustomRenderableCompileDesc or "
                            "ParamBlock");
                    }
                    // The Any-typed binding ports cannot be told apart by
                    // dependency TYPE, and dep positions shift once optional
                    // ports exist — so the graph path recovers the node's
                    // PORT-ORDERED dep keys (empty key = unwired port) from
                    // its registration entry. That list is already part of
                    // the node's identity (the key folds the dep
                    // composition), so the compile stays content-addressed.
                    std::span<const wz::asset::AssetKey> port_keys{};
                    if (asset_system) {
                        for (const wz::asset::AssetSystem::RegistrationEntry&
                                 entry : asset_system->registered_assets())
                        {
                            if (entry.node.key == input.key) {
                                port_keys = entry.dep_keys;
                                break;
                            }
                        }
                    }
                    if (port_keys.empty()) {
                        logger->error(
                            "custom renderable has no port-ordered "
                            "registration entry");
                        return compile_failed_node(
                            input,
                            "cannot map binding ports: the node has no "
                            "port-ordered registration entry");
                    }
                    editor_desc = custom_renderable_desc_from_ports(
                        *params, port_keys);
                    desc = &editor_desc;
                }

                // Locate the required deps BY KEY: positions shift once
                // optional ports exist (the documented hazard), and the
                // binding deps are interleaved arbitrarily.
                const auto dep_index_of =
                    [&](const wz::asset::AssetKey& key)
                        -> std::optional<size_t>
                    {
                        for (size_t i = 0; i < dep_nodes.size(); ++i) {
                            if (dep_nodes[i].key == key) {
                                return i;
                            }
                        }
                        return std::nullopt;
                    };

                if (desc->mesh_asset == wz::asset::AssetKey{}) {
                    logger->error("custom renderable geometry missing");
                    return compile_failed_node(
                        input, "geometry dependency missing");
                }
                if (desc->render_program_asset == wz::asset::AssetKey{}) {
                    logger->error("custom renderable program missing");
                    return compile_failed_node(
                        input, "render program dependency missing");
                }

                const std::optional<size_t> mesh_index =
                    dep_index_of(desc->mesh_asset);
                if (!mesh_index
                    || *mesh_index >= dep_handles.size()
                    || !dep_handles[*mesh_index].valid())
                {
                    logger->error(
                        "custom renderable geometry did not resolve");
                    return compile_failed_node(
                        input, "geometry dependency did not resolve");
                }
                const MeshData* mesh =
                    mesh_table->get(dep_handles[*mesh_index]);
                if (!mesh || !mesh->valid()) {
                    logger->error(
                        "custom renderable source mesh is invalid");
                    return compile_failed_node(
                        input, "source mesh is invalid");
                }

                const std::optional<size_t> program_index =
                    dep_index_of(desc->render_program_asset);
                if (!program_index
                    || *program_index >= dep_handles.size()
                    || !dep_handles[*program_index].valid())
                {
                    logger->error(
                        "custom renderable program did not resolve");
                    return compile_failed_node(
                        input, "render program dependency did not resolve");
                }
                const RenderProgramData* program =
                    render_program_table->get(dep_handles[*program_index]);
                if (!program) {
                    logger->error(
                        "custom renderable render program is invalid");
                    return compile_failed_node(
                        input, "render program is invalid");
                }

                // Fill a CameraSnappedTerrain footprint from a resolved Placement
                // frame (extent.xz = world size, extent.y = vertical scale) —
                // the 0x708 #218 derivation, shared by the placement port and the
                // PlacedField path below.
                const auto fill_footprint =
                    [](const PlacementData& placement,
                       ClipmapLandscapeRenderSettings& out) noexcept
                    {
                        out.world_origin[0] = placement.origin[0];
                        out.world_origin[1] = placement.origin[2];
                        out.world_size[0] = placement.extent[0];
                        out.world_size[1] = placement.extent[2];
                        out.vertical_scale = placement.extent[1];
                        out.base_height = placement.base_height;
                        out.view_snapped = true;
                        out.placement_authoritative = true;
                    };

                // Resolve the optional footprint (#233). A default (absent)
                // footprint is node-transform-derived at render time.
                ClipmapLandscapeRenderSettings terrain_footprint{};
                bool has_terrain_footprint = false;

                // build_custom_renderable_recipe reads the desc's bindings +
                // resolves each binding's source type from these deps. A
                // PlacedField (below) injects a synthetic binding + dep, so work
                // from mutable copies; without a PlacedField they equal *desc /
                // dep_nodes exactly (the legacy path is unchanged).
                CustomRenderableCompileDesc effective_desc = *desc;
                std::vector<wz::asset::AssetNode> effective_deps(
                    dep_nodes.begin(), dep_nodes.end());

                if (!(desc->placed_field_asset == wz::asset::AssetKey{})) {
                    // A connected PlacedField (issue #223) is ONE upstream
                    // bundling the height field + its Placement. It SUPERSEDES
                    // the separate scalar_field_texture binding + placement port:
                    // its field is injected as the height binding and its
                    // placement drives the footprint, so a terrain look
                    // references one node and the two cannot drift apart.
                    const std::optional<size_t> placed_index =
                        dep_index_of(desc->placed_field_asset);
                    const PlacedFieldData* placed =
                        (placed_index && *placed_index < dep_handles.size())
                            ? placed_field_table->get(
                                  dep_handles[*placed_index])
                            : nullptr;
                    if (!placed || !placed->valid()) {
                        logger->error(
                            "custom renderable placed field did not resolve");
                        return compile_failed_node(
                            input, "placed field dependency did not resolve");
                    }

                    // The field IS the height binding. Drop any separately-wired
                    // scalar_field_texture binding (the PlacedField wins), then
                    // inject one for the combiner's field. Its source type is
                    // known from the PlacedField, so append a synthetic dep node
                    // for source_type_of — the field is one hop away, not a
                    // direct dep of this node.
                    const std::string height_semantic(
                        descriptor_semantic_name(
                            DescriptorSemantic::ScalarFieldTexture));
                    effective_desc.bindings.erase(
                        std::remove_if(
                            effective_desc.bindings.begin(),
                            effective_desc.bindings.end(),
                            [&](const CustomRenderableCompileDesc::Binding& b) {
                                return b.semantic == height_semantic;
                            }),
                        effective_desc.bindings.end());
                    effective_desc.bindings.push_back(
                        CustomRenderableCompileDesc::Binding{
                            height_semantic, placed->field_key });
                    wz::asset::AssetNode field_dep{};
                    field_dep.key = placed->field_key;
                    field_dep.type = placed->field_type;
                    effective_deps.push_back(std::move(field_dep));

                    // The placement drives the footprint. It is a dep of the
                    // PlacedField (one hop away), so resolve it through the asset
                    // system rather than this node's direct deps.
                    if (const auto* compiled =
                            asset_system->find_compiled(placed->placement_key))
                    {
                        if (const PlacementData* placement =
                                placement_table->get(compiled->handle))
                        {
                            fill_footprint(*placement, terrain_footprint);
                            has_terrain_footprint = true;
                        }
                    }
                }
                else if (!(desc->placement_asset == wz::asset::AssetKey{})) {
                    // Legacy path: the Placement is a direct dep at the placement
                    // port; the height field arrives via its own
                    // scalar_field_texture binding.
                    if (const std::optional<size_t> placement_index =
                            dep_index_of(desc->placement_asset);
                        placement_index
                        && *placement_index < dep_handles.size())
                    {
                        if (const PlacementData* placement =
                                placement_table->get(
                                    dep_handles[*placement_index]))
                        {
                            fill_footprint(*placement, terrain_footprint);
                            has_terrain_footprint = true;
                        }
                    }
                }

                RhiRenderableRecipe recipe{};
                if (std::optional<std::string> error =
                        build_custom_renderable_recipe(
                            effective_desc, *program, effective_deps,
                            has_terrain_footprint ? &terrain_footprint : nullptr,
                            recipe))
                {
                    logger->error("custom renderable: " + *error);
                    return compile_failed_node(input, std::move(*error));
                }

                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(std::move(recipe));
                if (!handle.valid()) {
                    logger->error(
                        "failed to store custom renderable recipe");
                    return compile_failed_node(
                        input, "failed to store custom renderable recipe");
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });
    }
}
