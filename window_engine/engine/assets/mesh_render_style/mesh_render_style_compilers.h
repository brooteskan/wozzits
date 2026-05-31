#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>

#include <engine/assets/mesh_render_style/mesh_render_style.h>

namespace wz::engine::assets::internal
{
    void register_mesh_render_style_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshRenderStyleTable& table);
}
