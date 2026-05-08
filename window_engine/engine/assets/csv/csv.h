#pragma once

// engine/assets/csv/csv.h
//
// Runtime data types and table for the CSV asset.
//
// ── Design ────────────────────────────────────────────────────────────────────
//
// CSVData is an immutable, CPU-side representation of the raw contents of a
// CSV file. No type inference is performed: every cell is stored as a string.
// Ragged rows (rows with fewer or more columns than the widest row) are accepted
// and stored as-is. max_column_count records the widest row observed.
//
// Header detection is controlled by CSVHeaderMode. Auto mode applies a
// heuristic: row 0 is treated as a header if none of its cells parse as a
// finite floating-point number. ForceHeader and ForceData override this
// unconditionally.
//
// ── Ownership model ───────────────────────────────────────────────────────────
//
// The AssetSystem stores a ResourceHandle in compiled node payloads.
// The actual CSVData lives in CSVTable, owned by EngineAssetLibrary.
// CSVTable::add() stores the data and returns the handle;
// CSVTable::get() resolves it back.

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets {

    // ─── CSVHeaderMode ────────────────────────────────────────────────────────────

    enum class CSVHeaderMode : uint8_t
    {
        Auto,        // heuristic: row 0 is a header if no cell parses as a finite float
        ForceHeader, // always treat row 0 as a header regardless of content
        ForceData,   // always treat all rows as data regardless of content
    };


    // ─── CSVData ──────────────────────────────────────────────────────────────────

    struct CSVData
    {
        bool has_header = false;
        std::vector<std::string>              headers;  // populated only when has_header
        std::vector<std::vector<std::string>> rows;     // data rows only (header excluded)

        uint32_t max_column_count = 0;
        bool     is_ragged        = false;

        bool valid() const noexcept { return true; }
    };


    // ─── CSVCompileDesc ───────────────────────────────────────────────────────────
    //
    // Stored in AssetNode::meta for CSV table compile nodes.

    struct CSVCompileDesc
    {
        CSVHeaderMode header_mode = CSVHeaderMode::Auto;
    };


    // ─── CSVTable ─────────────────────────────────────────────────────────────────
    //
    // Runtime owner of resolved CSV data. The AssetSystem stores only a
    // ResourceHandle; the actual CSVData lives in this table.
    //
    // V1: append-only. Epoch starts at 1 — epoch 0 in a ResourceHandle is invalid.

    class CSVTable
    {
    public:
        CSVTable();

        wz::asset::ResourceHandle add(CSVData data);

        const CSVData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch    = 0;
            bool     occupied = false;
            CSVData  data;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
