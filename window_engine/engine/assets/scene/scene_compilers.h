#pragma once

// engine/assets/scene/scene_compilers.h

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/scene/scene.h>
#include <engine/assets/json/json.h>

namespace wz::engine::assets::internal
{
    void register_scene_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table,
        SceneAssetTable& scene_table);

} // namespace wz::engine::assets::internal
