#pragma once

// engine/assets/render_program/render_program_compilers.h

#include <asset/compiler.h>
#include <engine/assets/render_program/render_program.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_render_program_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderProgramTable& table);
}