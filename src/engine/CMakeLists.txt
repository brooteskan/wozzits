target_sources(window_engine PRIVATE
    engine.cpp
    app_context.cpp
    game_app.cpp
    benchmark_app.cpp
    flying_camera.cpp
    runtime_camera.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/engine_asset_library_compiler_registry.cpp
    assets/engine_asset_library_file_carriers.cpp

    assets/file_carrier_asset_module.cpp
    assets/shader_asset_module.cpp
    assets/shader/shader_compilers.cpp

    assets/scalar_field_asset_module.cpp
    assets/scalar_field/scalar_field.cpp
    assets/scalar_field/scalar_field_compilers.cpp
    assets/scalar_field/scalar_field_set.cpp

    assets/csv/csv.cpp
    assets/csv/csv_compilers.cpp
    assets/csv_asset_module.cpp

    assets/json/json.cpp
    assets/json/json_compilers.cpp
    assets/json_asset_module.cpp

    assets/toml/toml.cpp
    assets/toml/toml_compilers.cpp
    assets/toml_asset_module.cpp

    assets/mesh/mesh.cpp
    assets/mesh/mesh_compilers.cpp
    assets/mesh_asset_module.cpp
    assets/mesh/procedural_mesh.cpp

    assets/gaussian_splat/gaussian_splat.cpp
    assets/gaussian_splat/gaussian_splat_decode.cpp
    assets/gaussian_splat/gaussian_splat_compilers.cpp
    assets/gaussian_splat/gaussian_splat_terrain_surface_compiler.cpp
    assets/gaussian_splat/terrain_splat_from_gaea_r32_compiler.cpp
    assets/gaussian_splat/gaussian_splat_ply_schema.cpp
    assets/gaussian_splat/gaussian_splat_ply_importer.cpp
    assets/gaussian_splat/gaussian_splat_color_lod.cpp
    assets/gaussian_splat/gaussian_splat_color_lod_compiler.cpp
    assets/gaussian_splat/gaussian_splat_terrain_recipe_compiler.cpp
    assets/gaussian_splat/terrain_reconstruction.cpp
    assets/gaussian_splat/terrain_splat_compile_service.cpp

    assets/landscape/landscape_document_json.cpp

    assets/gaussian_splat_asset_module.cpp
    assets/gaussian_splat_color_lod_asset_module.cpp

    assets/data_table/data_table.cpp
    assets/data_table/data_table_compilers.cpp
    assets/data_table_asset_module.cpp

    assets/diagnostics/diagnostic_resampled_time_series.cpp
    assets/diagnostics/diagnostic_resampled_time_series_compilers.cpp
    assets/diagnostic_resampled_time_series_asset_module.cpp

    assets/diagnostics/diagnostic_timeframe_summary.cpp
    assets/diagnostics/diagnostic_timeframe_summary_compilers.cpp
    assets/diagnostic_timeframe_summary_asset_module.cpp

    assets/csv_export/csv_export.cpp
    assets/csv_export/csv_export_compilers.cpp
    assets/csv_export_asset_module.cpp

    assets/gltf/gltf_importer.cpp 

    assets/renderable/renderable.cpp
    assets/renderable/renderable_compilers.cpp
    assets/renderable_asset_module.cpp

    assets/render_program/render_program.cpp
    assets/render_program/render_program_asset_module.cpp
    assets/render_program/render_program_compilers.cpp

    assets/scene/scene.cpp
    assets/scene/scene_compilers.cpp
    assets/scene/scene_fingerprint.cpp
    assets/scene/scene_instance.cpp
    assets/scene/scene_runtime_build.cpp
    assets/scene/scene_json_export.cpp
    assets/scene_asset_module.cpp

    rendering/renderable_gpu_cache.cpp
    rendering/renderable_debug_runtime.cpp
    rendering/renderable_pipeline_cache.cpp
    rendering/render_program_pipeline_cache.cpp
    rendering/builtin_render_programs.cpp
    rendering/render_resource_resolver.cpp
    rendering/scene_render_resource_resolver.cpp
    rendering/gpu_scene_render_resource_resolver.cpp
)
