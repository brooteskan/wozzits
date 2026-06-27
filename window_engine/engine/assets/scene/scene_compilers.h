#pragma once

// engine/assets/scene/scene_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/json/json.h>
#include <engine/assets/mesh/mesh.h>

namespace wz::engine::assets::internal
{
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
