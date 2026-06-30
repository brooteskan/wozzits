target_sources(window_engine PRIVATE
    engine.cpp
    app_context.cpp
    app/wozzits_app_v1.cpp
    app/editor_runtime.cpp
    project/project_manifest.cpp
    project/project_runtime_launch.cpp
    editor/project_snapshot.cpp
    editor/project_snapshot_abi.cpp
    editor/asset_graph_layout.cpp
    editor/asset_graph_editor_session.cpp
    editor/asset_graph_schema_registry.cpp
    editor/asset_catalog.cpp
    editor/asset_graph_snapshot.cpp
    editor/scene_snapshot.cpp
    behavior/behavior_registry.cpp
    behavior/behavior_dispatch.cpp
    behavior/behavior_command_apply.cpp
    behavior/behavior_gpu_compute_executor.cpp
    behavior/behavior_plugin_adapter.cpp
    behavior/builtin_behaviors.cpp
    behavior/sample_collision_behaviors.cpp
    behavior/scene_camera_behaviors.cpp
    behavior/prefab_spawner_behaviors.cpp
    behavior/drive_forward_behaviors.cpp
    behavior/terrain_align_behaviors.cpp
    qstate/qstate.cpp
    cognition/mean_field.cpp
    cognition/exact_group.cpp
    cognition/anneal.cpp
    cognition/commit.cpp
    cognition/agent_layout.cpp
    cognition/learning.cpp
    cognition/exclusivity.cpp
    cognition/sequential_goals.cpp
    cognition/conditional_policy.cpp
    collision/collision_frame.cpp
    collision/collision_surface_sampling.cpp
    input_events.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/engine_asset_key_factory.cpp
    assets/engine_asset_library_compiler_registry.cpp
    assets/engine_asset_library_file_carriers.cpp
    assets/engine_disk_cache_provider.cpp
    assets/type_extensions.cpp

    assets/file_carrier_asset_module.cpp
    assets/shader_asset_module.cpp
    assets/shader/shader_compilers.cpp

    assets/scalar_field_asset_module.cpp
    assets/scalar_field/scalar_field.cpp
    assets/scalar_field/scalar_field_compilers.cpp
    assets/scalar_field/scalar_field_set.cpp

    assets/vector_field_asset_module.cpp
    assets/vector_field/vector_field.cpp
    assets/vector_field/vector_field_compilers.cpp

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
    assets/mesh/clipmap_lattice_mesh.cpp
    assets/mesh_derived_field/mesh_derived_field.cpp
    assets/mesh_derived_field/mesh_derived_field_compilers.cpp
    assets/mesh_derived_field_asset_module.cpp
    assets/mesh_sparse_operator/mesh_sparse_operator.cpp
    assets/mesh_sparse_operator/mesh_sparse_operator_compilers.cpp
    assets/mesh_sparse_operator_asset_module.cpp
    assets/gpu_sparse_mesh/gpu_sparse_mesh.cpp
    assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.cpp
    assets/gpu_sparse_mesh_asset_module.cpp
    assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.cpp
    assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.cpp
    assets/mesh_cluster_hierarchy_asset_module.cpp
    mesh_processing/mesh_processing_pmp.cpp

    assets/terrain/terrain.cpp
    assets/terrain/terrain_compilers.cpp
    assets/terrain/terrain_lod_seams.cpp
    assets/terrain/terrain_visual_proxy.cpp
    assets/terrain/terrain_visual_proxy_compilers.cpp
    assets/terrain_asset_module.cpp
    assets/terrain_visual_proxy_asset_module.cpp

    assets/collision/collision.cpp
    assets/collision/collision_compilers.cpp
    assets/collision_asset_module.cpp

    assets/placement/placement.cpp
    assets/placement/placement_compilers.cpp
    assets/placement_asset_module.cpp

    assets/audio/audio_clip.cpp
    assets/audio/wav_decoder.cpp
    assets/audio/audio_clip_compilers.cpp
    assets/audio_clip_asset_module.cpp
    assets/audio/audio_clip_bank.cpp
    assets/audio/audio_clip_bank_compilers.cpp
    assets/audio_clip_bank_asset_module.cpp
    assets/audio/audio_renderable.cpp
    assets/audio/audio_renderable_compilers.cpp
    assets/audio_renderable_asset_module.cpp
    audio/scene_audio.cpp

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

    assets/mesh_render_style/mesh_render_style.cpp
    assets/mesh_render_style/mesh_render_style_compilers.cpp
    assets/mesh_render_style_asset_module.cpp

    assets/gltf/gltf_importer.cpp 

    assets/hdri/hdri_lighting_metadata.cpp
    assets/hdri/hdri_image_loader_tinyexr.cpp

    assets/renderable/renderable.cpp
    assets/renderable/renderable_compilers.cpp
    assets/renderable_asset_module.cpp

    assets/render_program/render_program.cpp
    assets/render_program/render_program_asset_module.cpp
    assets/render_program/render_program_compilers.cpp

    assets/compute_pipeline/compute_pipeline.cpp
    assets/compute_pipeline/hlsl_binding_extract.cpp
    assets/compute_pipeline_asset_module.cpp
    assets/compute_pipeline/compute_pipeline_compilers.cpp

    assets/light/light.cpp
    assets/light/light_compilers.cpp
    assets/light_asset_module.cpp

    assets/scene/scene.cpp
    assets/scene/scene_compilers.cpp
    assets/scene/scene_authoring_materialize.cpp
    assets/scene/scene_fingerprint.cpp
    assets/scene/scene_instance.cpp
    assets/scene/scene_runtime_build.cpp
    assets/scene/scene_json_export.cpp
    assets/scene/scene_subtree_export.cpp
    assets/scene/prefab_instantiate.cpp
    assets/scene/asset_graph_json.cpp
    assets/scene_asset_module.cpp

    assets/authoring/asset_graph_authoring.cpp
    assets/authoring/scene_authoring.cpp

    rendering/clipmap_view.cpp
    rendering/renderable_gpu_cache.cpp
    rendering/renderable_pipeline_cache.cpp
    rendering/render_program_pipeline_cache.cpp
    rendering/builtin_render_programs.cpp
    rendering/render_resource_resolver.cpp
    rendering/scene_terrain_visual_proxy_bridge.cpp
    rendering/scene_render_resource_resolver.cpp
    rendering/gpu_scene_render_resource_resolver.cpp
    rendering/rhi_dx12_command_recorder.cpp
    rendering/rhi_dx12_pipeline.cpp
    rendering/rhi_render_program_bridge.cpp
    rendering/rhi_shader_bridge.cpp
    rendering/rhi_gpu_backend.cpp
    rendering/rhi_scene_renderer.cpp
)
