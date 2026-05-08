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
