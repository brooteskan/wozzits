#pragma once

#include <asset/system.h>
#include <asset/compiler.h>
#include <file/filesystem.h>
#include <gpu/gpu_types.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

namespace wz::engine::assets::test
{
    struct TriangleTestResources
    {
        wz::gpu::GPUHandle vertex_shader{};
        wz::gpu::GPUHandle pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader.valid() && pixel_shader.valid();
        }
    };

    wz::asset::CompilerRegistry make_triangle_test_compiler_registry(
        wz::gpu::Device& device,
        wz::Logger& logger
    );

    TriangleTestResources initialize_triangle_test_assets(
        wz::asset::AssetSystem& asset_sys,
        const wz::fs::Path& assets_path,
        wz::Logger& logger
    );
}