#pragma once

// engine/rendering/rhi_shader_bridge.h
//
// Engine -> rhi shader-module adapter. This stage takes already-compiled
// bytecode from the caller and registers it under the same refs used by
// rhi_render_program_bridge.

#include <engine/assets/render_program/render_program.h>

#include <wozzits/rhi/shader_module.h>

#include <cstdint>
#include <span>

namespace wz::engine::rendering
{
    bool register_program_shaders(
        wz::rhi::ShaderModuleRegistry& shaders,
        const wz::engine::assets::CustomRenderProgramDesc& src,
        std::span<const uint8_t> vertex_bytecode,
        std::span<const uint8_t> pixel_bytecode);
}
