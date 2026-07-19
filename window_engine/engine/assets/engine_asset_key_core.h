#pragma once

// engine/assets/engine_asset_key_core.h
//
// Key construction helpers for the engine asset layer.
//
// The generic AssetSystem is content-addressed: every node is identified by an
// AssetKey encoding four independent hashes. This file makes those decisions
// explicit for the engine-layer asset types and provides factory functions that
// produce correct keys for each recipe variant.
//
// ── Key construction philosophy ───────────────────────────────────────────────
//
// File carrier nodes (Source stage):
//   Editor draft keys are content-aware when the source full path is known.
//   content_hash  — derived from the canonical relative path string
//   schema_hash   — derived from the schema ID constant
//   compiler_hash — zero (carriers perform no transformation)
//   deps_hash     — zero (file nodes have no DAG prerequisites)
//
//   The editor draft key factory uses file bytes when a full path is available,
//   mixed with the canonical path, so same-path file edits invalidate the file
//   source node and downstream compiled assets. The path-only helper remains as
//   a fallback for callers that cannot read file bytes.
//
// Compiled nodes (Intermediate → Compiled stage):
//   content_hash  — derived from recipe-specific parameters (entry, target, …)
//   schema_hash   — derived from the schema ID constant for this recipe format
//   compiler_hash — encodes the compiler version; bump this constant when the
//                   compile function's logic changes to invalidate stale cache
//   deps_hash     — derived from the full AssetKey of the prerequisite node(s);
//                   changing any upstream key propagates through automatically
//
// ── AssetType extensions ──────────────────────────────────────────────────────
//
// The generic AssetType enum lives in wz/asset/types.h and must not be
// modified with engine-specific values. Engine-specific types are declared
// below as typed constants that extend the enum's value space without
// touching the generic library.

#include <asset/types.h>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <file/filesystem.h>

namespace wz::engine::assets {

    // ─── Hashing primitives ───────────────────────────────────────────────────────

    namespace detail {

        // FNV-1a 64-bit over a string view. Constexpr-safe.
        [[nodiscard]] constexpr uint64_t fnv1a_64(std::string_view s) noexcept {
            uint64_t h = 14695981039346656037ull;
            for (const unsigned char c : s) {
                h ^= static_cast<uint64_t>(c);
                h *= 1099511628211ull;
            }
            return h;
        }

        // Combine two 64-bit values using Knuth multiplicative mixing.
        [[nodiscard]] constexpr uint64_t mix64(uint64_t a, uint64_t b) noexcept {
            return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
        }

        // Hash a string into a full 128-bit Hash struct (lo/hi from two independent seeds).
        [[nodiscard]] constexpr wz::asset::Hash hash_str(std::string_view s) noexcept {
            const uint64_t lo = fnv1a_64(s);
            // Vary the hi word with a distinct multiplier so lo == hi is unlikely.
            uint64_t hi = 2166136261ull;
            for (const unsigned char c : s) {
                hi ^= static_cast<uint64_t>(c);
                hi *= 16777619ull;
            }
            return { lo, hi };
        }

        [[nodiscard]] inline wz::asset::Hash hash_bytes(
            std::span<const std::uint8_t> bytes) noexcept
        {
            uint64_t lo = 14695981039346656037ull;
            for (const std::uint8_t c : bytes) {
                lo ^= static_cast<uint64_t>(c);
                lo *= 1099511628211ull;
            }

            uint64_t hi = 2166136261ull;
            for (const std::uint8_t c : bytes) {
                hi ^= static_cast<uint64_t>(c);
                hi *= 16777619ull;
            }

            return { lo, hi };
        }

        // Produce a Hash from a single 64-bit value (hi = 0).
        [[nodiscard]] constexpr wz::asset::Hash hash_u64(uint64_t v) noexcept {
            return { v, 0 };
        }

        // Reinterpret a float as its bit pattern, so a float dial can be folded
        // into a key exactly rather than through a lossy numeric conversion.
        //
        // Lives here rather than in one key factory because every factory with
        // float dials needs it, and two headers each defining their own `inline`
        // copy is a redefinition error the moment one TU includes both.
        [[nodiscard]] inline uint64_t float_bits(float value) noexcept {
            uint32_t bits = 0;
            static_assert(sizeof(float) == sizeof(uint32_t));
            std::memcpy(&bits, &value, sizeof(bits));
            return static_cast<uint64_t>(bits);
        }

        // Fold one AssetKey's full content into a single Hash, for use as deps_hash.
        // All four component hashes are mixed so any upstream change propagates.
        [[nodiscard]] constexpr wz::asset::Hash key_to_dep_hash(
            const wz::asset::AssetKey& k) noexcept
        {
            const uint64_t lo = mix64(mix64(k.content_hash.lo, k.schema_hash.lo),
                mix64(k.compiler_hash.lo, k.deps_hash.lo));
            const uint64_t hi = mix64(mix64(k.content_hash.hi, k.schema_hash.hi),
                mix64(k.compiler_hash.hi, k.deps_hash.hi));
            return { lo, hi };
        }

        // Combine two dep-hashes (for nodes with multiple prerequisites).
        //
        // Dependency ordering policy:
        //   Dependency vectors are ORDERED. combine_dep_hashes() is not commutative —
        //   swapping the order of deps produces a different key.
        //   Key factories that declare multiple dependencies MUST document which
        //   index corresponds to which input (e.g. "dep[0] = source file").
        //
        //   If a particular key factory's deps are semantically unordered (e.g. a
        //   material whose texture bindings have no positional meaning), sort the dep
        //   keys before folding them and document this explicitly. The default
        //   assumption everywhere is that order is significant.
        [[nodiscard]] constexpr wz::asset::Hash combine_dep_hashes(
            wz::asset::Hash a, wz::asset::Hash b) noexcept
        {
            return { mix64(a.lo, b.lo), mix64(a.hi, b.hi) };
        }

        [[nodiscard]] inline std::string canonical_asset_path(const wz::fs::Path& path)
        {
            std::string s = path;

            for (char& c : s) {
                if (c == '\\')
                    c = '/';
            }

            while (!s.empty() && s.front() == '/')
                s.erase(s.begin());

            return s;
        }
    } // namespace detail












} // namespace wz::engine::assets
