#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>
#include <gpu/shader.h>

namespace wz::engine::assets::internal {

    void register_shader_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::gpu::Device& device
    );

}
