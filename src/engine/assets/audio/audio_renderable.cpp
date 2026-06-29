// engine/assets/audio/audio_renderable.cpp

#include <engine/assets/audio/audio_renderable.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets {

    AudioRenderableTable::AudioRenderableTable()
    {
        slots_.emplace_back(); // sentinel: epoch=0, occupied=false
    }

    wz::asset::ResourceHandle AudioRenderableTable::add(
        AudioRenderableData renderable)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.renderable = std::move(renderable);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeAudioRenderable,
        };
    }

    const AudioRenderableData* AudioRenderableTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.renderable;
    }

    AudioRenderableData* AudioRenderableTable::get_mutable_for_tests(
        wz::asset::ResourceHandle handle)
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.renderable;
    }

    void AudioRenderableTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }

} // namespace wz::engine::assets
