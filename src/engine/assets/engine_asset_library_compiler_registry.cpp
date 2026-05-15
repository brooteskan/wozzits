// src/engine/assets/engine_asset_library_compiler_registry.cpp

#include <engine/assets/engine_asset_library_internal.h>

#include <engine/assets/shader/shader_compilers.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/csv/csv_compilers.h>
#include <engine/assets/json/json_compilers.h>
#include <engine/assets/toml/toml_compilers.h>
#include <engine/assets/mesh/mesh_compilers.h>

namespace wz::engine::assets::internal
{
    wz::asset::CompilerRegistry make_engine_compiler_registry(
        wz::gpu::Device& device,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        CSVTable& csv_table,
        JSONTable& json_table,
        TOMLTable& toml_table,
        MeshTable& mesh_table)
    {
        wz::asset::CompilerRegistry registry;

        register_file_carrier_compilers(registry, logger);
        register_shader_compilers(registry, logger, device);
        register_scalar_field_compilers(registry, logger, scalar_field_table);
        register_csv_compilers(registry, logger, csv_table);
        register_json_compilers(registry, logger, json_table);
        register_toml_compilers(registry, logger, toml_table);
        register_mesh_compilers(registry, logger, mesh_table);

        return registry;
    }

} // namespace wz::engine::assets::internal
