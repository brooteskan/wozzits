#pragma once

// engine/assets/audio/audio_clip_bank.h
//
// Runtime data + table for the audio clip bank asset — a CPU-resident table of N
// decoded clips referenced by name/index, loaded either from an explicit list of
// clips or from a directory of WAVs. It is the source side of a bank-backed audio
// renderable: one AudioSource per entity can then trigger many sounds by index.
//
// Ownership mirrors the other CPU asset families: the AssetSystem stores a
// ResourceHandle in the compiled node; the AudioClipBankData lives in
// AudioClipBankTable, owned by EngineAssetLibrary. The bank does not own clip
// samples — each entry holds a ResourceHandle into AudioClipTable, so the clips
// stay shared with any per-clip renderable.

#include <asset/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets {

    // ─── AudioClipBankEntry ───────────────────────────────────────────────────────
    //
    // One named slot in the bank. `clip` is a handle into AudioClipTable (resolved
    // from a clip dependency at compile time); `name_hash` is the FNV-1a/32 of the
    // entry's authored name, for name-keyed lookup without storing the string in
    // the hot path. The parallel `names` vector on the bank keeps the strings for
    // debug/editor display.

    struct AudioClipBankEntry
    {
        uint32_t name_hash = 0;
        wz::asset::ResourceHandle clip{};
    };


    // ─── AudioClipBankData ────────────────────────────────────────────────────────
    //
    // The compiled bank: N resolved clip handles plus their name hashes. `names`
    // parallels `entries` (same order/size) and exists only for debug/editor
    // inspection — playback selects by index or name_hash, never by string.

    struct AudioClipBankData
    {
        std::vector<AudioClipBankEntry> entries;
        std::vector<std::string>        names;  // parallel to entries (debug/editor)

        size_t size() const noexcept { return entries.size(); }

        bool valid() const noexcept
        {
            if (entries.empty()) {
                return false;
            }
            for (const AudioClipBankEntry& e : entries) {
                if (!e.clip.valid()) {
                    return false;
                }
            }
            return true;
        }

        // Linear lookup by name hash. Returns nullptr if no entry matches. Banks
        // are small (a handful to a few dozen clips), so a scan is fine.
        const AudioClipBankEntry* find_by_name_hash(uint32_t name_hash) const noexcept
        {
            for (const AudioClipBankEntry& e : entries) {
                if (e.name_hash == name_hash) {
                    return &e;
                }
            }
            return nullptr;
        }
    };


    // ─── AudioClipBankTable ───────────────────────────────────────────────────────
    //
    // V1: append-only, slot 0 reserved as the invalid sentinel, epoch starts at 1.
    // Identical in shape to AudioClipTable / AudioRenderableTable.

    class AudioClipBankTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        AudioClipBankTable();

        wz::asset::ResourceHandle add(AudioClipBankData bank);
        const AudioClipBankData* get(wz::asset::ResourceHandle handle) const;
        AudioClipBankData* get_mutable_for_tests(wz::asset::ResourceHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t          epoch = 0;
            bool              occupied = false;
            AudioClipBankData bank;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
