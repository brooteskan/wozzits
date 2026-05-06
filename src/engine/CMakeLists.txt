target_sources(window_engine PRIVATE
    engine.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/engine_asset_library_compiler_registry.cpp
    assets/engine_asset_library_files.cpp
    assets/engine_asset_library_scalar_fields.cpp
    assets/engine_asset_library_shaders.cpp
    assets/scalar_field/scalar_field.cpp
    assets/test/triangle_shader_assets.cpp
    render/test/triangle_test_scene.cpp
 )