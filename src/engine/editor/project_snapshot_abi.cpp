#include <engine/editor/project_snapshot_abi.h>

#include <engine/abi/wozzits_abi.h>
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
}
