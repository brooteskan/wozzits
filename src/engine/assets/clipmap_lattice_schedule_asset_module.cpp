// src/engine/assets/clipmap_lattice_schedule_asset_module.cpp

#include <engine/assets/clipmap_lattice_schedule_asset_module.h>

#include <engine/assets/key_factories/clipmap_lattice_schedule.h>
#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    ClipmapLatticeScheduleAssetModule::ClipmapLatticeScheduleAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        ClipmapLatticeScheduleTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    ClipmapLatticeScheduleAsset
    ClipmapLatticeScheduleAssetModule::create_clipmap_lattice_schedule(
        const ClipmapLatticeScheduleDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("clipmap lattice schedule asset has empty name");
            return {};
        }
        if (desc.field_key == wz::asset::AssetKey{}) {
            logger_.error(
                "clipmap lattice schedule asset has no height field dependency");
            return {};
        }

        // The typed meta the compiler prefers over a ParamBlock — the SAME
        // struct the lattice mesh recipe decodes its dials into.
        ClipmapLatticePhysicalParams physical{};
        physical.world_extent = desc.world_extent;
        physical.horizon = desc.horizon;
        physical.triangle_budget = desc.triangle_budget;

        const wz::asset::AssetKey key =
            make_clipmap_lattice_schedule_key(
                desc.name,
                desc.field_key,
                desc.world_extent,
                desc.horizon,
                desc.triangle_budget);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeClipmapLatticeSchedule;
        node.schema = kClipmapLatticeScheduleSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = physical;

        // One dep: the height field, matching the compiler's single required
        // port and make_clipmap_lattice_schedule_key's deps_hash.
        system_.register_asset(std::move(node), { desc.field_key });

        return ClipmapLatticeScheduleAsset{ .output = key };
    }

    ClipmapLatticeScheduleHandle
    ClipmapLatticeScheduleAssetModule::get_clipmap_lattice_schedule(
        const ClipmapLatticeScheduleAsset& asset) const
    {
        ClipmapLatticeScheduleHandle out = find_clipmap_lattice_schedule(asset);
        if (asset.valid() && !out.valid()) {
            logger_.error("clipmap lattice schedule asset handle not found");
        }
        return out;
    }

    ClipmapLatticeScheduleHandle
    ClipmapLatticeScheduleAssetModule::find_clipmap_lattice_schedule(
        const ClipmapLatticeScheduleAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        ClipmapLatticeScheduleHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        return out;
    }

    const ClipmapLatticeScheduleData*
    ClipmapLatticeScheduleAssetModule::get_clipmap_lattice_schedule_data(
        ClipmapLatticeScheduleHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
