#pragma once

// engine/starfield/star_catalog_compilers.h

#include <asset/compiler.h>
#include <engine/starfield/star_catalog_table.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker
#include <engine/assets/json/json.h>
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // Registrar for the StarCatalog-from-JSON compiler (Seam C-2, issue #266).
    // Reads a compiled JSONDocument dependency (the baked .star_catalog.json),
    // deserializes the raw catalog rows + grade dials, runs the starfield
    // astronomy kernel (build_catalog) at compile time, and stores the built set
    // in the StarCatalogTable.
    //
    // gpu_resources / rhi_resource_tracker are the shared-registry residency
    // hook (mirrors register_sky_gaussian_compilers): when present the compiler
    // also publishes the built stars as a resident point-source StructuredBuffer
    // (variant "star_catalog") so the generic render-binding path can bind it.
    // Null for a device-only library, which skips rhi residency.
    void register_star_catalog_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::engine::starfield::StarCatalogTable& table,
        JSONTable& json_table,
        wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
        RhiResourceTracker rhi_resource_tracker = {});
}
