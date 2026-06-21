#include <engine/abi/wozzits_abi.h>

#include <engine/app/editor_runtime.h>
#include <engine/editor/asset_graph_editor_session.h>
#include <engine/editor/asset_graph_layout.h>
#include <engine/editor/asset_graph_schema_registry.h>
#include <engine/editor/project_snapshot_abi.h>
#include <engine/editor/project_snapshot.h>
#include <engine/project/project_runtime_launch.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct WzEditorSession
{
    wz::asset::CompilerRegistry registry;
    std::unique_ptr<wz::engine::editor::AssetGraphEditorSession> editor;
};

struct WzEditorRuntime
{
    wz::app::EditorRuntimeControl control;
    std::thread thread;
    WzEditorLogCallback log_callback = nullptr;
    void* log_user = nullptr;
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

    void emit_editor_runtime_log(
        wz::LogLevel level,
        std::string_view timestamp,
        std::string_view text,
        void* user)
    {
        auto* runtime = static_cast<WzEditorRuntime*>(user);
        if (!runtime || !runtime->log_callback) {
            return;
        }

        runtime->log_callback(
            static_cast<uint32_t>(level),
            timestamp.data(),
            static_cast<uint64_t>(timestamp.size()),
            text.data(),
            static_cast<uint64_t>(text.size()),
            runtime->log_user);
    }
}

