#pragma once

#include <file/filesystem.h>
#include <asset/types.h>
#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/csv/csv.h>
#include <engine/assets/json/json.h>
#include <engine/assets/toml/toml.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/data_table/data_table.h>
#include <engine/assets/data_table/data_table_compilers.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series.h>
#include <engine/assets/diagnostics/diagnostic_resampled_time_series_compilers.h>
#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/csv_export/csv_export_compilers.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable/renderable_compilers.h>

#include <gpu/shader.h>

namespace wz::engine::assets::internal {

    struct FileSourceDesc
    {
        wz::fs::Path full_path;
        std::string  canonical_path;
    };

    struct EngineAssetContext
    {
        wz::gpu::Device&            device;
        wz::Logger&                 logger;
        ScalarFieldTable&           scalar_fields_table;
        CSVTable&                   csv_table;
        JSONTable&                  json_table;
        TOMLTable&                  toml_table;
        MeshTable&                  mesh_table;
        GaussianSplatCloudTable&    gaussian_splat_cloud_table;
        DataTable&                  data_table;
        DiagnosticResampledTimeSeriesTable& diagnostic_resampled_time_series_table;
        CSVExportTable&             csv_export_table;
        RenderableAssetTable&       renderable_table;
    };

    wz::asset::AssetNode compile_failed_node(
        const wz::asset::AssetNode& input
    );

    wz::asset::CompilerRegistry make_engine_compiler_registry(
        const EngineAssetContext& ctx
    );

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger
    );

}
