#pragma once

// wozzits/rhi/tag_registry.h
//
// Runtime name -> handle registry. This is the structural answer to the
// "open identity enum" problem: instead of a compile-time enum that many
// call sites must each switch on (and can silently fail to handle), identity
// is registered at runtime by name and addressed by an opaque Tag handle.
//
// Studied from O3DE Atom's RHI::TagRegistry, rewritten as plain C++ with no
// framework dependency. Key properties preserved:
//   - Tags are acquired by name and reference-counted (shared ownership).
//   - find() resolves without taking ownership; a miss is a *null Tag*, never
//     undefined behavior or a silent fallthrough.
//   - Bounded capacity (fixed array); acquire() returns a null Tag when full.
//   - Names are case-sensitive.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace wz::rhi
{
    // Opaque handle into a TagRegistry. Default-constructed Tag is the null
    // (invalid) tag. Cheap to copy; compare for routing decisions.
    struct Tag
    {
        static constexpr uint16_t kInvalidIndex = 0xFFFFu;

        uint16_t index = kInvalidIndex;

        [[nodiscard]] bool valid() const noexcept
        {
            return index != kInvalidIndex;
        }

        friend bool operator==(Tag, Tag) = default;
    };

    template <size_t MaxTags>
    class TagRegistry
    {
    public:
        static_assert(MaxTags < Tag::kInvalidIndex,
            "MaxTags must leave room for the null Tag sentinel");

        // Acquire a tag for a name. If the name is already registered, its
        // reference count is incremented and the existing Tag is returned.
        // Returns a null Tag if the name is empty or the registry is full.
        [[nodiscard]] Tag acquire(std::string_view name)
        {
            if (name.empty()) {
                return {};
            }

            size_t first_empty = MaxTags;
            for (size_t i = 0; i < MaxTags; ++i) {
                Entry& entry = entries_[i];
                if (entry.ref_count == 0) {
                    if (first_empty == MaxTags) {
                        first_empty = i;
                    }
                }
                else if (entry.name == name) {
                    ++entry.ref_count;
                    return Tag{ static_cast<uint16_t>(i) };
                }
            }

            if (first_empty == MaxTags) {
                return {};  // registry full
            }

            Entry& slot = entries_[first_empty];
            slot.name.assign(name);
            slot.ref_count = 1;
            ++allocated_;
            return Tag{ static_cast<uint16_t>(first_empty) };
        }

        // Resolve a name to a Tag without taking ownership. Returns a null Tag
        // if the name is not currently registered. This is the checkable miss
        // that replaces a silent enum fallthrough.
        [[nodiscard]] Tag find(std::string_view name) const
        {
            if (name.empty()) {
                return {};
            }
            for (size_t i = 0; i < MaxTags; ++i) {
                const Entry& entry = entries_[i];
                if (entry.ref_count > 0 && entry.name == name) {
                    return Tag{ static_cast<uint16_t>(i) };
                }
            }
            return {};
        }

        // Release one reference to a tag. When the count reaches zero the slot
        // is freed for reuse.
        void release(Tag tag)
        {
            if (!tag.valid() || tag.index >= MaxTags) {
                return;
            }
            Entry& entry = entries_[tag.index];
            if (entry.ref_count == 0) {
                return;
            }
            if (--entry.ref_count == 0) {
                entry.name.clear();
                --allocated_;
            }
        }

        // Reverse lookup: the name registered for a tag, or empty if the tag
        // is not currently allocated.
        [[nodiscard]] std::string_view name_of(Tag tag) const
        {
            if (!tag.valid() || tag.index >= MaxTags) {
                return {};
            }
            const Entry& entry = entries_[tag.index];
            return entry.ref_count > 0
                ? std::string_view{ entry.name }
                : std::string_view{};
        }

        [[nodiscard]] size_t allocated_count() const noexcept
        {
            return allocated_;
        }

        // Enumerate every allocated tag: visitor(std::string_view name, Tag).
        // "What identities exist" is an introspectable query, not knowledge
        // scattered across switch statements.
        template <typename Visitor>
        void visit(Visitor&& visitor) const
        {
            for (size_t i = 0; i < MaxTags; ++i) {
                const Entry& entry = entries_[i];
                if (entry.ref_count > 0) {
                    visitor(std::string_view{ entry.name },
                        Tag{ static_cast<uint16_t>(i) });
                }
            }
        }

        // Free every slot at once. After this, all previously-issued Tags are
        // unregistered (find/name_of miss) and the registry is empty. For
        // wholesale replacement (e.g. an asset-graph swap that retires the
        // entire registered set), not for incremental release.
        //
        // INVARIANT: callers must discard every Tag obtained before clear().
        // Tags are bare slot indices (not generational), so a stale Tag reused
        // after clear() + new acquires would silently address a recycled slot.
        void clear() noexcept
        {
            for (Entry& entry : entries_) {
                entry.name.clear();
                entry.ref_count = 0;
            }
            allocated_ = 0;
        }

    private:
        struct Entry
        {
            std::string name;
            size_t ref_count = 0;
        };

        std::array<Entry, MaxTags> entries_{};
        size_t allocated_ = 0;
    };
}
