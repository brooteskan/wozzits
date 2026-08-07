#pragma once

// wozzits/rhi/shader_module.h
//
// Device-agnostic shader bytecode registry. The engine adapter populates this
// from compiled shader bytes; rhi stores opaque bytes and never compiles source.

#include <wozzits/rhi/program_registry.h>
#include <wozzits/rhi/compute_program.h>
#include <wozzits/rhi/render_program.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wz::rhi
{
    struct ShaderModuleDesc
    {
        std::string name;
        ShaderStage stage = ShaderStage::Vertex;
        std::vector<uint8_t> bytecode;
    };

    inline constexpr size_t kMaxShaderModules = 256;
    using ShaderModuleRegistry =
        ProgramRegistry<ShaderModuleDesc, kMaxShaderModules>;

    struct ProgramBytecode
    {
        // These spans view registry-owned storage; they are valid only until
        // the next mutating registry call (register_program / release / clear),
        // which may reallocate that storage. Callers must consume them
        // immediately (PSO creation) and never retain them across a mutation.
        std::span<const uint8_t> vertex;
        std::span<const uint8_t> pixel;
    };

    [[nodiscard]] inline std::optional<ProgramBytecode>
    resolve_program_bytecode(const RenderProgramDesc& program,
                             const ShaderModuleRegistry& modules)
    {
        const Tag vertex_tag = modules.find(program.vertex_shader);
        const Tag pixel_tag = modules.find(program.pixel_shader);
        if (!vertex_tag.valid() || !pixel_tag.valid()) {
            return std::nullopt;
        }

        const ShaderModuleDesc* vertex = modules.get(vertex_tag);
        const ShaderModuleDesc* pixel = modules.get(pixel_tag);
        // The name resolving is not enough: a module registered under the
        // vertex name but carrying a pixel (or All) stage would resolve here
        // and only fail far downstream at PSO creation. Require the concrete
        // stage so the mismatch is a checkable null at the seam. (ShaderStage::
        // All is a binding-visibility concept, not a module stage, and does not
        // satisfy this check.) Mirrors resolve_compute_bytecode below.
        if (!vertex || !pixel
            || vertex->stage != ShaderStage::Vertex
            || pixel->stage != ShaderStage::Pixel
            || vertex->bytecode.empty()
            || pixel->bytecode.empty())
        {
            return std::nullopt;
        }

        return ProgramBytecode{ vertex->bytecode, pixel->bytecode };
    }

    [[nodiscard]] inline std::optional<std::vector<uint8_t>>
    resolve_compute_bytecode(const ComputeProgramDesc& program,
                             const ShaderModuleRegistry& modules)
    {
        const Tag compute_tag = modules.find(program.compute_shader);
        if (!compute_tag.valid()) {
            return std::nullopt;
        }

        const ShaderModuleDesc* compute = modules.get(compute_tag);
        if (!compute
            || compute->stage != ShaderStage::Compute
            || compute->bytecode.empty())
        {
            return std::nullopt;
        }

        return compute->bytecode;
    }
}
