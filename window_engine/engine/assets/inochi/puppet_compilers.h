#pragma once

// engine/assets/inochi/puppet_compilers.h

#include <asset/compiler.h>
#include <engine/assets/inochi/puppet_table.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // Registrar for the puppet-from-file compiler. Reads a RawFile dependency
    // (the .inp/.inx TRNSRTS bytes), loads it into an in-memory Puppet, and --
    // when a shared rhi registry is present -- publishes the puppet's residency
    // (atlas Texture2Ds + per-Part pull buffers) and stores the source + resident
    // metadata in the PuppetTable. Null gpu_resources (a device-only library)
    // stores the source only; the RHI puppet renderable then stays unrealizable
    // until residency succeeds. Mirrors register_gaussian_splat_compilers.
    void register_puppet_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PuppetTable& table,
        wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
        RhiResourceTracker rhi_resource_tracker = {});
}
