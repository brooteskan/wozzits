#pragma once

// wozzits/rhi/constants_layout.h
//
// O3DE-style shader constant layout: authoring/declaration code names each
// constant range with a Tag, then resolves those names once into byte
// intervals. The hot path binds a packed byte payload by interval, not by
// repeatedly looking up strings or Tags.

#include <wozzits/rhi/tag_registry.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wz::rhi
{
    inline constexpr size_t kMaxConstantSemantics = 128;
    using ConstantSemanticRegistry = TagRegistry<kMaxConstantSemantics>;

    struct ConstantInterval
    {
        uint32_t byte_offset = 0;
        uint32_t byte_size = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return byte_size != 0;
        }

        [[nodiscard]] uint32_t dword_offset() const noexcept
        {
            return byte_offset / sizeof(uint32_t);
        }

        [[nodiscard]] uint32_t dword_count() const noexcept
        {
            return byte_size / sizeof(uint32_t);
        }
    };

    class ConstantsLayout
    {
    public:
        // Append a named constant interval to the packed payload. Returns false
        // for null Tags, zero-sized ranges, unaligned ranges, or duplicates.
        bool append(Tag semantic, uint32_t byte_size)
        {
            if (!semantic.valid()
                || byte_size == 0
                || byte_size % sizeof(uint32_t) != 0
                || find(semantic))
            {
                return false;
            }

            entries_.push_back(Entry{
                semantic,
                ConstantInterval{ total_byte_size_, byte_size },
            });
            total_byte_size_ += byte_size;
            return true;
        }

        [[nodiscard]] std::optional<ConstantInterval> find(
            Tag semantic) const noexcept
        {
            if (!semantic.valid()) {
                return std::nullopt;
            }
            for (const Entry& entry : entries_) {
                if (entry.semantic == semantic) {
                    return entry.interval;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] uint32_t byte_size() const noexcept
        {
            return total_byte_size_;
        }

        [[nodiscard]] uint32_t dword_count() const noexcept
        {
            return total_byte_size_ / sizeof(uint32_t);
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return entries_.size();
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return entries_.empty();
        }

        [[nodiscard]] uint64_t hash() const noexcept
        {
            uint64_t h = 1469598103934665603ull;
            hash_combine(h, static_cast<uint64_t>(total_byte_size_));
            hash_combine(h, static_cast<uint64_t>(entries_.size()));
            for (const Entry& entry : entries_) {
                hash_combine(h, static_cast<uint64_t>(entry.semantic.index));
                hash_combine(h, static_cast<uint64_t>(entry.interval.byte_offset));
                hash_combine(h, static_cast<uint64_t>(entry.interval.byte_size));
            }
            return h;
        }

    private:
        struct Entry
        {
            Tag semantic{};
            ConstantInterval interval{};
        };

        static void hash_combine(uint64_t& h, uint64_t v) noexcept
        {
            h ^= v;
            h *= 1099511628211ull;
        }

        std::vector<Entry> entries_;
        uint32_t total_byte_size_ = 0;
    };
}
