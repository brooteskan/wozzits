#pragma once

// engine/assets/render_program/render_program_compilers.h

#include <asset/compiler.h>
#include <engine/assets/render_program/render_program.h>
#include <logging/logger.h>

#include <wozzits/rhi/constants_layout.h>
#include <wozzits/rhi/render_program_registry.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

namespace wz::engine::assets::internal
{
    // The custom-render-program compiler additionally produces the rhi program
    // when the shared rhi registries are provided (non-null): it converts the
    // authored description to a RenderProgramDesc (referencing its shaders by
    // shader_ref, which the shader compiler produces as rhi ShaderModules) and
    // registers it under program_ref(program_key) so the renderer binds it by
    // find. No shader compilation here (#193). Pass nullptr registries
    // (device-only library / builtin path) to skip rhi production.
    void register_render_program_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        RenderProgramTable& table,
        wz::rhi::RenderProgramRegistry* programs,
        wz::rhi::DescriptorSemanticRegistry* descriptor_semantics,
        wz::rhi::ConstantSemanticRegistry* constant_semantics);
}
