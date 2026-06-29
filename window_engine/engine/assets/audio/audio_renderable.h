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
#include <audio/grain_window.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets {

    // Which kind of terminal an AudioRenderableData is — a discriminator so one
    // AudioSource references either flavor identically. Clip = play a source as a
    // mixer Voice (by index/name); GrainCloud = run the source clips through the
    // granular generator (an evolving bed). The mixer-execution path differs; the
    // scene reference does not.
    enum class AudioRenderableKind : uint8_t
    {
        Clip = 0,
        GrainCloud = 1,
    };

    // Authored granular parameters baked into a GrainCloud renderable. The source
    // clips are the renderable's `clips` (the bank); these are everything else the
    // generator needs. The live subset (gain/density/position/pitch) can be driven
    // per frame by a behavior; the rest are set once at start.
    struct GrainCloudParams
    {
        uint32_t max_grains = 32;
        uint32_t seed = 1;

        float gain = 1.0f;
        float density = 0.0f;   // grains/sec
        float position = 0.0f;  // 0..1 playhead
        float pitch = 1.0f;

        float                position_jitter = 0.0f;
        float                pitch_jitter_semitones = 0.0f;
        float                pan_center = 0.0f;
        float                pan_spread = 0.0f;
        float                grain_ms = 100.0f;
        wz::audio::GrainWindow window = wz::audio::GrainWindow::Gaussian;
        float                window_param = 0.4f;

        // Autonomous source-blend LFO (no behavior needed): slowly cycles the
        // per-source selection weights so the cloud crossfades between its clips
        // on its own. blend_rate = cycles/sec (0 = off); blend_depth = 0..1.
        float                blend_rate = 0.0f;
        float                blend_depth = 0.0f;
    };

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
        AudioRenderableKind kind = AudioRenderableKind::Clip;

        std::vector<wz::asset::ResourceHandle> clips;  // source clips in AudioClipTable
        std::vector<uint32_t> clip_name_hashes;        // parallel to clips, may be empty
        uint32_t default_index = 0;
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;

        // Meaningful only when kind == GrainCloud (the clips are its sources).
        GrainCloudParams grain{};

        bool valid() const noexcept
        {
            if (clips.empty()) {
                return false;
            }
            if (kind == AudioRenderableKind::GrainCloud) {
                return clips[0].valid();  // the bank guarantees the rest
            }
            return default_index < clips.size()
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
