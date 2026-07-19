#pragma once

// engine/assets/scalar_field_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/scalar_field/scalar_field.h>

namespace wz::engine::assets
{
    // ─── Scalar field asset types ─────────────────────────────────────────────────

    // Describes a file-backed scalar field asset to register.
    // path is relative to the EngineAssetLibrary's resource_root.
    struct ScalarFieldFileDesc
    {
        std::string name;

        wz::fs::Path path;

        uint32_t width  = 0;
        uint32_t height = 1;
        uint32_t depth  = 1;

        ScalarFieldFormat     format      = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Describes a procedural scalar field asset to register.
    // No file path is required — values are generated from the parameters alone.
    // name contributes to the asset key so differently-named procedural fields
    // with identical parameters are treated as distinct assets.
    struct ProceduralScalarFieldDesc
    {
        std::string name;

        uint32_t width  = 0;
        uint32_t height = 1;
        uint32_t depth  = 1;   // must be 1 for V1

        ScalarFieldGenerator  generator  = ScalarFieldGenerator::GradientX;

        float frequency = 1.0f;
        float amplitude = 1.0f;

        ScalarFieldFormat     format      = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Describes a procedural TERRAIN scalar field asset to register: fractal
    // noise shaped into a landscape, with a radial basin that flattens the
    // middle so a far layer can ring a near one without erupting through it.
    //
    // Every default here must match TerrainScalarFieldCompileDesc and the
    // compiler's declared ParamDecl defaults — three declarations of one value,
    // and nothing but a test stops them drifting apart.
    //
    // Output is normalised to exactly [0, 1], the same contract a Gaea .r32
    // arrives with, so the two are interchangeable under one Placement. No dial
    // is in world units: the world footprint lives in the Placement, and a
    // generator that also spoke in metres could silently disagree with it.
    struct TerrainScalarFieldDesc
    {
        std::string name;

        uint32_t resolution = 1024;   // square

        float    ridge_count = 6.0f;  // major ridges across the field
        float    ridginess   = 0.6f;  // 0 rounded hills, 1 sharp crests
        float    roughness   = 0.5f;  // per-octave amplitude falloff
        uint32_t detail      = 6;     // octaves
        uint32_t seed        = 0;

        float basin_radius  = 0.35f;  // fractions of the field half-width
        float basin_falloff = 0.25f;
        float basin_depth   = 1.0f;   // 1 = flat middle, 0 = basin off

        ScalarFieldFormat     format      = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Describes a Gaea .r32 scalar field asset to register.
    // path is relative to the EngineAssetLibrary's resource_root. Dimensions are
    // derived from the file's sample count (square convention) at compile time,
    // so none are authored here.
    struct ScalarFieldGaeaR32Desc
    {
        std::string name;

        wz::fs::Path path;

        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };

    // Returned by create_scalar_field(). Wraps the DAG output node key.
    struct ScalarFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_scalar_field(). Wraps the ResourceHandle into ScalarFieldTable.
    struct ScalarFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };


    // ─── ScalarFieldAssetModule ───────────────────────────────────────────────────

    class ScalarFieldAssetModule
    {
    public:
        ScalarFieldAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files,
            ScalarFieldTable&       table
        );

        // Register a file-backed scalar field asset in the DAG.
        // Call commit() and resolve_all() on EngineAssetLibrary before querying handles.
        ScalarFieldAsset create_scalar_field(const ScalarFieldFileDesc& desc);

        // Register a procedural scalar field asset in the DAG.
        ScalarFieldAsset create_procedural_scalar_field(const ProceduralScalarFieldDesc& desc);

        // Register a procedural terrain scalar field asset in the DAG.
        // Returns an invalid asset if name is empty: name is an identity input
        // to the key factory, so an unnamed terrain would collide with every
        // other unnamed terrain sharing its dials.
        ScalarFieldAsset create_terrain_scalar_field(const TerrainScalarFieldDesc& desc);

        // Register a Gaea .r32 scalar field asset in the DAG. The compiler derives
        // a square grid from the file's sample count (Gaea's convention).
        ScalarFieldAsset create_scalar_field_from_gaea_r32(const ScalarFieldGaeaR32Desc& desc);

        // Retrieve the ResourceHandle for a resolved scalar field asset.
        // Returns an invalid handle if the asset has not been resolved.
        ScalarFieldHandle get_scalar_field(const ScalarFieldAsset& asset) const;

        // Retrieve the resolved data for a scalar field by handle.
        // Returns nullptr if the handle is invalid or stale.
        const ScalarFieldData* get_scalar_field_data(ScalarFieldHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
        ScalarFieldTable&       table_;
    };

} // namespace wz::engine::assets
