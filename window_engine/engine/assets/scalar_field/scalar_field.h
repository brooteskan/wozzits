#pragma once

// engine/assets/scalar_field/scalar_field.h
//
// Runtime data types and table for the scalar field asset.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads.
// The actual ScalarFieldData lives in ScalarFieldTable, owned by
// EngineAssetLibrary. ScalarFieldTable::add() stores the data and
// returns the handle; ScalarFieldTable::get() resolves it back.
//
// ── Separation of concerns ────────────────────────────────────────────────────
//
// ScalarFieldData   — immutable CPU-side sampled field, no GPU knowledge
// ScalarFieldTable  — runtime owner of resolved field data
// ScalarFieldCompileDesc — metadata stored in AssetNode::meta for compile nodes

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets {

    // ─── Field descriptors ────────────────────────────────────────────────────────

    enum class ScalarFieldFormat : uint8_t
    {
        Float32,
    };

    enum class ScalarFieldDomainKind : uint8_t
    {
        Unknown,
        Spatial1D,
        Spatial2D,
        Lookup1D,
        Lookup2D,
        BakedComputation,
    };

    enum class ScalarFieldSampleLayout : uint8_t
    {
        TexelCentered,
    };

    enum class ScalarFieldOrigin : uint8_t
    {
        TopLeft,
    };


    // ─── ScalarFieldData ──────────────────────────────────────────────────────────
    //
    // Immutable, CPU-side sampled scalar function over a declared discrete domain.
    // Storage order: row-major, index rule: x + y * width + z * width * height.
    // Not an image. May represent spatial data, lookup tables, or baked results.

    struct ScalarFieldData
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        ScalarFieldFormat      format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind  domain_kind = ScalarFieldDomainKind::Unknown;
        ScalarFieldSampleLayout layout = ScalarFieldSampleLayout::TexelCentered;
        ScalarFieldOrigin      origin = ScalarFieldOrigin::TopLeft;

        float min_value = 0.0f;
        float max_value = 0.0f;

        std::vector<float> values;

        uint32_t count() const noexcept
        {
            return width * height * depth;
        }

        bool valid() const noexcept
        {
            return width > 0
                && height > 0
                && depth > 0
                && values.size() == static_cast<size_t>(count());
        }

        float at(uint32_t x, uint32_t y = 0, uint32_t z = 0) const
        {
            return values[x + y * width + z * width * height];
        }
    };


    // ─── ScalarFieldCompileDesc ───────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for scalar field compile nodes.
    // Describes how to interpret the raw float32 bytes from the file dependency.

    struct ScalarFieldCompileDesc
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        ScalarFieldFormat     format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };


    // ─── ScalarFieldTable ─────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved scalar field data. The AssetSystem stores only
    // a ResourceHandle; the actual ScalarFieldData lives in this table.
    //
    // V1: append-only. Free-list reuse and resize strategies are deferred.
    // Epoch starts at 1 — epoch 0 in a ResourceHandle always indicates invalid.

    class ScalarFieldTable
    {
    public:
        // Reserves slot 0 as the invalid sentinel so all real handles have id >= 1.
        ScalarFieldTable();

        // Store a resolved field and return a handle to it.
        wz::asset::ResourceHandle add(ScalarFieldData field);

        // Look up a field by handle. Returns nullptr if the handle is stale or
        // out of range. Const — does not modify the table.
        const ScalarFieldData* get(wz::asset::ResourceHandle handle) const;

        // Mutable access for tests only. Not for use in production code paths.
        ScalarFieldData* get_mutable_for_tests(wz::asset::ResourceHandle handle);

        // Release all slots. Invalidates all outstanding handles.
        void destroy();

    private:
        struct Slot
        {
            uint32_t      epoch = 0;
            bool          occupied = false;
            ScalarFieldData field;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
