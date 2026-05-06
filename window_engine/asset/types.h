#pragma once

// wz/asset/types.h
//
// Core value types for the Asset System DAG.
// No dependency on the graph library — pure data definitions.

#include <cstdint>
#include <vector>
#include <variant>
#include <span>
#include <any>

namespace wz::asset {

    // ─── Hash ─────────────────────────────────────────────────────────────────────
    // 128-bit opaque content hash. Produced externally (e.g. Blake3, xxh128).

    struct Hash {
        uint64_t lo = 0;
        uint64_t hi = 0;

        bool operator==(const Hash&) const = default;
    };

    // ─── AssetKey ─────────────────────────────────────────────────────────────────
    // Content-addressed identity for one deterministic build of an asset node.
    // Uniquely identifies the output of a specific (data, schema, compiler, deps)
    // combination — changing any component produces a different key.

    struct AssetKey {
        Hash content_hash;    // hash of raw input bytes
        Hash schema_hash;     // hash of the interpretation schema
        Hash compiler_hash;   // hash encoding compiler version / transform logic
        Hash deps_hash;       // derived from all transitive dependency keys

        bool operator==(const AssetKey&) const = default;
    };

    // std::unordered_map-compatible hasher for AssetKey.
    struct AssetKeyHash {
        size_t operator()(const AssetKey& k) const noexcept {
            auto mix = [](size_t seed, uint64_t v) -> size_t {
                return seed ^ (v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
                };
            size_t h = 0;
            h = mix(h, k.content_hash.lo);   h = mix(h, k.content_hash.hi);
            h = mix(h, k.schema_hash.lo);    h = mix(h, k.schema_hash.hi);
            h = mix(h, k.compiler_hash.lo);  h = mix(h, k.compiler_hash.hi);
            h = mix(h, k.deps_hash.lo);      h = mix(h, k.deps_hash.hi);
            return h;
        }
    };

    // ─── AssetType ────────────────────────────────────────────────────────────────

    enum class AssetType : uint16_t {
        Unknown = 0,
        Mesh = 1,
        Texture = 2,
        Material = 3,
        Shader = 4,
        Audio = 5,
        Font = 6,
        ShaderSource = 7,
    };

    // ─── SchemaID ─────────────────────────────────────────────────────────────────

    struct SchemaID {
        uint64_t value = 0;
        bool operator==(const SchemaID&) const = default;
    };

    struct SchemaIDHash {
        size_t operator()(SchemaID s) const noexcept {
            return std::hash<uint64_t>{}(s.value);
        }
    };

    // ─── AssetStage ───────────────────────────────────────────────────────────────

    enum class AssetStage : uint8_t {
        Source = 0,   // raw bytes from filesystem
        Intermediate = 1,   // decoded, pre-GPU (e.g. parsed mesh data)
        Compiled = 2,   // ready for the renderer
    };

    // ─── ResourceHandle ───────────────────────────────────────────────────────────
    // Opaque reference to a renderer-owned resource.
    // The asset system does not interpret this value; it only stores and returns it.
    // epoch provides a generation counter so callers can detect stale references.
    //
    // Convention:
    //   id == 0 is the null/invalid sentinel.
    //   valid() returns true iff id != 0.
    //   Runtime resource tables must never assign id 0 to a real resource.
    //   Table slot/index 0 should be reserved or unused.
    //
    // This convention is shared by all table-backed resources:
    //   GPU shader tables
    //   scalar field tables
    //   future gaussian splat tables
    //   future wavelet/pipeline/material tables

    struct ResourceHandle
    {
        uint32_t id = 0;
        uint32_t epoch = 0;
        AssetType type = AssetType::Unknown;

        bool valid() const noexcept
        {
            return id != 0 &&
                epoch != 0;
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        bool operator==(const ResourceHandle&) const = default;
    };

    inline constexpr ResourceHandle INVALID_RESOURCE_HANDLE{};


    // ─── AssetIR ──────────────────────────────────────────────────────────────────
    // Intermediate representation: decoded asset data awaiting upload.
    // The byte vector is interpreted per AssetType by the final-stage compiler.

    struct AssetIR {
        AssetType            type = AssetType::Unknown;
        std::vector<uint8_t> data;
    };

    // ─── AssetNode ────────────────────────────────────────────────────────────────
    // Vertex data stored in the Asset DAG.
    //
    // payload reflects the node's current stage:
    //   Source       → vector<uint8_t>   raw file bytes
    //   Intermediate → AssetIR           decoded, type-specific representation
    //   Compiled     → ResourceHandle    live renderer resource (mirrored from cache)
    //
    // Carrier nodes (e.g. file nodes) are an exception: they hold vector<uint8_t>
    // at Compiled stage so their dependents can read the bytes via dep_nodes.
    //
    // meta holds compiler-specific descriptors (e.g. HLSLCompileDesc). It is not
    // interpreted by the asset system — compilers read it via std::any_cast.
    //
    // The dependency list is NOT stored here. It is encoded as DAG edges:
    // parent = prerequisite.

    struct AssetNode {
        AssetKey   key;
        AssetType  type = AssetType::Unknown;
        SchemaID   schema = {};
        AssetStage stage = AssetStage::Source;

        std::variant<
            std::vector<uint8_t>,   // Source, or Compiled carrier
            AssetIR,                // Intermediate
            ResourceHandle          // Compiled (GPU-backed)
        > payload;

        std::any meta;   // optional compiler descriptor — not interpreted by the system
    };

} // namespace wz::asset
