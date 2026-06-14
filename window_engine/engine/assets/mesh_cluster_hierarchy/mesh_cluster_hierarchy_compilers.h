#pragma once

// engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy_compilers.h

#include <asset/compiler.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_mesh_cluster_hierarchy_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        MeshClusterHierarchyTable& hierarchy_table);
}
