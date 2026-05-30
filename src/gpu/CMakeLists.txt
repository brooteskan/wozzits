target_sources(window_engine PRIVATE
    gpu.cpp
    scalar_field_texture.cpp
    vector_field_texture.cpp
    hdr_image_texture.cpp
    mesh.cpp
    gaussian_splat.cpp 

    dx12/dx12_device.cpp
    dx12/dx12_shader.cpp
    dx12/dx12_internal.cpp
    dx12/dx12_pipeline_factory.cpp
    dx12/dx12_descriptor_allocator.cpp
    dx12/dx12_scalar_field_texture.cpp
    dx12/dx12_vector_field_texture.cpp
    dx12/dx12_hdr_image_texture.cpp
    dx12/dx12_scalar_field_debug.cpp
    dx12/dx12_mesh.cpp
    dx12/dx12_mesh_wireframe_debug.cpp
    dx12/dx12_gaussian_splat.cpp
    dx12/dx12_gaussian_splat_debug.cpp
    dx12/dx12_terrain_field_accum.cpp
    dx12/dx12_terrain_surface_mesh.cpp
    dx12/dx12_gpu_timer.cpp
)
