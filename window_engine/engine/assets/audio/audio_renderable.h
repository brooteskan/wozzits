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
    // The compiled, executable terminal. `clip` is a handle into AudioClipTable
    // (resolved from the source dependency at compile time), so the runtime can
    // fetch the clip's AudioBufferView and start a voice without re-resolving the
    // graph.

    struct AudioRenderableData
    {
        wz::asset::ResourceHandle clip{};  // source clip in AudioClipTable
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;

        bool valid() const noexcept { return clip.valid(); }
    };


    // ─── AudioRenderableCompileDesc ───────────────────────────────────────────────
    //
    // Stored in AssetNode::meta. The source clip is a dependency (not a param);
    // these are the playback params folded into the terminal.

    struct AudioRenderableCompileDesc
    {
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;
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
