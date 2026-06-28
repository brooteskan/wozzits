#pragma once

// engine/assets/placement_asset_module.h

#include <asset/system.h>
#include <engine/assets/placement/placement.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    // Authoring recipe for a Placement frame. Mirrors the collision/scalar-field
    // module create-from-desc pattern. name contributes to the asset key so
    // differently-named frames with identical parameters are distinct assets.
    struct PlacementDesc
    {
        std::string name;
        float origin[3]{ 0.0f, 0.0f, 0.0f };  // origin[1] is derived from base_height
        float extent[3]{ 1.0f, 1.0f, 1.0f };  // extent.xz footprint; extent.y vertical scale
        float base_height = 0.0f;
    };

    // Returned by create_placement(). Wraps the DAG output node key.
    struct PlacementAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_placement(). Wraps the ResourceHandle into PlacementTable.
    struct PlacementHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class PlacementAssetModule
    {
    public:
        PlacementAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            PlacementTable& table);

        // Register a Placement frame in the DAG. Call commit() and resolve_all()
        // on EngineAssetLibrary before querying handles.
        [[nodiscard]] PlacementAsset create_placement(const PlacementDesc& desc);

        [[nodiscard]] PlacementHandle get_placement(
            const PlacementAsset& asset) const;

        [[nodiscard]] PlacementHandle find_placement(
            const PlacementAsset& asset) const;

        [[nodiscard]] const PlacementData* get_placement_data(
            PlacementHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        PlacementTable& table_;
    };
}
