#pragma once

// engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h

#include <asset/compiler.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/mesh/mesh.h>
#include <logging/logger.h>

#include <wozzits/rhi/gpu_resource.h>

#include <functional>
#include <vector>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // Asset-type agnostic residency tracker: records (asset key → the rhi
    // identities the compiler acquired in the shared registry). Matches
    // EngineAssetContext::RhiResourceTracker; the library binds it to
    // EngineAssetLibrary::track_rhi_resources.
    using RhiResourceTracker = std::function<void(
        const wz::asset::AssetKey&,
        std::vector<wz::rhi::ResourceIdentity>)>;

    void register_gpu_sparse_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& compute,
        MeshTable& mesh_table,
        GpuSparseMeshTable& gpu_sparse_mesh_table,
        GpuResidentSparseMeshTable& resident_sparse_mesh_table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker);
}
