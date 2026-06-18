#pragma once

// engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h

#include <asset/compiler.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/mesh/mesh.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_gpu_sparse_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& compute,
        MeshTable& mesh_table,
        GpuSparseMeshTable& gpu_sparse_mesh_table,
        GpuResidentSparseMeshTable& resident_sparse_mesh_table);
}
