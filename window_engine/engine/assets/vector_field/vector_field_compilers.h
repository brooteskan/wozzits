#pragma once

#include <asset/compiler.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>
#include <engine/assets/vector_field/vector_field.h>
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // gpu_resources / rhi_resource_tracker are the shared-registry residency hook
    // (#201, mirroring the scalar-field #197 path): when present, the compiler
    // publishes the field's first channel as an RGBA32Float rhi texture resource.
    // Null for a device-only library, which skips rhi residency.
    // RhiResourceTracker is defined in gpu_sparse_mesh_compilers.h (included).
    void register_vector_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        VectorFieldTable& vector_field_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker);

} // namespace wz::engine::assets::internal
