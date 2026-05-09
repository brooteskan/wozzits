#pragma once

#include <file/filesystem.h>
#include <asset/types.h>
#include <asset/compiler.h>
#include <logging/logger.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/csv/csv.h>
#include <engine/assets/json/json.h>
#include <engine/assets/toml/toml.h>

#include <gpu/shader.h>

namespace wz::engine::assets::internal {

    struct FileSourceDesc
    {
        wz::fs::Path full_path;
        std::string  canonical_path;
    };

    wz::asset::AssetNode compile_failed_node(
        const wz::asset::AssetNode& input
    );

    wz::asset::CompilerRegistry make_engine_compiler_registry(
        wz::gpu::Device& device,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        CSVTable& csv_table,
        JSONTable& json_table,
        TOMLTable& toml_table
    );

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger
    );

    void register_json_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table
    );

    void register_toml_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TOMLTable& toml_table
    );

    bool compute_min_max(
        const std::vector<float>& values,
        float& min_value,
        float& max_value,
        wz::Logger& logger,
        std::string_view          label);

}