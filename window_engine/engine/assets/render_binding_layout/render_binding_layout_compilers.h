#pragma once

// engine/assets/render_binding_layout/render_binding_layout_compilers.h

#include <asset/compiler.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_render_binding_layout_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderBindingLayoutTable& table);
}
