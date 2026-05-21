#pragma once

// engine/assets/gaussian_splat/gaussian_splat_compilers.h

#include <asset/compiler.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <logging/logger.h>

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
}

namespace wz::engine::assets::internal
{
    void register_gaussian_splat_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& table,
        ScalarFieldTable& scalar_field_table);

    // Sibling registrar for the terrain-surface compiler.  Kept separate so
    // the implementation can live in its own TU.
    void register_gaussian_splat_terrain_surface_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table,
        ScalarFieldTable& scalar_field_table);

    // Registrar for the .r32 + .json terrain recipe compiler.  Internally
    // builds a transient ScalarField from the .r32 bytes, parses the JSON
    // sidecar for world params + optional dimensions, and runs the existing
    // terrain-surface compiler — so the scalar field table is not used by
    // this path.
    void register_terrain_splat_from_gaea_r32_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table);
}