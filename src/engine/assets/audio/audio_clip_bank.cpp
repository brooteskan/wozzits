// engine/assets/audio/audio_clip_bank.cpp

#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets {

    // ─── AudioClipBankTable ───────────────────────────────────────────────────────
    //
    // Slot 0 is permanently reserved as an unoccupied sentinel, mirroring
    // AudioClipTable / AudioRenderableTable so every handle returned by add()
    // passes ResourceHandle::valid() (id >= 1, epoch >= 1).

    AudioClipBankTable::AudioClipBankTable()
    {
        slots_.emplace_back(); // sentinel: epoch=0, occupied=false
    }

    wz::asset::ResourceHandle AudioClipBankTable::add(AudioClipBankData bank)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.bank = std::move(bank);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeAudioBank,
        };
    }

    const AudioClipBankData* AudioClipBankTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.bank;
    }

    AudioClipBankData* AudioClipBankTable::get_mutable_for_tests(
        wz::asset::ResourceHandle handle)
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.bank;
    }

    void AudioClipBankTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back(); // restore sentinel slot 0
    }

} // namespace wz::engine::assets
