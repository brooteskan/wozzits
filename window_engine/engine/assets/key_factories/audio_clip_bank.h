#pragma once

// engine/assets/key_factories/audio_clip_bank.h
//
// Key factory for audio clip bank recipe nodes. Identity = the ordered list of
// source clip dependencies (deps_hash) + the per-entry name hashes
// (content_hash). Two banks over the same clips but different names — or the
// same clips in a different order — are distinct assets.
//
// Dependency ordering is significant: the clip keys are folded in order (mirrors
// the engine's default "order matters" dep policy), so the bank's index space
// matches the authored clip order.

#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

#include <cstdint>
#include <span>

namespace wz::engine::assets {

    // clip_keys  — ordered keys of the kAssetTypeAudioClip dependency nodes.
    // name_hashes — FNV-1a/32 of each entry's name, parallel to clip_keys.
    [[nodiscard]] inline wz::asset::AssetKey make_audio_clip_bank_key(
        std::span<const wz::asset::AssetKey> clip_keys,
        std::span<const uint32_t>            name_hashes) noexcept
    {
        // Fold the ordered clip keys into a single dep hash (non-commutative).
        wz::asset::Hash deps{};
        for (const wz::asset::AssetKey& k : clip_keys) {
            deps = detail::combine_dep_hashes(
                deps, detail::key_to_dep_hash(k));
        }

        // Fold the per-entry name hashes into the content hash so renaming an
        // entry (without changing the clip) still yields a distinct key.
        uint64_t name_lo = 14695981039346656037ull;
        uint64_t name_hi = 2166136261ull;
        for (const uint32_t h : name_hashes) {
            name_lo = detail::mix64(name_lo, static_cast<uint64_t>(h));
            name_hi = detail::mix64(
                name_hi, static_cast<uint64_t>(h) + 0x9e3779b9ull);
        }

        return wz::asset::AssetKey{
            .content_hash = { name_lo, name_hi },
            .schema_hash = detail::hash_u64(kAudioClipBankFromClipsSchema.value),
            .compiler_hash = detail::hash_u64(kAudioClipBankCompilerVersion),
            .deps_hash = deps,
        };
    }

    // Key for a directory-import bank node. Identity = the authored directory path
    // (canonical) + the recursive flag (content_hash). No deps — the compiler is
    // the I/O boundary that scans the folder. NOTE: like a path-only file carrier,
    // identity does NOT fold the folder's current contents, so adding/removing a
    // WAV without changing the path does not change the key (matches make_file_key;
    // a fresh compile re-scans regardless).
    [[nodiscard]] inline wz::asset::AssetKey make_audio_clip_bank_from_directory_key(
        std::string_view canonical_directory,
        bool             recursive) noexcept
    {
        const wz::asset::Hash path_hash = detail::hash_str(canonical_directory);
        return wz::asset::AssetKey{
            .content_hash = {
                detail::mix64(path_hash.lo, recursive ? 1ull : 0ull),
                path_hash.hi,
            },
            .schema_hash =
                detail::hash_u64(kAudioClipBankFromDirectorySchema.value),
            .compiler_hash =
                detail::hash_u64(kAudioClipBankFromDirectoryCompilerVersion),
            .deps_hash = {},
        };
    }

} // namespace wz::engine::assets
