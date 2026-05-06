target_sources(window_engine PRIVATE
    engine.cpp
    render_backends/dx12/dx12_submit.cpp
    assets/engine_asset_library.cpp
    assets/test/triangle_shader_assets.cpp
    render/test/triangle_test_scene.cpp
 )