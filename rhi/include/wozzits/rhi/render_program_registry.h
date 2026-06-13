#pragma once

// wozzits/rhi/render_program_registry.h
//
// Render programs as registered data, not as a BuiltinRenderProgram enum.
//
// In the old engine, a render program was an enum member, and the data that
// described it (shader paths, root-constant count, bind strategy) lived
// scattered across switch statements in submit, the pipeline factory, the
// shader-pair map, and a hand-maintained list in the editor. Adding a program
// meant editing all of them; missing one failed silently.
//
// Here a render program is one RenderProgramDesc, registered by name, addressed
// by a Tag. There is no enum to drift against. Resolving an unregistered
// program is a checkable null (nullptr / null Tag), never a silent skip.

#include <wozzits/rhi/tag_registry.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace wz::rhi
{
    inline constexpr size_t kMaxRenderPrograms = 256;

    // The data the scattered switches used to hardcode, gathered in one place.
    // Fields will grow as the design firms up (binding layout, blend/depth
    // state, vertex layout) — but they grow here, in one record, not across N
    // call sites.
    struct RenderProgramDesc
    {
        std::string name;
        std::string vertex_shader;   // path/ref; resolved by the asset bridge later
        std::string pixel_shader;
        uint32_t    root_constant_count = 0;
    };

    class RenderProgramRegistry
    {
    public:
        // Register (or update) a program by name. Returns its Tag, or a null
        // Tag if the registry is full. Re-registering an existing name updates
        // its descriptor and returns the same Tag.
        Tag register_program(RenderProgramDesc desc)
        {
            const Tag existing = tags_.find(desc.name);
            const Tag tag = tags_.acquire(desc.name);
            if (!tag.valid()) {
                return {};  // full
            }
            // acquire() ref-counts a repeat name; drop the extra ref so a
            // re-register stays at one reference rather than leaking counts.
            if (existing.valid()) {
                tags_.release(tag);
            }
            descs_[tag.index] = std::move(desc);
            return tag;
        }

        // Resolve a program by tag. Returns nullptr if the tag is not a
        // currently-registered program.
        [[nodiscard]] const RenderProgramDesc* get(Tag tag) const
        {
            if (is_unregistered(tag)) {
                return nullptr;
            }
            return &descs_[tag.index];
        }

        // Resolve a program by name. Returns a null Tag if not registered —
        // the explicit, checkable miss that replaces a silent enum fallthrough.
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

        // Enumerate registered programs: visitor(Tag, const RenderProgramDesc&).
        // The basis for "realize a pipeline for every program that exists"
        // instead of a hand-maintained list that can fall behind.
        template <typename Visitor>
        void visit(Visitor&& visitor) const
        {
            tags_.visit([&](std::string_view, Tag tag) {
                visitor(tag, descs_[tag.index]);
            });
        }

    private:
        // True when the tag does not name a currently-registered program.
        [[nodiscard]] bool is_unregistered(Tag tag) const
        {
            return !tag.valid()
                || tag.index >= kMaxRenderPrograms
                || tags_.name_of(tag).empty();
        }

        TagRegistry<kMaxRenderPrograms> tags_;
        std::array<RenderProgramDesc, kMaxRenderPrograms> descs_{};
    };
}
