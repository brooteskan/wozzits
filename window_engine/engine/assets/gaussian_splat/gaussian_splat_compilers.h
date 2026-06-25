#pragma once

// engine/assets/gaussian_splat/gaussian_splat_compilers.h

#include <asset/compiler.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker
#include <engine/assets/json/json.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/scalar_field/scalar_field_set.h>
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets
{
    // ─── Public terrain-surface compile core ────────────────────────────────
    //
    // Exposed so toolhosts can re-run the terrain compile outside the asset
    // DAG (e.g. for live tuning panels where the user adjusts compile params
    // and we re-upload the cloud to the GPU each time).  Deterministic;
    // identical (desc, field) → identical output.
    GaussianSplatCloudData make_terrain_surface_splat_cloud(
        const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& desc,
        const ScalarFieldData& field);

    // ─── Multi-field recipe compile ────────────────────────────────────────
    //
    // Wrapper: calls make_terrain_surface_splat_cloud for geometry using the
    // recipe's height field, then patches splat colors according to the
    // recipe's color mode and field mappings.
    //
    // Returns an invalid (empty) cloud on validation failure.  On success
    // the validation result is ok=true; on failure ok=false with an error
    // message describing what went wrong.
    GaussianSplatCloudData make_terrain_splat_cloud_from_recipe(
        const TerrainSplatFieldRecipe& recipe,
        const ScalarFieldSet& fields,
        TerrainSplatRecipeValidation* out_validation = nullptr);
}

namespace wz::engine::assets::internal
{
    // gpu_resources / rhi_resource_tracker are the shared-registry residency
    // hook (#208): when present, the scalar-field-derived splat compiler
    // publishes the decoded splat cloud as a resident StructuredBuffer so the
    // RhiSceneRenderer can render it as a splat cloud. Null for a device-only
    // library, which skips rhi residency (and the splat RHI renderable stays
    // unrealizable). RhiResourceTracker is defined in
    // gpu_sparse_mesh_compilers.h (included above).
    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table,
        ScalarFieldTable& scalar_field_table,
        wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
        RhiResourceTracker rhi_resource_tracker = {});

    // Sibling registrar for the terrain-surface compiler.  Kept separate so
    // the implementation can live in its own TU.
    void register_gaussian_splat_terrain_surface_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table,
        ScalarFieldTable& scalar_field_table);

    // Registrar for the .r32 + .json terrain recipe compiler.  Internally
    // builds a transient ScalarField from the .r32 bytes, reads the JSON
    // sidecar (as a pre-compiled JSONDocument) for world params + optional
    // dimensions, and runs the existing terrain-surface compiler — so the
    // scalar field table is not used by this path.
    void register_terrain_splat_from_gaea_r32_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table,
        JSONTable& json_table);
}