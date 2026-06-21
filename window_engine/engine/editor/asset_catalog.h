#pragma once

// engine/editor/asset_catalog.h
//
// Device-free catalog of the asset-graph node types the editor can author,
// grouped per output type with the schemas that produce each. Built from the
// same device-free schema registry the editor already uses for validation
// (build_asset_graph_schema_registry), so the browser cannot drift from the
// real compilers.
//
// The catalog deliberately withholds asset types whose GPU residency still
// lives on the legacy gpu_resident_* tables (the wozzits-rhi migration surface
// tracked by #186): the browser only offers types that already work end-to-end
// on the rhi path or have no GPU residency at all.

#include <asset/types.h>

#include <string>
#include <vector>

namespace wz::engine::editor
{
    struct AssetCatalogSchema
    {
        wz::asset::SchemaID schema;
        std::string label;
    };

    struct AssetCatalogEntry
    {
        wz::asset::AssetType type = wz::asset::AssetType::Unknown;
        std::string type_name;
        std::string category;
        std::vector<AssetCatalogSchema> schemas;
    };

    [[nodiscard]] std::vector<AssetCatalogEntry> build_asset_catalog();

    // True if the asset type's GPU residency has not yet moved to wozzits-rhi
    // (see #186) and so should be withheld from the authoring catalog.
    [[nodiscard]] bool asset_type_needs_rhi_migration(wz::asset::AssetType type);
}
