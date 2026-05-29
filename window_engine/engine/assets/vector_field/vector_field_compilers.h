#pragma once

#include <asset/compiler.h>
#include <engine/assets/vector_field/vector_field.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_vector_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        VectorFieldTable& vector_field_table);

} // namespace wz::engine::assets::internal
