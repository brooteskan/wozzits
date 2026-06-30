#pragma once

// engine/assets/scene/scene_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/json/json.h>
#include <engine/assets/mesh/mesh.h>

#include <external/json/json_document.h>

#include <optional>

namespace wz::engine::assets::internal
{
    // Parse a scene JSON document into SceneAssetData WITHOUT resolving any
    // asset-key references (renderable/collision/terrain/mesh/field). The app
    // uses this to read a self-contained "scenelet" file into prefab nodes; the
    // scene-from-JSON compiler bridges references through the graph afterward, so
    // a prefab needs none here. Thin wrapper over the compiler's internal
    // parse_scene_json with empty reference maps; returns nullopt on a malformed
    // document (the error is logged).
    std::optional<SceneAssetData> parse_scene_data_from_json(
        const wz::json::JSONDocument& doc,
        wz::Logger& logger);

    // mesh_table is used by the "Mesh from GLB scene" extractor compiler
    // (issue #213), which reads a Scene dependency's embedded GLB geometry and
    // outputs a standalone Mesh. The Scene-from-JSON / Scene-from-GLB compilers
    // do not touch it.
    void register_scene_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table,
        SceneAssetTable& scene_table,
        MeshTable& mesh_table);

} // namespace wz::engine::assets::internal
