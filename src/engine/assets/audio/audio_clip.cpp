// engine/assets/audio/audio_clip.cpp

#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets {

    // ─── AudioClipTable ───────────────────────────────────────────────────────────
    //
    // Slot 0 is permanently reserved as an unoccupied sentinel.
    // ResourceHandle::valid() treats id == 0 as invalid — the same convention used
    // by ScalarFieldTable and other handle types throughout the engine. Starting
    // real slots at id == 1 ensures every handle returned by add() passes valid().

    AudioClipTable::AudioClipTable()
    {
        slots_.emplace_back(); // sentinel: epoch=0, occupied=false
    }

    wz::asset::ResourceHandle AudioClipTable::add(AudioClipData clip)
    {
        // slots_[0] is the sentinel, so the first real slot gets id == 1.
        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.clip = std::move(clip);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeAudioClip,
        };
    }

    const AudioClipData* AudioClipTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.clip;
    }

    AudioClipData* AudioClipTable::get_mutable_for_tests(
        wz::asset::ResourceHandle handle)
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.clip;
    }

    void AudioClipTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back(); // restore sentinel slot 0
    }

} // namespace wz::engine::assets
