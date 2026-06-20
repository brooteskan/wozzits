#include <engine/abi/wozzits_abi.h>

#include <engine/editor/asset_graph_editor_session.h>
#include <engine/editor/asset_graph_layout.h>
#include <engine/editor/project_snapshot_abi.h>
#include <engine/editor/project_snapshot.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

struct WzEditorSession
{
    wz::asset::CompilerRegistry registry;
    std::unique_ptr<wz::engine::editor::AssetGraphEditorSession> editor;
};

namespace
{
    WzResult result(WzResultCode code, const char* message)
    {
        return WzResult{
            .code = code,
            .message = message,
        };
    }

    WzResult dynamic_error(WzResultCode code, std::string message)
    {
        thread_local std::string last_error;
        last_error = std::move(message);
        return result(code, last_error.c_str());
    }

    WzResult copy_bytes_to_buffer(const std::vector<uint8_t>& bytes, WzBuffer* out)
    {
        const uint64_t size = static_cast<uint64_t>(bytes.size());
        auto* data = static_cast<uint8_t*>(std::malloc(size == 0u ? 1u : size));
        if (!data) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }

        if (size != 0u) {
            std::memcpy(data, bytes.data(), static_cast<size_t>(size));
        }
        out->data = data;
        out->size = size;
        return result(WZ_RESULT_OK, "");
    }

    WzResult validate_scene_edit_target(
        const char* project_root_utf8,
        const char* node_id_utf8)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "project_root_utf8 must not be empty");
        }
        if (!node_id_utf8 || node_id_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "node_id_utf8 must not be empty");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult scene_mutations_not_wired_yet()
    {
        return result(
            WZ_RESULT_INTERNAL_ERROR,
            "scene authoring mutations are not implemented in the engine ABI yet");
    }

    WzResult validate_project_root(const char* project_root_utf8)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "project_root_utf8 must not be empty");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult validate_session(
        WzEditorSession* session,
        const char* parameter_name = "session")
    {
        if (!session || !session->editor) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string(parameter_name) + " must not be null");
        }
        return result(WZ_RESULT_OK, "");
    }

    WzResult prepare_output_buffer(WzBuffer* out, const char* parameter_name)
    {
        if (!out) {
            return dynamic_error(
                WZ_RESULT_INVALID_ARGUMENT,
                std::string(parameter_name) + " must not be null");
        }

        out->data = nullptr;
        out->size = 0u;
        return result(WZ_RESULT_OK, "");
    }

    void populate_session_registry_from_draft(WzEditorSession& session)
    {
        struct CompilerSeed
        {
            wz::asset::SchemaID schema{};
            wz::asset::AssetType type = wz::asset::AssetType::Unknown;
            std::vector<wz::asset::InputPort> input_ports;
        };

        std::unordered_map<
            wz::asset::CompilerKey,
            CompilerSeed,
            wz::asset::CompilerKeyHash>
            seeds;

        const wz::asset::AssetGraphDraft& draft = session.editor->draft();
        for (const wz::asset::AssetGraphDraftNode& node : draft.nodes) {
            if (node.state
                == wz::asset::AssetGraphDraftNodeState::Deleted)
            {
                continue;
            }

            const wz::asset::CompilerKey key{
                .schema = node.node.schema,
                .type = node.node.type,
            };
            seeds.try_emplace(
                key,
                CompilerSeed{
                    .schema = node.node.schema,
                    .type = node.node.type,
                });
        }

        for (const wz::asset::AssetGraphDraftEdge& edge : draft.edges) {
            const wz::asset::AssetGraphDraftNode* from =
                wz::asset::find_asset_graph_draft_node(draft, edge.from);
            const wz::asset::AssetGraphDraftNode* to =
                wz::asset::find_asset_graph_draft_node(draft, edge.to);
            if (!from || !to
                || from->state
                    == wz::asset::AssetGraphDraftNodeState::Deleted
                || to->state
                    == wz::asset::AssetGraphDraftNodeState::Deleted)
            {
                continue;
            }

            const wz::asset::CompilerKey key{
                .schema = to->node.schema,
                .type = to->node.type,
            };
            auto& seed = seeds.try_emplace(
                key,
                CompilerSeed{
                    .schema = to->node.schema,
                    .type = to->node.type,
                }).first->second;

            if (seed.input_ports.size() <= edge.to_input_port) {
                seed.input_ports.resize(
                    static_cast<std::size_t>(edge.to_input_port) + 1u,
                    wz::asset::InputPort{
                        .name = "",
                        .type = wz::asset::AssetType::Unknown,
                        .requirement =
                            wz::asset::InputPortRequirement::Optional,
                    });
            }

            wz::asset::InputPort& port =
                seed.input_ports[edge.to_input_port];
            if (port.type == wz::asset::AssetType::Unknown) {
                port.type = from->node.type;
            }
            else if (port.type == from->node.type) {
                port.arity = wz::asset::InputPortArity::Many;
            }
        }

        session.registry = {};
        for (auto& [_, seed] : seeds) {
            session.registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = seed.schema,
                .output_type = seed.type,
                .input_ports = std::move(seed.input_ports),
            });
        }
    }
}

