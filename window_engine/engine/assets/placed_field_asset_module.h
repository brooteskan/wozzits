#pragma once

// engine/assets/placed_field_asset_module.h

#include <asset/system.h>
#include <engine/assets/placed_field/placed_field.h>
#include <engine/assets/type_extensions.h>

#include <logging/logger.h>

namespace wz::engine::assets
{
    // Authoring recipe for a PlacedField combiner (issue #223): bind a frame-less
    // field to a Placement frame. Identity is content-addressed from the two dep
    // keys (no name), so binding the same field to the same placement dedups.
    struct PlacedFieldDesc
    {
        wz::asset::AssetKey  field_key{};      // the frame-less field (scalar field in v1)
        wz::asset::AssetKey  placement_key{};  // the world-space Placement frame
        wz::asset::AssetType field_type = kAssetTypeScalarField;
    };

    // Returned by create_placed_field(). Wraps the DAG output node key.
    struct PlacedFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_placed_field(). Wraps the ResourceHandle into
    // PlacedFieldTable.
    struct PlacedFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class PlacedFieldAssetModule
    {
    public:
        PlacedFieldAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            PlacedFieldTable& table);

        // Register a PlacedField in the DAG with its field + placement deps. Call
        // commit() and resolve_all() on EngineAssetLibrary before querying handles.
        [[nodiscard]] PlacedFieldAsset create_placed_field(
            const PlacedFieldDesc& desc);

        [[nodiscard]] PlacedFieldHandle get_placed_field(
            const PlacedFieldAsset& asset) const;

        [[nodiscard]] PlacedFieldHandle find_placed_field(
            const PlacedFieldAsset& asset) const;

        [[nodiscard]] const PlacedFieldData* get_placed_field_data(
            PlacedFieldHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        PlacedFieldTable& table_;
    };
}
