#pragma once

// engine/assets/sky_gaussian/sky_gaussian_compilers.h

#include <asset/compiler.h>
#include <engine/assets/sky_gaussian/sky_gaussian_table.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker
#include <engine/assets/json/json.h>
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // Registrar for the SkyGaussian-from-JSON compiler. Reads a compiled
    // JSONDocument dependency (the baked .sky_gaussian.json), deserializes the
    // spherical-Gaussian lobe set via sky_gaussian_from_json, and stores it in
    // the SkyGaussianTable.
    //
    // gpu_resources / rhi_resource_tracker are the shared-registry residency
    // hook (mirrors register_gaussian_splat_compilers): when present, the
    // compiler also publishes the decoded lobes as a resident StructuredBuffer
    // (variant "sky_gaussian") so the generic render-binding path can bind it.
    // Null for a device-only library, which skips rhi residency. The
    // RhiResourceTracker type is defined in gpu_sparse_mesh_compilers.h.
    void register_sky_gaussian_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        SkyGaussianTable& table,
        JSONTable& json_table,
        wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
        RhiResourceTracker rhi_resource_tracker = {});
}
