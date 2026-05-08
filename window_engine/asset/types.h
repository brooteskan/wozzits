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
    // Opaque reference to a runtime-owned resource (CPU table or GPU backend).
    // The asset system does not interpret this value; it only stores and returns it.
    // epoch provides a generation counter so callers can detect stale references.
    //
    // Global null/stale convention (applies to every resource table in the engine):
    //   id == 0     — always null/invalid; no real resource is ever assigned id 0.
    //   epoch == 0  — always invalid/stale; real handles always have epoch >= 1.
    //   valid()     — returns true iff id != 0 && epoch != 0.
    //
    // Every resource table (MeshTable, TextureTable, GaussianSplatCloudTable, …)
    // MUST follow this convention. Slot 0 is permanently reserved as the sentinel.
    //
    // ─── Runtime table contract (V1) ─────────────────────────────────────────────
    //
    // All CPU-owned asset tables in V1 implement the following policy:
    //
    //   Slot 0 sentinel   — reserved at construction; never holds real data.
    //   Epoch start       — first real slot has epoch == 1.
    //   Append-only       — slots are never reused; the table only grows.
    //   destroy()         — releases all memory and invalidates every handle.
    //   No free-list      — individual-slot deletion is deferred to a later version.
    //
    // ScalarFieldTable is the reference implementation. Follow the same pattern
    // for MeshTable, TextureTable, and any other CPU-owned table added in the future.
    //
    // ─── Lifecycle (V1 assumptions) ──────────────────────────────────────────────
    //
    // V1 does not support hot reload or partial invalidation.
    // Assets are resolved once at load time (resolve_all()) and remain live until
    // the owning table is destroyed. Outstanding handles become invalid after
    // destroy(). Re-resolving individual nodes or rebuilding dependents is
    // deferred — do not build future table code that assumes it is possible.
    //
    // ─── CPU vs GPU asset ownership boundary ─────────────────────────────────────
    //
    // CPU asset compilers produce handles into EngineAssetLibrary-owned tables
    // (e.g. ScalarFieldTable). The AssetSystem owns neither the table memory
    // nor the GPU objects — it stores only the ResourceHandle.
    //
    // GPU asset compilers call GPU/device APIs and store backend-owned handles.
    // GPU-side assets (compiled shaders, GPU textures, GPU mesh buffers) are
    // owned by the GPU backend, not by any CPU table.
    //
    // Both flows produce a ResourceHandle. The AssetSystem treats them identically.
    // Mesh and texture assets will eventually have both CPU and GPU forms; keep
    // the ownership boundary explicit when implementing those compilers.

    // id == 0 is the null sentinel.
    // epoch == 0 is invalid/stale.
    // Resource tables must never return id 0 for a live resource.
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