extern "C"
{
    uint32_t wz_abi_version(void)
    {
        return WZ_ABI_VERSION;
    }

    WzResult wz_editor_asset_catalog(WzBuffer* out_catalog)
    {
        if (const WzResult target =
                prepare_output_buffer(out_catalog, "out_catalog");
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return copy_bytes_to_buffer(
                wz::engine::editor::asset_catalog_abi_blob(),
                out_catalog);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset catalog build failed");
        }
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

            // Device-free compiler schemas: lets can_connect/snapshot validate
            // against the real declared input ports (not draft-inferred ones)
            // without an EngineAssetLibrary or a GPU device.
            session->registry =
                wz::engine::editor::build_asset_graph_schema_registry();

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

    WzResult wz_editor_asset_graph_add_node(
        WzEditorSession* session,
        uint64_t schema,
        uint32_t type,
        uint64_t* out_node_id)
    {
        if (out_node_id) {
            *out_node_id = 0u;
        }
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const wz::asset::AssetGraphDraftNodeId id =
                session->editor->add_node(
                    wz::asset::SchemaID{ schema },
                    static_cast<wz::asset::AssetType>(
                        static_cast<uint16_t>(type)));
            if (id == wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE) {
                return result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "could not add asset graph node");
            }
            if (out_node_id) {
                *out_node_id = static_cast<uint64_t>(id);
            }
            return result(WZ_RESULT_OK, "");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph add node failed");
        }
    }

    WzResult wz_editor_session_set_node_position(
        WzEditorSession* session,
        uint64_t node_id,
        double x,
        double y)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->set_node_position(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                       x,
                       y)
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node position update failed");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph node position update failed");
        }
    }

    WzResult wz_editor_session_set_zoom(
        WzEditorSession* session,
        double zoom)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->set_zoom(zoom)
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph zoom update failed");
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

    WzResult wz_editor_asset_graph_remove_node(
        WzEditorSession* session,
        uint64_t node_id)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            return session->editor->remove_node(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id))
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph remove node failed");
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

    WzResult wz_editor_asset_graph_set_node_param_string(
        WzEditorSession* session,
        uint64_t node_id,
        const char* name_utf8,
        const char* value_utf8)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }
        if (!name_utf8 || name_utf8[0] == '\0') {
            return result(
                WZ_RESULT_INVALID_ARGUMENT, "param name must not be empty");
        }

        try {
            // The editor passes the value as display text; the session converts
            // it to the param's declared ParamType (int/float/bool/enum/...).
            return session->editor->set_node_param(
                       static_cast<wz::asset::AssetGraphDraftNodeId>(node_id),
                       name_utf8,
                       value_utf8 ? value_utf8 : "")
                ? result(WZ_RESULT_OK, "")
                : result(
                    WZ_RESULT_INVALID_ARGUMENT,
                    "asset graph node not found");
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(
                WZ_RESULT_INTERNAL_ERROR,
                "asset graph set node param failed");
        }
    }

    WzResult wz_editor_session_save(WzEditorSession* session)
    {
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            const auto saved = wz::engine::editor::save_project_asset_graph(
                session->editor->desc(),
                session->editor->draft(),
                session->editor->document_root());
            return saved.ok
                ? result(WZ_RESULT_OK, "")
                : dynamic_error(WZ_RESULT_INTERNAL_ERROR, saved.error);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "asset graph save failed");
        }
    }

    WzEditorRuntime* wz_editor_runtime_start(
        const char* project_root_utf8,
        const char* resource_root_utf8,
        WzEditorLogCallback log_callback,
        void* log_user)
    {
        if (!project_root_utf8 || project_root_utf8[0] == '\0') {
            return nullptr;
        }

        try {
            auto runtime = std::make_unique<WzEditorRuntime>();
            const std::string project_root = project_root_utf8;
            const std::string resource_root =
                resource_root_utf8 ? resource_root_utf8 : "";
            runtime->log_callback = log_callback;
            runtime->log_user = log_user;

            WzEditorRuntime* raw = runtime.get();
            raw->thread = std::thread([raw, project_root, resource_root]() {
                try {
                    const auto loaded =
                        wz::engine::project::load_project_runtime_launch(
                            wz::engine::project::ProjectRuntimeLaunchDesc{
                                .project_root = project_root,
                                .resource_root = resource_root,
                            });
                    if (!loaded.ok) {
                        const std::string message =
                            "resident engine launch failed: " + loaded.error;
                        emit_editor_runtime_log(
                            wz::LogLevel::Error,
                            {},
                            message,
                            raw);
                    }
                    else {
                        const int exit_code = wz::app::run_project_runtime(
                            "Wozzits Viewport",
                            loaded.launch.asset_graph_path,
                            loaded.launch.scene_path,
                            loaded.launch.resource_root,
                            &raw->control,
                            wz::app::EditorRuntimeLogSink{
                                .write = emit_editor_runtime_log,
                                .user = raw,
                            });
                        emit_editor_runtime_log(
                            exit_code == 0
                                ? wz::LogLevel::Info
                                : wz::LogLevel::Error,
                            {},
                            exit_code == 0
                                ? "resident engine runtime exited."
                                : "resident engine runtime exited with an error.",
                            raw);
                    }
                }
                catch (...) {
                    emit_editor_runtime_log(
                        wz::LogLevel::Error,
                        {},
                        "resident engine runtime failed with an exception.",
                        raw);
                    // The runtime thread must not throw across the ABI; a
                    // failure simply ends the viewport.
                }
                // Always unblock any pending bind, including the init-fail path.
                raw->control.mark_finished();
            });

            return runtime.release();
        }
        catch (...) {
            return nullptr;
        }
    }

    void wz_editor_runtime_stop(WzEditorRuntime* runtime)
    {
        if (!runtime) {
            return;
        }
        runtime->control.request_stop();
        if (runtime->thread.joinable()) {
            runtime->thread.join();
        }
        delete runtime;
    }

    WzResult wz_editor_runtime_bind_draft(
        WzEditorRuntime* runtime,
        WzEditorSession* session)
    {
        if (!runtime) {
            return result(WZ_RESULT_INVALID_ARGUMENT, "runtime must not be null");
        }
        if (const WzResult target = validate_session(session);
            target.code != WZ_RESULT_OK)
        {
            return target;
        }

        try {
            // The draft is moved to the engine thread and the bound draft moved
            // back into the session in place (AssetGraphDraft is move-only).
            wz::asset::AssetGraphDraft& draft = session->editor->draft();
            const wz::app::AssetGraphCompileResult report =
                runtime->control.bind(draft);
            if (report.ok) {
                return result(WZ_RESULT_OK, "");
            }
            std::string detail = "asset graph compile failed";
            bool first = true;
            for (const wz::asset::AssetGraphDraftValidationMessage& diag :
                 report.diagnostics)
            {
                if (diag.severity
                    != wz::asset::AssetGraphDraftValidationSeverity::Error)
                {
                    continue;
                }
                detail += first ? ": " : "; ";
                first = false;
                if (diag.node
                    != wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE)
                {
                    detail += "node "
                        + std::to_string(static_cast<uint64_t>(diag.node))
                        + ": ";
                }
                detail += diag.message.empty()
                    ? "unspecified error"
                    : diag.message;
            }
            if (first) {
                detail += ": "
                    + std::to_string(report.diagnostics.size())
                    + " diagnostic(s)";
            }
            return dynamic_error(WZ_RESULT_INTERNAL_ERROR, detail);
        }
        catch (const std::bad_alloc&) {
            return result(WZ_RESULT_OUT_OF_MEMORY, "out of memory");
        }
        catch (...) {
            return result(WZ_RESULT_INTERNAL_ERROR, "asset graph bind failed");
        }
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
