#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <external/json/json_document.h>

#include <optional>

namespace wz::engine::assets
{
    // Generated scene export. This emits the scene fields currently understood
    // by the scene JSON compiler; it is not a source-document patcher.
    wz::json::JSONDocument export_scene_to_json_document(
        const SceneAssetData& scene);

    // Replace (or add) the "nodes" array of an existing scene document with the
    // nodes re-emitted from `nodes`, preserving every other root member (schema,
    // name, lights, defaults, ...). Used to persist live node edits back to the
    // scene file without dropping non-node data. No-op if the root isn't an
    // object.
    void set_scene_document_nodes(
        wz::json::JSONDocument& document,
        const std::vector<SceneNodeAsset>& nodes);

    // Editor-only viewport camera state, persisted in the scene file's top-level
    // "scene_editor_metadata" block (schema "wozzits.scene_editor_metadata.v1").
    // This is NOT part of the authored game scene: it is the edit viewport's
    // free-fly camera pose + tuning, so the editor reopens a project looking from
    // where it was last left. Standalone play ignores it and uses the authored
    // scene camera. Fields default to the FlyingCamera defaults, so a partial or
    // absent block still yields a usable camera.
    struct SceneEditorCameraMetadata
    {
        float position[3]    = { 0.0f, 0.0f, 0.0f };
        float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };  // x, y, z, w
        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;
        float roll_speed       = 1.5f;
    };

    // Upsert the scene_editor_metadata.camera block of an existing scene document
    // from `camera`, preserving every other root member (and every other member of
    // scene_editor_metadata, e.g. schema/version/note). Creates the metadata block
    // if absent. Mirrors set_scene_document_nodes; no-op if the root isn't an
    // object.
    void set_scene_document_editor_camera(
        wz::json::JSONDocument& document,
        const SceneEditorCameraMetadata& camera);

    // Read the scene_editor_metadata.camera block from a parsed scene document.
    // Returns nullopt when the block is absent; a present block fills the returned
    // struct field-by-field, keeping the default for any missing/malformed field.
    std::optional<SceneEditorCameraMetadata> read_scene_document_editor_camera(
        const wz::json::JSONDocument& document);

} // namespace wz::engine::assets