extern "C"
{
    uint32_t wz_abi_version(void)
    {
        return WZ_ABI_VERSION;
    }

    WzResult wz_editor_load_project_snapshot(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzBuffer* out_snapshot)
    {
        if (!out_snapshot) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_snapshot must not be null");
        }

        out_snapshot->data = nullptr;
        out_snapshot->size = 0u;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto snapshot =
                wz::engine::editor::load_project_snapshot(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    });
            return copy_bytes_to_buffer(
                wz::engine::editor::project_snapshot_abi_blob(snapshot),
                out_snapshot);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "project snapshot load failed");
        }
    }

    WzResult wz_editor_create_project(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* name_utf8,
        WzBuffer* out_project)
    {
        if (!out_project) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_project must not be null");
        }

        out_project->data = nullptr;
        out_project->size = 0u;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto created =
                wz::engine::project::create_project_manifest(
                    wz::engine::project::ProjectManifestCreateDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                        .name = name_utf8 ? name_utf8 : "",
                    });
            return copy_bytes_to_buffer(
                wz::engine::editor::project_create_abi_blob(created),
                out_project);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "project creation failed");
        }
    }

    WzResult wz_editor_scene_set_node_properties(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* name_utf8,
        uint32_t visible)
    {
        (void)resource_root_utf8;
        (void)name_utf8;
        (void)visible;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_editor_scene_set_node_transform(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* translation_x_utf8,
        const char* translation_y_utf8,
        const char* translation_z_utf8,
        const char* rotation_x_utf8,
        const char* rotation_y_utf8,
        const char* rotation_z_utf8,
        const char* scale_x_utf8,
        const char* scale_y_utf8,
        const char* scale_z_utf8)
    {
        (void)resource_root_utf8;
        (void)translation_x_utf8;
        (void)translation_y_utf8;
        (void)translation_z_utf8;
        (void)rotation_x_utf8;
        (void)rotation_y_utf8;
        (void)rotation_z_utf8;
        (void)scale_x_utf8;
        (void)scale_y_utf8;
        (void)scale_z_utf8;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_editor_scene_set_camera(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        const char* node_id_utf8,
        const char* fov_y_utf8,
        const char* near_plane_utf8,
        const char* far_plane_utf8,
        const char* aspect_utf8)
    {
        (void)resource_root_utf8;
        (void)fov_y_utf8;
        (void)near_plane_utf8;
        (void)far_plane_utf8;
        (void)aspect_utf8;

        if (const WzResult target =
                validate_scene_edit_target(project_root_utf8, node_id_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return scene_mutations_not_wired_yet();
    }

    WzResult wz_editor_asset_graph_set_node_position(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        uint64_t node_id,
        double x,
        double y)
    {
        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto updated =
                wz::engine::editor::update_project_asset_graph_node_layout(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                    x,
                    y);
            return updated.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    updated.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph layout update failed");
        }
    }

    WzResult wz_editor_asset_graph_set_zoom(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        double zoom)
    {
        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto updated =
                wz::engine::editor::update_project_asset_graph_zoom(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    zoom);
            return updated.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    updated.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph zoom update failed");
        }
    }

    WzResult wz_editor_open_project_session(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzEditorSession** out_session)
    {
        if (!out_session) {
            return result(
                WZ_RESULT_INVALID_ARGUMENT,
                "out_session must not be null");
        }
        *out_session = nullptr;

        if (const WzResult target = validate_project_root(project_root_utf8);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            auto session = std::make_unique<WzEditorSession>();

            auto opened =
                wz::engine::editor::open_asset_graph_editor_session(
                    wz::engine::project::ProjectManifestLoadDesc{
                        .project_root = project_root_utf8,
                        .resource_root = resource_root_utf8
                            ? resource_root_utf8
                            : "",
                    },
                    session->registry);
            if (!opened.ok || !opened.session) {
                return dynamic_error(
                    WZ_RESULT_INVALID_ARGUMENT,
                    opened.error);
            }

            session->editor = std::move(opened.session);
            populate_session_registry_from_draft(*session);
            *out_session = session.release();
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "editor session open failed");
        }
    }

    void wz_editor_close_session(WzEditorSession* session)
    {
        delete session;
    }

    WzResult wz_editor_session_asset_graph_snapshot(
        WzEditorSession* session,
        WzBuffer* out_snapshot)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_snapshot, "out_snapshot");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_snapshot_abi_blob(
                    session->editor->snapshot()),
                out_snapshot);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph snapshot failed");
        }
    }

    WzResult wz_editor_asset_graph_can_connect(
        WzEditorSession* session,
        uint64_t from_node_id,
        uint64_t to_node_id,
        uint32_t to_input_port,
        WzBuffer* out_check)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_check, "out_check");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_connection_check_abi_blob(
                    session->editor->can_connect(
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            from_node_id),
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            to_node_id),
                        to_input_port)),
                out_check);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph connection check failed");
        }
    }

    WzResult wz_editor_asset_graph_connect(
        WzEditorSession* session,
        uint64_t from_node_id,
        uint64_t to_node_id,
        uint32_t to_input_port,
        WzBuffer* out_check)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (const WzResult target =
                prepare_output_buffer(out_check, "out_check");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_graph_connection_check_abi_blob(
                    session->editor->connect(
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            from_node_id),
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            to_node_id),
                        to_input_port)),
                out_check);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph connect failed");
        }
    }

    WzResult wz_editor_asset_graph_disconnect_edge(
        WzEditorSession* session,
        uint64_t edge_id)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->disconnect_edge(
                       static_cast<wz::asset::AssetGraphDraftEdgeId>(edge_id))
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph edge not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph disconnect failed");
        }
    }

    WzResult wz_editor_asset_graph_commit(WzEditorSession* session)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return result(
            WZ_RESULT_INVALID_ARGUMENT,
            "asset graph commit must run through the editor host/runtime channel");
    }

    WzResult wz_editor_asset_graph_compile(WzEditorSession* session)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        return result(
            WZ_RESULT_INVALID_ARGUMENT,
            "asset graph compile must run through the editor host/runtime channel");
    }

    void wz_free_buffer(WzBuffer* buffer)
    {
        if (!buffer) {
            return;
        }
        std::free(buffer->data);
        buffer->data = nullptr;
        buffer->size = 0u;
    }
}
