#pragma once

// engine/assets/renderable/renderable_compilers.h

#include <asset/compiler.h>

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    struct EngineAssetContext;

    void register_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        const EngineAssetContext& ctx);
}