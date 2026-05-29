#include <engine/assets/light/light.h>

#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    namespace
    {
        template <typename Slot>
        bool valid_slot(
            const std::vector<Slot>& slots,
            wz::asset::ResourceHandle handle)
        {
            return handle.valid()
                && handle.id < static_cast<uint32_t>(slots.size())
                && slots[handle.id].occupied
                && slots[handle.id].epoch == handle.epoch;
        }
    }

    DirectLightTable::DirectLightTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle DirectLightTable::add(DirectLightData light)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.light = light;
        slots_.push_back(slot);

        return wz::asset::ResourceHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1),
            .epoch = slots_.back().epoch,
            .type = kAssetTypeDirectLight,
        };
    }

    const DirectLightData* DirectLightTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!valid_slot(slots_, handle)) {
            return nullptr;
        }
        return &slots_[handle.id].light;
    }

    void DirectLightTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }

    AmbientLightingTable::AmbientLightingTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle AmbientLightingTable::add(
        AmbientLightingData lighting)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.lighting = lighting;
        slots_.push_back(slot);

        return wz::asset::ResourceHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1),
            .epoch = slots_.back().epoch,
            .type = kAssetTypeAmbientLighting,
        };
    }

    const AmbientLightingData* AmbientLightingTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!valid_slot(slots_, handle)) {
            return nullptr;
        }
        return &slots_[handle.id].lighting;
    }

    void AmbientLightingTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }

    wz::scene::LightType direct_light_kind_to_scene_light_type(
        DirectLightKind kind) noexcept
    {
        switch (kind) {
        case DirectLightKind::Directional:
            return wz::scene::LightType::Directional;
        case DirectLightKind::Point:
            return wz::scene::LightType::Point;
        case DirectLightKind::Spot:
            return wz::scene::LightType::Spot;
        }
        return wz::scene::LightType::Point;
    }

} // namespace wz::engine::assets
