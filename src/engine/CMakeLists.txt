target_sources(window_engine PRIVATE
    engine.cpp
    app_context.cpp
    game_app.cpp
    runtime_camera.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/engine_asset_library_compiler_registry.cpp
    assets/engine_asset_library_file_carriers.cpp
    
    assets/file_carrier_asset_module.cpp
    assets/shader_asset_module.cpp

    assets/scalar_field_asset_module.cpp
    assets/scalar_field/scalar_field.cpp

    assets/csv/csv.cpp
    assets/csv_asset_module.cpp

    assets/json/json.cpp
    assets/engine_asset_library_json_compilers.cpp
    assets/json_asset_module.cpp

    assets/toml/toml.cpp
    assets/toml_asset_module.cpp
    assets/engine_asset_library_toml_compilers.cpp

    assets/mesh/mesh.cpp 
    assets/mesh_asset_module.cpp
    assets/mesh/procedural_mesh.cpp

    assets/test/triangle_shader_assets.cpp
    render/test/triangle_test_scene.cpp
  )