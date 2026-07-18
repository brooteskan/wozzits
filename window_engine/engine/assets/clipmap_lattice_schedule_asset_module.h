#pragma once

// engine/assets/clipmap_lattice_schedule_asset_module.h

#include <asset/system.h>
#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    // Authoring recipe for a ClipmapLatticeSchedule: the authored dials plus the
    // height field whose resolution completes them. name contributes to the
    // asset key so differently-named schedules with identical dials over the
    // same field are distinct assets. Defaults mirror the compiler's declared
    // parameter defaults.
    struct ClipmapLatticeScheduleDesc
    {
        std::string name;
        // The height field the schedule descends from. Only its resolution is
        // read; the samples are never touched.
        wz::asset::AssetKey field_key{};
        float    world_extent = 256.0f;     // metres spanned by the whole field
        float    horizon = 128.0f;          // metres the lattice must reach
        uint32_t triangle_budget = 200000u; // target ceiling for the whole lattice
    };

    // Returned by create_clipmap_lattice_schedule(). Wraps the DAG output node key.
    struct ClipmapLatticeScheduleAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_clipmap_lattice_schedule(). Wraps the ResourceHandle into
    // ClipmapLatticeScheduleTable.
    struct ClipmapLatticeScheduleHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class ClipmapLatticeScheduleAssetModule
    {
    public:
        ClipmapLatticeScheduleAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            ClipmapLatticeScheduleTable& table);

        // Register a ClipmapLatticeSchedule in the DAG with its height-field dep.
        // Call commit() and resolve_all() on EngineAssetLibrary before querying
        // handles.
        [[nodiscard]] ClipmapLatticeScheduleAsset
        create_clipmap_lattice_schedule(
            const ClipmapLatticeScheduleDesc& desc);

        [[nodiscard]] ClipmapLatticeScheduleHandle
        get_clipmap_lattice_schedule(
            const ClipmapLatticeScheduleAsset& asset) const;

        [[nodiscard]] ClipmapLatticeScheduleHandle
        find_clipmap_lattice_schedule(
            const ClipmapLatticeScheduleAsset& asset) const;

        [[nodiscard]] const ClipmapLatticeScheduleData*
        get_clipmap_lattice_schedule_data(
            ClipmapLatticeScheduleHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        ClipmapLatticeScheduleTable& table_;
    };
}
