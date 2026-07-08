#include <engine/editor/project_snapshot_abi.h>

#include <engine/abi/wozzits_abi.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/editor/asset_catalog.h>
#include <engine/editor/asset_graph_editor_session.h>
#include <engine/editor/project_snapshot.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace wz::engine::editor
{
    namespace
    {
        class AbiBlobBuilder
        {
        public:
            template <typename T>
            [[nodiscard]] uint64_t append_struct(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                align(alignof(T));
                const uint64_t offset = static_cast<uint64_t>(bytes_.size());
                const auto* first = reinterpret_cast<const uint8_t*>(&value);
                bytes_.insert(bytes_.end(), first, first + sizeof(T));
                return offset;
            }

            template <typename T>
            void patch_struct(uint64_t offset, const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                std::memcpy(
                    bytes_.data() + offset,
                    &value,
                    sizeof(T));
            }

            template <typename T>
            [[nodiscard]] WzEditorTableSpan append_table(
                const std::vector<T>& values)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (values.empty()) {
                    return {};
                }

                align(alignof(T));
                const uint64_t offset = static_cast<uint64_t>(bytes_.size());
                const auto* first =
                    reinterpret_cast<const uint8_t*>(values.data());
                bytes_.insert(
                    bytes_.end(),
                    first,
                    first + sizeof(T) * values.size());
                return WzEditorTableSpan{
                    .offset = offset,
                    .count = static_cast<uint64_t>(values.size()),
                };
            }

            [[nodiscard]] WzEditorStringSpan append_string(
                std::string_view value)
            {
                if (value.empty()) {
                    return {};
                }

                const uint64_t offset = static_cast<uint64_t>(bytes_.size());
                bytes_.insert(bytes_.end(), value.begin(), value.end());
                return WzEditorStringSpan{
                    .offset = offset,
                    .size = static_cast<uint64_t>(value.size()),
                };
            }

            [[nodiscard]] std::vector<uint8_t> take()
            {
                return std::move(bytes_);
            }

        private:
            void align(size_t alignment)
            {
                const size_t remainder = bytes_.size() % alignment;
                if (remainder == 0u) {
                    return;
                }
                bytes_.insert(
                    bytes_.end(),
                    alignment - remainder,
                    uint8_t{ 0 });
            }

            std::vector<uint8_t> bytes_;
        };

        WzEditorProjectStatus abi_project_status(
            wz::engine::project::ProjectManifestProbeStatus status)
        {
            switch (status) {
            case wz::engine::project::ProjectManifestProbeStatus::Missing:
                return WZ_EDITOR_PROJECT_STATUS_MISSING;
            case wz::engine::project::ProjectManifestProbeStatus::Invalid:
                return WZ_EDITOR_PROJECT_STATUS_INVALID;
            case wz::engine::project::ProjectManifestProbeStatus::Valid:
                return WZ_EDITOR_PROJECT_STATUS_VALID;
            }
            return WZ_EDITOR_PROJECT_STATUS_INVALID;
        }

        double component_or_zero(const std::vector<double>& values, size_t index)
        {
            return index < values.size() ? values[index] : 0.0;
        }

        WzEditorSceneTransform transform_abi(
            AbiBlobBuilder& builder,
            const SceneSnapshotTransform& transform)
        {
            return WzEditorSceneTransform{
                .translation_x = component_or_zero(transform.translation, 0u),
                .translation_y = component_or_zero(transform.translation, 1u),
                .translation_z = component_or_zero(transform.translation, 2u),
                .rotation_quaternion_x =
                    component_or_zero(transform.rotation_quat, 0u),
                .rotation_quaternion_y =
                    component_or_zero(transform.rotation_quat, 1u),
                .rotation_quaternion_z =
                    component_or_zero(transform.rotation_quat, 2u),
                .rotation_quaternion_w =
                    component_or_zero(transform.rotation_quat, 3u),
                .rotation_euler_degrees_x =
                    component_or_zero(transform.rotation_euler_degrees, 0u),
                .rotation_euler_degrees_y =
                    component_or_zero(transform.rotation_euler_degrees, 1u),
                .rotation_euler_degrees_z =
                    component_or_zero(transform.rotation_euler_degrees, 2u),
                .scale_x = component_or_zero(transform.scale, 0u),
                .scale_y = component_or_zero(transform.scale, 1u),
                .scale_z = component_or_zero(transform.scale, 2u),
                .display_translation_x =
                    builder.append_string(transform.display.translation_x),
                .display_translation_y =
                    builder.append_string(transform.display.translation_y),
                .display_translation_z =
                    builder.append_string(transform.display.translation_z),
                .display_rotation_x =
                    builder.append_string(transform.display.rotation_x),
                .display_rotation_y =
                    builder.append_string(transform.display.rotation_y),
                .display_rotation_z =
                    builder.append_string(transform.display.rotation_z),
                .display_scale_x =
                    builder.append_string(transform.display.scale_x),
                .display_scale_y =
                    builder.append_string(transform.display.scale_y),
                .display_scale_z =
                    builder.append_string(transform.display.scale_z),
            };
        }

        WzEditorSceneCamera camera_abi(const SceneSnapshotCamera& camera)
        {
            WzEditorSceneCamera out{};
            if (camera.fov_y) {
                out.fov_y = *camera.fov_y;
                out.flags |= WZ_EDITOR_SCENE_CAMERA_HAS_FOV_Y;
            }
            if (camera.near_plane) {
                out.near_plane = *camera.near_plane;
                out.flags |= WZ_EDITOR_SCENE_CAMERA_HAS_NEAR_PLANE;
            }
            if (camera.far_plane) {
                out.far_plane = *camera.far_plane;
                out.flags |= WZ_EDITOR_SCENE_CAMERA_HAS_FAR_PLANE;
            }
            if (camera.aspect) {
                out.aspect = *camera.aspect;
                out.flags |= WZ_EDITOR_SCENE_CAMERA_HAS_ASPECT;
            }
            return out;
        }

        // Pack the high-impact style subset (surface/wireframe enabled + rgba)
        // for read-back (issue #213 Phase 3b-2). No blob strings/tables — plain
        // POD copy — so it needs no AbiBlobBuilder.
        WzEditorGlbStyle glb_style_abi(const SceneSnapshotMeshStyle& style)
        {
            WzEditorGlbStyle out{};
            out.surface_enabled = style.surface_enabled ? 1u : 0u;
            out.wireframe_enabled = style.wireframe_enabled ? 1u : 0u;
            for (int i = 0; i < 4; ++i) {
                out.surface_rgba[i] = style.surface_rgba[i];
                out.wireframe_rgba[i] = style.wireframe_rgba[i];
            }
            return out;
        }

        WzEditorTableSpan scene_nodes_abi(
            AbiBlobBuilder& builder,
            const std::vector<SceneSnapshotNode>& nodes);

        WzEditorSceneNode scene_node_abi(
            AbiBlobBuilder& builder,
            const SceneSnapshotNode& node)
        {
            WzEditorSceneNode out{};
            out.id = builder.append_string(node.id);
            out.display_name = builder.append_string(node.display_name);
            out.kind = builder.append_string(node.kind);

            if (node.parent_id) {
                out.parent_id = builder.append_string(*node.parent_id);
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_PARENT;
            }

            if (node.visible) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_VISIBLE;
                if (*node.visible) {
                    out.flags |= WZ_EDITOR_SCENE_NODE_VISIBLE;
                }
            }

            // Draw-order layer key (always valid; default 0 = World).
            out.render_order = node.render_order;

            if (node.transform) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_TRANSFORM;
                out.transform = transform_abi(builder, *node.transform);
            }

            if (node.camera) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_CAMERA;
                out.camera = camera_abi(*node.camera);
            }

            if (node.renderable) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_RENDERABLE;
                if (node.renderable->asset_graph_node_id) {
                    out.flags |=
                        WZ_EDITOR_SCENE_NODE_RENDERABLE_HAS_ASSET_GRAPH_NODE_ID;
                    out.renderable.asset_graph_node_id =
                        *node.renderable->asset_graph_node_id;
                }
            }

            out.renderable_source = WzEditorSceneRenderableSource{
                .kind = builder.append_string(node.renderable_source.kind),
                .display_name =
                    builder.append_string(node.renderable_source.display_name),
            };

            std::vector<WzEditorSceneComponent> components;
            components.reserve(node.components.size());
            for (const SceneSnapshotComponent& component : node.components) {
                components.push_back(WzEditorSceneComponent{
                    .kind = builder.append_string(component.kind),
                    .display_name =
                        builder.append_string(component.display_name),
                });
            }
            out.components = builder.append_table(components);

            std::vector<WzEditorSceneBehavior> behaviors;
            behaviors.reserve(node.behaviors.size());
            for (const SceneSnapshotBehavior& behavior : node.behaviors) {
                std::vector<WzEditorStringSpan> events;
                events.reserve(behavior.events.size());
                for (const std::string& event : behavior.events) {
                    events.push_back(builder.append_string(event));
                }

                std::vector<WzEditorAssetGraphParam> config;
                config.reserve(behavior.config.size());
                for (const SceneSnapshotBehaviorConfig& entry : behavior.config)
                {
                    config.push_back(WzEditorAssetGraphParam{
                        .name = builder.append_string(entry.name),
                        .kind = builder.append_string(entry.kind),
                        .value = builder.append_string(entry.value),
                        .options = {},
                    });
                }

                behaviors.push_back(WzEditorSceneBehavior{
                    .id = builder.append_string(behavior.id),
                    .label = builder.append_string(behavior.label),
                    .module = builder.append_string(behavior.module),
                    .name = builder.append_string(behavior.name),
                    .enabled = behavior.enabled ? 1u : 0u,
                    .reserved = 0u,
                    .events = builder.append_table(events),
                    .config = builder.append_table(config),
                });
            }
            out.behaviors = builder.append_table(behaviors);

            if (node.scene_source) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE;

                // Pack the per-mesh override table (Phase 3b-2). The base style is
                // packed inline below; both carry the high-impact surface/wireframe
                // subset so the editor can pre-fill its style editor.
                std::vector<WzEditorGlbStyleOverride> overrides;
                overrides.reserve(node.scene_source->style_overrides.size());
                for (const SceneSnapshotMeshStyleOverride& ov :
                     node.scene_source->style_overrides)
                {
                    overrides.push_back(WzEditorGlbStyleOverride{
                        .mesh_index = ov.mesh_index,
                        .reserved = 0u,
                        .style = glb_style_abi(ov.style),
                    });
                }

                out.scene_source = WzEditorSceneSceneSource{
                    .kind = builder.append_string(node.scene_source->kind),
                    .path = builder.append_string(node.scene_source->path),
                    .consume_mode = builder.append_string(
                        node.scene_source->consume_mode),
                    .scene_index = node.scene_source->scene_index,
                    .style_override_count =
                        node.scene_source->style_override_count,
                    .has_base_style =
                        node.scene_source->has_base_style ? 1u : 0u,
                    .reserved = 0u,
                    .base_style = glb_style_abi(node.scene_source->base_style),
                    .style_overrides = builder.append_table(overrides),
                };
            }

            // Authored render-binding refs (issue #213): surface the persisted
            // node ids + presence flags so the inspector reveals + pre-selects the
            // "Subtree from asset" and "Render binding" sections from real state.
            if (node.scene_source_node_id) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_SCENE_SOURCE_REF;
                out.scene_source_node_id = *node.scene_source_node_id;
            }
            if (node.geometry_node_id) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_GEOMETRY;
                out.geometry_node_id = *node.geometry_node_id;
            }
            if (node.render_program_node_id) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_RENDER_PROGRAM;
                out.render_program_node_id = *node.render_program_node_id;
            }

            // Persisted Collision/Motion component field values (read-back gap
            // fix): pack the field values + set the presence flag so the inspector
            // restores them on select + after reload.
            if (node.collision) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_COLLISION;
                out.collision.has_collision_ref =
                    node.collision->collision_asset_node_id ? 1u : 0u;
                out.collision.collision_asset_node_id =
                    node.collision->collision_asset_node_id
                        ? static_cast<uint32_t>(
                              *node.collision->collision_asset_node_id)
                        : 0u;
                out.collision.constrain_movement =
                    node.collision->constrain_movement ? 1u : 0u;
            }
            if (node.motion) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_MOTION;
                out.motion.terrain_constrained =
                    node.motion->terrain_constrained ? 1u : 0u;
                out.motion.align_to_surface =
                    node.motion->align_to_surface ? 1u : 0u;
                out.motion.ride_height = node.motion->ride_height;
                out.motion.footprint_radius = node.motion->footprint_radius;
                out.motion.alignment_strength = node.motion->alignment_strength;
            }
            if (node.audio_source) {
                out.flags |= WZ_EDITOR_SCENE_NODE_HAS_AUDIO_SOURCE;
                out.audio_source.has_renderable_ref =
                    node.audio_source->audio_renderable_node_id ? 1u : 0u;
                out.audio_source.audio_renderable_node_id =
                    node.audio_source->audio_renderable_node_id
                        ? static_cast<uint64_t>(
                              *node.audio_source->audio_renderable_node_id)
                        : 0ull;
                out.audio_source.auto_play =
                    node.audio_source->auto_play ? 1u : 0u;
                out.audio_source.enabled =
                    node.audio_source->enabled ? 1u : 0u;
            }

            // Custom-renderable ingredients (issue #229/#230): always-valid
            // tables, possibly empty — presence is the count, no HAS_* flag.
            std::vector<WzEditorSceneRenderableBinding> renderable_bindings;
            renderable_bindings.reserve(node.renderable_bindings.size());
            for (const SceneSnapshotRenderableBinding& binding :
                 node.renderable_bindings)
            {
                renderable_bindings.push_back(WzEditorSceneRenderableBinding{
                    .semantic = builder.append_string(binding.semantic),
                    .asset_graph_node_id =
                        binding.asset_graph_node_id
                            ? static_cast<uint64_t>(
                                  *binding.asset_graph_node_id)
                            : 0ull,
                    .has_source_ref =
                        binding.asset_graph_node_id ? 1u : 0u,
                    .reserved = 0u,
                });
            }
            out.renderable_bindings =
                builder.append_table(renderable_bindings);

            std::vector<WzEditorSceneRenderableConstant> renderable_constants;
            renderable_constants.reserve(node.renderable_constants.size());
            for (const SceneSnapshotRenderableConstant& constant :
                 node.renderable_constants)
            {
                WzEditorSceneRenderableConstant abi_constant{};
                abi_constant.name = builder.append_string(constant.name);
                for (size_t i = 0; i < 4; ++i) {
                    abi_constant.value[i] = constant.value[i];
                }
                renderable_constants.push_back(abi_constant);
            }
            out.renderable_constants =
                builder.append_table(renderable_constants);

            out.children = scene_nodes_abi(builder, node.children);
            return out;
        }

        WzEditorTableSpan scene_nodes_abi(
            AbiBlobBuilder& builder,
            const std::vector<SceneSnapshotNode>& nodes)
        {
            std::vector<WzEditorSceneNode> abi_nodes;
            abi_nodes.reserve(nodes.size());
            for (const SceneSnapshotNode& node : nodes) {
                abi_nodes.push_back(scene_node_abi(builder, node));
            }
            return builder.append_table(abi_nodes);
        }

        WzEditorAssetGraphSnapshot asset_graph_snapshot_abi(
            AbiBlobBuilder& builder,
            const AssetGraphSnapshotLoadResult& result)
        {
            WzEditorAssetGraphSnapshot out{};
            out.ok = result.ok ? 1u : 0u;
            out.error = builder.append_string(result.error);
            out.schema = builder.append_string(result.snapshot.schema);
            out.zoom = result.snapshot.zoom;

            std::vector<WzEditorAssetGraphNode> nodes;
            nodes.reserve(result.snapshot.nodes.size());
            for (const AssetGraphSnapshotNode& node : result.snapshot.nodes) {
                std::vector<WzEditorAssetGraphPort> input_ports;
                input_ports.reserve(node.input_ports.size());
                for (const AssetGraphSnapshotPort& port : node.input_ports) {
                    input_ports.push_back(WzEditorAssetGraphPort{
                        .index = port.index,
                        .type = static_cast<uint32_t>(
                            static_cast<uint16_t>(port.type)),
                        .flags =
                            (port.required
                                 ? WZ_EDITOR_ASSET_GRAPH_PORT_REQUIRED
                                 : 0u)
                            | (port.many
                                   ? WZ_EDITOR_ASSET_GRAPH_PORT_MANY
                                   : 0u),
                        .name = builder.append_string(port.name),
                        .label = builder.append_string(port.label),
                        .type_name = builder.append_string(port.type_name),
                    });
                }

                std::vector<WzEditorAssetGraphPort> output_ports;
                output_ports.reserve(node.output_ports.size());
                for (const AssetGraphSnapshotPort& port : node.output_ports) {
                    output_ports.push_back(WzEditorAssetGraphPort{
                        .index = port.index,
                        .type = static_cast<uint32_t>(
                            static_cast<uint16_t>(port.type)),
                        .flags =
                            (port.required
                                 ? WZ_EDITOR_ASSET_GRAPH_PORT_REQUIRED
                                 : 0u)
                            | (port.many
                                   ? WZ_EDITOR_ASSET_GRAPH_PORT_MANY
                                   : 0u),
                        .name = builder.append_string(port.name),
                        .label = builder.append_string(port.label),
                        .type_name = builder.append_string(port.type_name),
                    });
                }

                std::vector<WzEditorAssetGraphDiagnostic> diagnostics;
                diagnostics.reserve(node.diagnostics.size());
                for (const AssetGraphSnapshotDiagnostic& diagnostic :
                     node.diagnostics)
                {
                    diagnostics.push_back(WzEditorAssetGraphDiagnostic{
                        .severity = static_cast<uint32_t>(
                            diagnostic.severity),
                        .code = static_cast<uint32_t>(diagnostic.code),
                        .node = diagnostic.node,
                        .edge = diagnostic.edge,
                        .input_port = diagnostic.input_port,
                        .message = builder.append_string(
                            diagnostic.message),
                        .severity_name = builder.append_string(
                            diagnostic.severity_name),
                        .code_name = builder.append_string(
                            diagnostic.code_name),
                    });
                }

                std::vector<WzEditorAssetGraphParam> params;
                params.reserve(node.params.size());
                for (const AssetGraphSnapshotParam& param : node.params) {
                    std::vector<WzEditorStringSpan> options;
                    options.reserve(param.options.size());
                    for (const std::string& option : param.options) {
                        options.push_back(builder.append_string(option));
                    }

                    params.push_back(WzEditorAssetGraphParam{
                        .name = builder.append_string(param.name),
                        .kind = builder.append_string(param.kind),
                        .value = builder.append_string(param.value),
                        .options = builder.append_table(options),
                    });
                }

                nodes.push_back(WzEditorAssetGraphNode{
                    .id = node.id,
                    .type = static_cast<uint32_t>(
                        static_cast<uint16_t>(node.type)),
                    .type_name = builder.append_string(node.type_name),
                    .schema = builder.append_string(node.schema_label),
                    .display_name = builder.append_string(node.display_name),
                    .compile_status =
                        builder.append_string(node.compile_status),
                    .x = node.x,
                    .y = node.y,
                    .input_ports = builder.append_table(input_ports),
                    .output_ports = builder.append_table(output_ports),
                    .diagnostics = builder.append_table(diagnostics),
                    .params = builder.append_table(params),
                });
            }

            std::vector<WzEditorAssetGraphEdge> edges;
            edges.reserve(result.snapshot.edges.size());
            for (const AssetGraphSnapshotEdge& edge : result.snapshot.edges) {
                edges.push_back(WzEditorAssetGraphEdge{
                    .id = edge.id,
                    .from = edge.from,
                    .to = edge.to,
                    .to_input_port = edge.to_input_port,
                });
            }

            out.nodes = builder.append_table(nodes);
            out.edges = builder.append_table(edges);
            return out;
        }

        WzEditorAssetGraphSnapshot asset_graph_snapshot_abi(
            AbiBlobBuilder& builder,
            const AssetGraphSnapshot& snapshot)
        {
            return asset_graph_snapshot_abi(
                builder,
                AssetGraphSnapshotLoadResult{
                    .ok = true,
                    .snapshot = snapshot,
                });
        }

        WzEditorAssetGraphConnectionStatus abi_connection_status(
            AssetGraphConnectionStatus status)
        {
            switch (status) {
            case AssetGraphConnectionStatus::Compatible:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_COMPATIBLE;
            case AssetGraphConnectionStatus::MissingNode:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_MISSING_NODE;
            case AssetGraphConnectionStatus::MissingCompiler:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_MISSING_COMPILER;
            case AssetGraphConnectionStatus::InvalidInputPort:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_INVALID_INPUT_PORT;
            case AssetGraphConnectionStatus::TypeMismatch:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_TYPE_MISMATCH;
            case AssetGraphConnectionStatus::SelfDependency:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_SELF_DEPENDENCY;
            case AssetGraphConnectionStatus::Cycle:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_CYCLE;
            case AssetGraphConnectionStatus::DuplicateInputPort:
                return WZ_EDITOR_ASSET_GRAPH_CONNECTION_DUPLICATE_INPUT_PORT;
            }
            return WZ_EDITOR_ASSET_GRAPH_CONNECTION_TYPE_MISMATCH;
        }

        WzEditorAssetGraphConnectionCheck connection_check_abi(
            AbiBlobBuilder& builder,
            const AssetGraphConnectionCheck& check)
        {
            return WzEditorAssetGraphConnectionCheck{
                .abi_version = WZ_ABI_VERSION,
                .compatible = check.compatible ? 1u : 0u,
                .status = abi_connection_status(check.status),
                .replaces_existing = check.replaces_existing ? 1u : 0u,
                .from = check.from,
                .to = check.to,
                .to_input_port = check.to_input_port,
                .from_type = static_cast<uint32_t>(
                    static_cast<uint16_t>(check.from_type)),
                .to_type = static_cast<uint32_t>(
                    static_cast<uint16_t>(check.to_type)),
                .message = builder.append_string(check.message),
                .from_type_name = builder.append_string(check.from_type_name),
                .to_type_name = builder.append_string(check.to_type_name),
            };
        }

        WzEditorSceneSnapshot scene_snapshot_abi(
            AbiBlobBuilder& builder,
            const SceneSnapshotLoadResult& result)
        {
            WzEditorSceneSnapshot out{};
            out.ok = result.ok ? 1u : 0u;
            out.error = builder.append_string(result.error);
            out.schema = builder.append_string(result.snapshot.schema);
            out.name = builder.append_string(result.snapshot.name);
            out.roots = scene_nodes_abi(builder, result.snapshot.roots);
            return out;
        }
    }

    std::vector<uint8_t> project_snapshot_abi_blob(
        const ProjectSnapshotLoadResult& result)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorProjectSnapshot{});

        WzEditorProjectSnapshot root{};
        root.abi_version = WZ_ABI_VERSION;
        root.ok = result.ok ? 1u : 0u;
        root.status = abi_project_status(result.status);
        root.error = builder.append_string(result.error);
        root.project_name = builder.append_string(result.project_name);
        root.asset_graph = asset_graph_snapshot_abi(builder, result.asset_graph);
        root.scene = scene_snapshot_abi(builder, result.scene);

        builder.patch_struct(root_offset, root);
        return builder.take();
    }

    std::vector<uint8_t> asset_graph_snapshot_abi_blob(
        const AssetGraphSnapshotLoadResult& result)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorAssetGraphSnapshot{});

        builder.patch_struct(
            root_offset,
            asset_graph_snapshot_abi(builder, result));
        return builder.take();
    }

    std::vector<uint8_t> asset_graph_snapshot_abi_blob(
        const AssetGraphSnapshot& snapshot)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorAssetGraphSnapshot{});

        builder.patch_struct(
            root_offset,
            asset_graph_snapshot_abi(builder, snapshot));
        return builder.take();
    }

    std::vector<uint8_t> asset_graph_connection_check_abi_blob(
        const AssetGraphConnectionCheck& check)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorAssetGraphConnectionCheck{});

        builder.patch_struct(
            root_offset,
            connection_check_abi(builder, check));
        return builder.take();
    }

    std::vector<uint8_t> project_create_abi_blob(
        const wz::engine::project::ProjectManifestCreateResult& result)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorProjectCreate{});

        WzEditorProjectCreate root{};
        root.abi_version = WZ_ABI_VERSION;
        root.ok = result.ok ? 1u : 0u;
        root.status = result.ok
            ? WZ_EDITOR_PROJECT_STATUS_VALID
            : WZ_EDITOR_PROJECT_STATUS_INVALID;
        root.created = result.created ? 1u : 0u;
        root.error = builder.append_string(result.error);

        builder.patch_struct(root_offset, root);
        return builder.take();
    }

    std::vector<uint8_t> asset_catalog_abi_blob()
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorAssetCatalog{});

        const std::vector<AssetCatalogEntry> catalog = build_asset_catalog();

        std::vector<WzEditorAssetCatalogEntry> entries;
        entries.reserve(catalog.size());
        for (const AssetCatalogEntry& entry : catalog) {
            std::vector<WzEditorAssetCatalogSchema> schemas;
            schemas.reserve(entry.schemas.size());
            for (const AssetCatalogSchema& schema : entry.schemas) {
                schemas.push_back(WzEditorAssetCatalogSchema{
                    .schema = schema.schema.value,
                    .label = builder.append_string(schema.label),
                });
            }

            entries.push_back(WzEditorAssetCatalogEntry{
                .type = static_cast<uint32_t>(
                    static_cast<uint16_t>(entry.type)),
                .reserved = 0u,
                .type_name = builder.append_string(entry.type_name),
                .category = builder.append_string(entry.category),
                .schemas = builder.append_table(schemas),
            });
        }

        WzEditorAssetCatalog root{};
        root.abi_version = WZ_ABI_VERSION;
        root.ok = 1u;
        root.entries = builder.append_table(entries);

        builder.patch_struct(root_offset, root);
        return builder.take();
    }

    std::vector<uint8_t> glb_scene_hierarchy_abi_blob(
        bool ok,
        std::string_view error,
        const wz::engine::assets::ImportedGLTFScene& scene)
    {
        AbiBlobBuilder builder;
        const uint64_t root_offset =
            builder.append_struct(WzEditorGlbSceneHierarchy{});

        std::vector<WzEditorGlbComponent> components;
        components.reserve(scene.nodes.size());
        for (const wz::engine::assets::ImportedGLTFSceneNode& node : scene.nodes)
        {
            WzEditorGlbComponent component{};
            component.id = builder.append_string(node.id);
            component.name = builder.append_string(node.name);
            component.node_index = node.node_index;
            if (node.parent_id) {
                component.parent_id = builder.append_string(*node.parent_id);
                component.flags |= WZ_EDITOR_GLB_COMPONENT_HAS_PARENT;
            }
            if (node.mesh_index) {
                component.mesh_index = *node.mesh_index;
                component.flags |= WZ_EDITOR_GLB_COMPONENT_HAS_MESH;
            }
            components.push_back(component);
        }

        WzEditorGlbSceneHierarchy root{};
        root.abi_version = WZ_ABI_VERSION;
        root.ok = ok ? 1u : 0u;
        root.error = builder.append_string(error);
        root.scene_name = builder.append_string(scene.name);
        root.scene_index = scene.scene_index;
        root.components = builder.append_table(components);

        builder.patch_struct(root_offset, root);
        return builder.take();
    }
}
