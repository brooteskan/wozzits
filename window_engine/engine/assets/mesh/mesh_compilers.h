#pragma once

#include <asset/compiler.h>
#include <engine/assets/asset_cache_settings.h>
#include <logging/logger.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::engine::assets::internal {

    // The clipmap lattice mesh compiler reads a height-field ScalarField
    // dependency's resolution (texel count per side) to derive its finest cell
    // size on the editor/asset-graph (ParamBlock) authoring path, so the
    // registrar needs the ScalarFieldTable to resolve that dep. The typed
    // (ClipmapLatticeMeshDesc) authoring path supplies explicit geometric
    // parameters and no dependency, so the field table is unused for it.
    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        ScalarFieldTable& scalar_field_table,
        const EngineAssetCacheSettings& cache_settings
    );

    bool load_cached_glb_mesh(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshData& mesh);

}
