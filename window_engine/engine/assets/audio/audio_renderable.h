#pragma once

// engine/assets/audio/audio_renderable.h
//
// Runtime data + table for the audio renderable asset — the terminal of an audio
// chain and the audio analog of a visual renderable. It is the executable
// descriptor the runtime mixer consumes: which source to play (a resolved
// AudioClip handle) plus how (gain / pitch / looping). V1 is the minimal
// source->out form; filter nodes (gain/biquad/delay) will extend the chain by
// inserting between the source and this terminal.
//
// Ownership mirrors the other CPU asset families: the AssetSystem stores a
// ResourceHandle in the compiled node; the AudioRenderableData lives in
// AudioRenderableTable, owned by EngineAssetLibrary.

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets {

    // ─── AudioRenderableData ──────────────────────────────────────────────────────
    //
    // The compiled, executable terminal. `clips` holds one or more handles into
    // AudioClipTable (resolved from the source dependency at compile time), so the
    // runtime can fetch a clip's AudioBufferView and start a voice without
    // re-resolving the graph. A single-clip terminal stores a one-element vector;
    // a bank-backed terminal stores the whole bank's clips and the behavior PLAY
    // command selects one by index OR by name. `default_index` is the clip
    // auto-play and out-of-range/<0 index requests fall back to.
    //
    // `clip_name_hashes` parallels `clips` (FNV-1a/32 of each clip's name) so a
    // behavior can select a clip by name without the bank — populated by the
    // bank-backed recipe, left empty by the single-clip recipe (which has no
    // names, so name selection there always falls back to default_index).

    struct AudioRenderableData
    {
        std::vector<wz::asset::ResourceHandle> clips;  // source clips in AudioClipTable
        std::vector<uint32_t> clip_name_hashes;        // parallel to clips, may be empty
        uint32_t default_index = 0;
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;

        bool valid() const noexcept
        {
            return !clips.empty()
                && default_index < clips.size()
                && clips[default_index].valid();
        }

        // Clip handle for the given index, falling back to default_index when the
        // index is out of range. Returns an invalid handle only for an empty bank.
        wz::asset::ResourceHandle clip_at(uint32_t index) const noexcept
        {
            if (clips.empty()) {
                return {};
            }
            if (index < clips.size()) {
                return clips[index];
            }
            return clips[default_index < clips.size() ? default_index : 0];
        }

        // Index of the clip with the given name hash, or -1 if none matches (incl.
        // the single-clip case, where clip_name_hashes is empty). Linear scan over
        // a tiny contiguous vector — banks hold a handful to a few dozen clips.
        int index_for_name_hash(uint32_t name_hash) const noexcept
        {
            for (size_t i = 0; i < clip_name_hashes.size(); ++i) {
                if (clip_name_hashes[i] == name_hash) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    };


    // ─── AudioRenderableCompileDesc ───────────────────────────────────────────────
    //
    // Stored in AssetNode::meta. The source (clip or bank) is a dependency (not a
    // param); these are the playback params folded into the terminal.
    // default_index is honored only by the bank-backed recipe.

    struct AudioRenderableCompileDesc
    {
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;
        uint32_t default_index = 0;
    };


    // ─── AudioRenderableTable ─────────────────────────────────────────────────────
    //
    // V1: append-only, slot 0 reserved as the invalid sentinel, epoch starts at 1.

    class AudioRenderableTable
    {
    public:
        AudioRenderableTable();

        wz::asset::ResourceHandle add(AudioRenderableData renderable);
        const AudioRenderableData* get(wz::asset::ResourceHandle handle) const;
        AudioRenderableData* get_mutable_for_tests(wz::asset::ResourceHandle handle);
        void destroy();

    private:
        struct Slot
        {
            uint32_t           epoch = 0;
            bool               occupied = false;
            AudioRenderableData renderable;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
