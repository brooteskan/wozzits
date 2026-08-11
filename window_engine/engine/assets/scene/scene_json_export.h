#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <external/json/json_document.h>
#include <file/filesystem.h>

#include <cstdint>
#include <functional>
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

    // ─── Scene-document persistence (issue #299) ────────────────────────────
    // The scene library owns the scene DOCUMENT; these two functions give it the
    // one wz::fs-backed reader/writer that turns a document into bytes on disk,
    // so the four scene-document writers no longer each roll their own persistence
    // (two of which used a raw std::ofstream whose destructor swallowed the write
    // error). Every write goes through wz::fs::write_file_text — a checked,
    // chunked, UTF-8-correct CreateFileW/WriteFile, not a stream whose buffered
    // flush on destruction can report a truncated file as a successful save.

    // Read + parse the scene document at `path`, hand the parsed document to
    // `edit`, and write the re-serialized result back ONLY if `edit` returns true
    // (a change was made). The change-gated write is what keeps an idempotent
    // caller's bytes untouched: `edit` returning false => the file is not
    // rewritten at all (no reformat, no mtime bump).
    //
    // STRICT about the existing file: a read failure (including NotFound) is
    // returned as that FileError without calling `edit`, and a file that is not
    // valid JSON is returned as FileError::InvalidPath. Callers that must CREATE a
    // missing file (save_scene's first save) detect NotFound/InvalidPath and fall
    // back to write_scene_document with a freshly built document; callers that
    // require the file to exist (the scene-file behaviour-config upsert) surface
    // the error directly. No parent directories are created (mirrors the raw
    // writers it replaces).
    wz::fs::FileError update_scene_document(
        const wz::fs::Path& path,
        const std::function<bool(wz::json::JSONDocument&)>& edit);

    // Serialize `document` and write it to `path`, for callers with no existing
    // file to preserve (a fresh prefab/scenelet export, or save_scene's create
    // path). Does NOT create parent directories.
    //
    // CRASH-ATOMIC (wz::fs::write_file_atomic): the document is staged in a
    // sibling temp and published with a rename, so an interrupted save leaves
    // the previous document intact rather than a half-written file. That matters
    // because the recovery path for an unparseable scene document is to emit a
    // fresh one from the node snapshot, which drops every non-node section the
    // read-modify-write above exists to preserve. update_scene_document's
    // write-back funnels through here too, so both scene writers are atomic.
    wz::fs::FileError write_scene_document(
        const wz::fs::Path& path,
        const wz::json::JSONDocument& document);

} // namespace wz::engine::assets
