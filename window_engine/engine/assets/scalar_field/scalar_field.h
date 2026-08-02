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
// ScalarFieldData                — immutable CPU-side sampled field, no GPU knowledge
// ScalarFieldTable               — runtime owner of resolved field data
// ScalarFieldCompileDesc         — metadata for file-backed compile nodes
// ProceduralScalarFieldCompileDesc — metadata for procedural compile nodes
//   (name is an identity input to the key factory, not a compile input,
//    so it is intentionally absent from both compile descriptors)

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

    // ─── Descriptor range checks ──────────────────────────────────────────────────
    //
    // The disk cache stores these four descriptors as raw uint8 and used to
    // static_cast them straight back with no range check, so a single flipped
    // byte produced an out-of-range enum that passed every other validation in
    // the loader (magic, format version, compiler version, stored key, and the
    // sample-count bound all cover other regions) and then reached the switch
    // statements downstream. Measured: a flip of the format byte loaded a field
    // reporting format == 1 when Float32 == 0 is the only member.
    //
    // WHEN YOU APPEND AN ENUMERATOR, UPDATE THE MATCHING PREDICATE HERE. They
    // live beside the enums rather than in the deserializer so the pairing is
    // visible at the point of change. Under-accepting is the safe direction: a
    // cache entry carrying an enumerator this build does not know is rejected,
    // and the asset simply recompiles.

    [[nodiscard]] constexpr bool valid_scalar_field_format(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(ScalarFieldFormat::Float32);
    }

    [[nodiscard]] constexpr bool valid_scalar_field_domain_kind(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(ScalarFieldDomainKind::BakedComputation);
    }

    [[nodiscard]] constexpr bool valid_scalar_field_sample_layout(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(ScalarFieldSampleLayout::TexelCentered);
    }

    [[nodiscard]] constexpr bool valid_scalar_field_origin(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(ScalarFieldOrigin::TopLeft);
    }

    // ─── ScalarFieldGenerator ─────────────────────────────────────────────────────
    //
    // Identifies the procedural generation algorithm for a procedural scalar field
    // recipe node. Used as an ordinal in the key factory so different generators
    // produce distinct AssetKeys even with identical dimensions and parameters.

    enum class ScalarFieldGenerator : uint8_t
    {
        GradientX,       // x / (width  - 1), left=0 right=1
        GradientY,       // y / (height - 1), top=0  bottom=1
        RadialGradient,  // normalized distance from center; center=0, corners=1
        Checkerboard,    // alternating 0 / amplitude; frequency used as cell size
        SineWaves,       // (0.5 + 0.5 * sin(u * freq * 2π)) * amplitude
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

        // 64-bit deliberately: the three extents are each uint32, so their
        // product overflows a uint32 long before it stops being expressible.
        // When this returned uint32, `65536 x 65536 x 1` wrapped to 0 and
        // valid() accepted a field declaring four billion samples while
        // holding none -- at() then indexed an empty vector. Every caller
        // must compare against values.size() (a size_t) at this width.
        uint64_t count() const noexcept
        {
            return static_cast<uint64_t>(width)
                * static_cast<uint64_t>(height)
                * static_cast<uint64_t>(depth);
        }

        bool valid() const noexcept
        {
            return width > 0
                && height > 0
                && depth > 0
                && static_cast<uint64_t>(values.size()) == count();
        }

        float at(uint32_t x, uint32_t y = 0, uint32_t z = 0) const
        {
            return values[x + y * width + z * width * height];
        }
    };


    // ─── ScalarFieldCompileDesc ───────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for file-backed scalar field compile nodes.
    // Describes how to interpret the raw float32 bytes from the file dependency.

    struct ScalarFieldCompileDesc
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        ScalarFieldFormat     format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };


    // ─── GaeaR32ScalarFieldCompileDesc ────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for Gaea .r32 scalar field compile nodes.
    // Carries no dimensions: the compiler derives a square grid from the file's
    // sample count (Gaea's documented convention). domain_kind is the only
    // authored knob. Expects exactly one kRawFileSchema dependency (the .r32).

    struct GaeaR32ScalarFieldCompileDesc
    {
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };


    // ─── ProceduralScalarFieldCompileDesc ─────────────────────────────────────────
    //
    // Stored in AssetNode::meta for procedural scalar field compile nodes.
    // The procedural compiler generates values from these parameters alone —
    // no file dependency is present in dep_nodes.
    //
    // name is intentionally absent: it is an identity input to the key factory
    // (so differently-named fields with identical parameters are distinct assets),
    // but the compiler does not need it to generate values.

    struct ProceduralScalarFieldCompileDesc
    {
        uint32_t width = 0;
        uint32_t height = 1;
        uint32_t depth = 1;

        ScalarFieldGenerator  generator = ScalarFieldGenerator::GradientX;

        float frequency = 1.0f;
        float amplitude = 1.0f;

        ScalarFieldFormat     format = ScalarFieldFormat::Float32;
        ScalarFieldDomainKind domain_kind = ScalarFieldDomainKind::Spatial2D;
    };


    // ─── TerrainScalarFieldCompileDesc ────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for procedural TERRAIN scalar field nodes
    // (kScalarFieldTerrainSchema). Fractal noise shaped into a landscape, for
    // authoring a far-horizon layer without leaving the editor — and for standing
    // in while the real hand-authored field is still being built.
    //
    // ── Two rules the dials follow ───────────────────────────────────────────────
    //
    // 1. The output is normalised to exactly [0, 1]. Vertical scale lives in the
    //    Placement (extent[1] is world Y per unit field value), so this recipe
    //    never speaks in metres. That is also what makes a procedural field and a
    //    Gaea .r32 field interchangeable under one Placement.
    //
    // 2. No dial is in world units. The world footprint lives in the Placement
    //    too, and a generator that also took metres could silently disagree with
    //    it. So spatial dials are FIELD-RELATIVE: ridge_count is "how many ridges
    //    span the field", the basin dials are fractions of the half-width. An
    //    author converts once, in their head, and the asset can never lie.
    //
    // name is intentionally absent, as in ProceduralScalarFieldCompileDesc: it is
    // an identity input to the key factory, not a compile input.

    struct TerrainScalarFieldCompileDesc
    {
        // Square by construction. A clipmap footprint is square, and a width !=
        // height field under a square Placement silently stretches the world.
        uint32_t resolution = 1024;

        // How many major ridges span the field. Pure grid-space, so it cannot
        // drift out of step with the Placement the way a wavelength in metres
        // could — and the author can count them on screen to check.
        float ridge_count = 6.0f;

        // 0 = plain fBm (rounded hills), 1 = ridged fBm (sharp alpine crests).
        // The most visible dial in silhouette, which is what a far field is for.
        float ridginess = 0.6f;

        // Per-octave amplitude falloff (persistence). 0 = smooth rolling, 1 =
        // every octave contributes equally and the surface is harsh.
        float roughness = 0.5f;

        // Octave count. Buys near-field crispness; most of it is sub-pixel at
        // horizon distance, so it matters less here than an author expects.
        uint32_t detail = 6;

        uint32_t seed = 0;

        // ── Basin ────────────────────────────────────────────────────────────────
        //
        // Flattens the middle of the field toward the valley floor so a far layer
        // can ring a near one without erupting through it. Radii are fractions of
        // the field HALF-width: 1.0 reaches the edge midpoint, ~1.414 the corners.
        //
        // Multiplicative, so it pulls toward the low ground rather than digging a
        // hole — lerp(h, 0, depth) is the same thing and keeps a hint of the
        // terrain's character at partial depth.
        float basin_radius  = 0.35f;   // flat out to here
        float basin_falloff = 0.25f;   // ramp width from floor to full relief
        float basin_depth   = 1.0f;    // 1 = flat, 0 = basin disabled

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
