#pragma once

// wozzits/rhi/draw_list_tag.h
//
// Pass routing is open: "depth", "forward", "shadow", "debug", and future
// passes are registered Tags. Surface/sort class is a separate closed enum.

#include <wozzits/rhi/tag_registry.h>

#include <cstdint>

namespace wz::rhi
{
    enum class RenderDomain : uint8_t
    {
        Opaque,
        Transparent,
        Splat,
    };

    inline constexpr size_t kMaxDrawListTags = 64;
    using DrawListTag = Tag;
    using DrawListTagRegistry = TagRegistry<kMaxDrawListTags>;

    struct DrawListMask
    {
        uint64_t bits = 0;

        [[nodiscard]] static DrawListMask from(DrawListTag tag) noexcept
        {
            DrawListMask mask;
            mask.add(tag);
            return mask;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return bits == 0;
        }

        bool add(DrawListTag tag) noexcept
        {
            if (!bit_valid(tag)) {
                return false;
            }
            bits |= bit(tag);
            return true;
        }

        bool remove(DrawListTag tag) noexcept
        {
            if (!bit_valid(tag)) {
                return false;
            }
            bits &= ~bit(tag);
            return true;
        }

        [[nodiscard]] bool contains(DrawListTag tag) const noexcept
        {
            return bit_valid(tag) && (bits & bit(tag)) != 0;
        }

        [[nodiscard]] bool intersects(DrawListMask other) const noexcept
        {
            return (bits & other.bits) != 0;
        }

    private:
        [[nodiscard]] static bool bit_valid(DrawListTag tag) noexcept
        {
            return tag.valid() && tag.index < kMaxDrawListTags;
        }

        [[nodiscard]] static uint64_t bit(DrawListTag tag) noexcept
        {
            return uint64_t{ 1 } << tag.index;
        }
    };
}
