#pragma once

// engine/assets/gaussian_splat/gaussian_splat_compilers.h

#include <asset/compiler.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table,
        ScalarFieldTable& scalar_field_table);
}