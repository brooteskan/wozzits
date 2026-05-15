#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/mesh/mesh.h>

namespace wz::engine::assets::internal {

    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table
    );

}
