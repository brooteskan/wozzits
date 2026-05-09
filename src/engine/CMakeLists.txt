target_sources(window_engine PRIVATE
    engine.cpp
    game_app.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/engine_asset_library_compiler_registry.cpp
    assets/engine_asset_library_file_carriers.cpp
    
    
    assets/file_carrier_asset_module.cpp
    assets/shader_asset_module.cpp
    assets/scalar_field_asset_module.cpp
    assets/scalar_field/scalar_field.cpp
    assets/csv/csv.cpp
    assets/json/json.cpp
    assets/csv_asset_module.cpp
    assets/test/triangle_shader_assets.cpp
    render/test/triangle_test_scene.cpp
  )