#pragma once

#include <asset/compiler.h>
#include <file/filesystem.h>
#include <logging/logger.h>
#include <gpu/shader.h>

#include <wozzits/rhi/shader_module.h>

namespace wz::engine::assets::internal {

    // The HLSL shader compiler additionally produces an rhi ShaderModule when
    // `shaders` is non-null: it D3DCompiles the primary source to bytecode and
    // registers it under shader_ref(key) into the shared registry, alongside the
    // legacy wz::gpu::GPUHandle. Dual-output during the transition — the legacy
    // handle stays until the legacy renderer retires (#179). The render-program
    // compiler then references shaders by ref instead of re-compiling them
    // (#193). Pass nullptr (device-only library) to skip rhi production.
    void register_shader_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::gpu::Device& device,
        wz::rhi::ShaderModuleRegistry* shaders,
        // The library resource_root: a RELATIVE HLSL source_path resolves against
        // it (issue #334), matching the raw/byte file carriers. Empty => path used
        // as-is (schema-only registries / historical CWD-relative behavior).
        wz::fs::Path resource_root = {}
    );

}
