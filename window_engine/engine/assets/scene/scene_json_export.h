#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <external/json/json_document.h>

#include <cstdint>
#include <optional>
#include <string_view>

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

    // Set one config entry on every behaviour binding in `document` that matches
    // `module` AND whose config `match_key` equals `match_value` — the shape the
    // editor needs to re-embed a freshly compiled statechart/mind IR into the
    // behaviours of a scenelet that is NOT the open scene (issue #303). Returns
    // how many bindings actually changed.
    //
    // A surgical upsert like set_scene_document_editor_camera, NOT a nodes-array
    // replacement: the document is edited in place, so node data this exporter
    // does not model survives untouched. A binding already carrying `value` is
    // left alone, so a call that changes nothing reports 0 and the caller can
    // skip rewriting the file entirely (which is what keeps a scenelet that is
    // also the open scene from being written twice).
    //
    // Understands both node behaviour shapes — the "behaviors" array and the
    // legacy singular "behavior" object — so that compatibility rule has one
    // owner, on this side of the ABI. No-op if the root isn't an object.
    uint32_t set_scene_document_behavior_config(
        wz::json::JSONDocument& document,
        std::string_view module,
        std::string_view match_key,
        std::string_view match_value,
        std::string_view config_key,
        std::string_view value);

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
