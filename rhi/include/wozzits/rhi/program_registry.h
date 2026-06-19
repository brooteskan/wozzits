#pragma once

// wozzits/rhi/program_registry.h
//
// Generic "named programs as data" registry. Render programs and compute
// programs are registered, resolved, and enumerated identically — only the
// stored Desc differs — so the logic lives here once. A new program *kind*
// (render, compute, future mesh/ray) is a new Desc + alias, not a new enum
// member that call sites must switch on.
//
// Requirements on Desc: a `std::string name` member.
//
// A missing program resolves to a null Tag / nullptr — the checkable miss that
// replaces a silent enum fallthrough.

#include <wozzits/rhi/tag_registry.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace wz::rhi
{
    template <typename Desc, size_t MaxPrograms>
    class ProgramRegistry
    {
    public:
        // Register (or update) a program by name. Returns its Tag, or a null
        // Tag if the registry is full. Re-registering an existing name updates
        // its descriptor and returns the same Tag.
        Tag register_program(Desc desc)
        {
            const Tag existing = tags_.find(desc.name);
            const Tag tag = tags_.acquire(desc.name);
            if (!tag.valid()) {
                return {};
            }
            if (existing.valid()) {
                tags_.release(tag);   // keep a re-register at one reference
            }
            descs_[tag.index] = std::move(desc);
            return tag;
        }

        [[nodiscard]] const Desc* get(Tag tag) const
        {
            if (is_unregistered(tag)) {
                return nullptr;
            }
            return &descs_[tag.index];
        }

        [[nodiscard]] Tag find(std::string_view name) const
        {
            return tags_.find(name);
        }

        [[nodiscard]] std::string_view name_of(Tag tag) const
        {
            return tags_.name_of(tag);
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return tags_.allocated_count();
        }

        template <typename Visitor>
        void visit(Visitor&& visitor) const
        {
            tags_.visit([&](std::string_view, Tag tag) {
                visitor(tag, descs_[tag.index]);
            });
        }

        // Drop every registered program: free the name slots AND reset the
        // descriptors so any heap they own (e.g. shader bytecode) is released.
        // After this, size() == 0 and get()/find() miss for every prior Tag.
        // For wholesale replacement (asset-graph swap), not incremental edits.
        //
        // INVARIANT: discard every Tag obtained before clear() (see
        // TagRegistry::clear) — a stale Tag reused afterwards addresses a
        // recycled slot.
        void clear()
        {
            tags_.clear();
            for (Desc& desc : descs_) {
                desc = Desc{};
            }
        }

    private:
        [[nodiscard]] bool is_unregistered(Tag tag) const
        {
            return !tag.valid()
                || tag.index >= MaxPrograms
                || tags_.name_of(tag).empty();
        }

        TagRegistry<MaxPrograms> tags_;
        std::array<Desc, MaxPrograms> descs_{};
    };
}
