#pragma once

// engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h

#include <asset/compiler.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_mesh_cluster_hierarchy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& mesh_field_compute,
        MeshTable& mesh_table,
        ComputePipelineTable& compute_pipeline_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
        GpuResidentSparseOperatorTable& gpu_resident_sparse_operator_table,
        GpuResidentMeshClusterHierarchyTable&
            gpu_resident_mesh_cluster_hierarchy_table,
        MeshClusterHierarchyTable& hierarchy_table);
}
