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
    wz::asset::CompilerRegistry make_engine_compiler_registry(const EngineAssetContext& ctx)
    {
        wz::asset::CompilerRegistry registry;

        register_file_carrier_compilers(registry, ctx.logger);
        register_shader_compilers(registry, ctx.logger, ctx.device);
        register_scalar_field_compilers(registry, ctx.logger, ctx.scalar_fields_table);
        register_csv_compilers(registry, ctx.logger, ctx.csv_table);
        register_json_compilers(registry, ctx.logger, ctx.json_table);
        register_toml_compilers(registry, ctx.logger, ctx.toml_table);
        register_mesh_compilers(registry, ctx.logger, ctx.mesh_table);
        register_gaussian_splat_compilers(registry, ctx.logger, ctx.gaussian_splat_cloud_table);
        register_diagnostic_table_compilers(registry, ctx.logger, ctx.diagnostic_table);
        return registry;
    }

} // namespace wz::engine::assets::internal
