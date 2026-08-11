#pragma once

// diagnostics/diagnostic_state.h
//
// The consumer-side aggregate the LoggerService lane folds DiagnosticRecords into
// (#291 / #305 step 4d). Records cross the lane one-per-producer-drain; this table
// dedups them by (id, source) and accumulates the EXACT running total -- the count
// the old D3D12 deny-after-8 filter had to throw away because it stopped storing
// to dedup. "State-side dedup replaces the deny-after-N filter" (#291).
//
// Bounded and fixed-size on purpose: a fixed table keyed by id, no per-record
// allocation, mirroring the 64-entry D3D12 tally cap. New distinct keys past the
// cap are dropped and counted (dropped()), so the reporter can say "N more ids not
// tracked" instead of silently under-counting.
//
// Single-consumer: only the LoggerService lane thread touches an instance (it owns
// the table and drains the channel into it). Not thread-safe by itself; the lock-
// free ring is the cross-lane boundary, this is lane-local state behind it.

#include <cstddef>
#include <cstdint>

#include "diagnostic_record.h"

namespace wz::diag
{
    template <std::size_t Capacity = 64>
    class DiagnosticState
    {
        static_assert(Capacity > 0, "DiagnosticState needs at least one slot");

    public:
        struct Entry
        {
            uint32_t           id       = 0;
            DiagnosticSource   source   = DiagnosticSource::Engine;
            DiagnosticSeverity severity = DiagnosticSeverity::Info;
            uint8_t            category = 0;
            uint16_t           last_pass = 0;   // pass of the most recent occurrence
            uint64_t           count    = 0;    // exact running total (64-bit: never wraps)
            uint64_t           reported = 0;    // count as of the last mark_reported()

            // Occurrences accumulated since this entry was last reported. The
            // reporter logs only entries with unreported() > 0, so a steady repeat
            // is logged once per cadence with its true total, not every frame.
            uint64_t unreported() const { return count - reported; }
        };

        // Fold a record into the table. Finds (id, source) and accumulates its
        // occurrences, refreshing severity/category/last_pass to the latest; or
        // inserts a new entry if there is room. Returns false ONLY when the table
        // is full and this is a new key -- the occurrences are then dropped and
        // counted in dropped(). O(Capacity) linear probe: fine on the cold lane,
        // and Capacity is small (the tally cap).
        bool ingest(const DiagnosticRecord& r)
        {
            for (std::size_t i = 0; i < size_; ++i) {
                Entry& e = entries_[i];
                if (e.id == r.id && e.source == r.source) {
                    e.count += r.occurrences;
                    e.severity  = r.severity;   // stable per id, refreshed anyway
                    e.category  = r.category;
                    e.last_pass = r.pass;
                    return true;
                }
            }
            if (size_ >= Capacity) {
                dropped_ += r.occurrences;
                return false;  // table full -- new key not tracked (see dropped())
            }
            Entry& e = entries_[size_++];
            e.id        = r.id;
            e.source    = r.source;
            e.severity  = r.severity;
            e.category  = r.category;
            e.last_pass = r.pass;
            e.count     = r.occurrences;
            e.reported  = 0;
            return true;
        }

        std::size_t  size()  const { return size_; }
        const Entry& entry(std::size_t i) const { return entries_[i]; }

        // Total occurrences dropped because the table was full when their (new) id
        // first appeared. The reporter surfaces this so a full table never reads as
        // "nothing else happened".
        uint64_t dropped() const { return dropped_; }

        // Occurrences that never REACHED this table: the cross-lane channel was
        // full when the producer published them. Counted on the producer side and
        // folded in here by the lane each cadence (LoggerServiceLane::publish).
        //
        // Kept separate from dropped() because the two losses mean different
        // things and are fixed differently -- a full table means more than
        // Capacity distinct ids, a full channel means the producer outran the
        // consumer's cadence -- and because this one used to be invisible: the
        // publish discarded try_push's result while the reporter advertised its
        // totals as exact. Every other hop in the diagnostics path counts its
        // losses; this closes the last silent one.
        void note_channel_drops(uint64_t occurrences) { channel_dropped_ += occurrences; }
        uint64_t channel_dropped() const { return channel_dropped_; }

        // Snapshot entry i as reported: its unreported() becomes 0 until more
        // occurrences arrive. Called by the reporter after it logs the entry.
        void mark_reported(std::size_t i) { entries_[i].reported = entries_[i].count; }
        void mark_all_reported()
        {
            for (std::size_t i = 0; i < size_; ++i) {
                entries_[i].reported = entries_[i].count;
            }
        }

        void clear()
        {
            size_            = 0;
            dropped_         = 0;
            channel_dropped_ = 0;
        }

    private:
        Entry       entries_[Capacity];
        std::size_t size_            = 0;
        uint64_t    dropped_         = 0;
        uint64_t    channel_dropped_ = 0;
    };

} // namespace wz::diag
