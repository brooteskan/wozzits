#pragma once

#include <asset/compiler.h>
#include <logging/logger.h>

#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>
#include <engine/assets/light/light.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // gpu_resources / rhi_resource_tracker are the shared-registry residency hook
    // (#201, mirroring the scalar-field #197 path): when present, the HDRI
    // environment-map compiler decodes the source image and publishes it as an
    // RGBA32Float rhi texture resource. Null for a device-only library, which
    // skips rhi residency (lighting metadata still computes). RhiResourceTracker
    // is defined in gpu_sparse_mesh_compilers.h (included).
    void register_light_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DirectLightTable& direct_light_table,
        AmbientLightingTable& ambient_lighting_table,
        HDRIEnvironmentTable& hdri_environment_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker);
}
