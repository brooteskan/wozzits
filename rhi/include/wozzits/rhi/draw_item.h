#pragma once

// wozzits/rhi/draw_item.h
//
// The render-IR seam. A DrawItem is the flat, self-describing unit the backend
// consumes: one draw of one object in one pass. It references resources by
// handle/tag (it does not own them) and carries no hierarchy — the opposite of
// the authored scene's pointer-linked node tree.
//
// Following the O3DE shape: sorting metadata lives in a separate Properties
// wrapper, so the heavy item stays put and a flat array of small Properties is
// what actually gets sorted before submission.
//
// This is a seed: fields will grow (geometry/SRG handles, scissor/viewport,
// instance args). The shape — references not ownership, sort key separate — is
// the part that matters now.

#include <wozzits/rhi/tag_registry.h>

#include <cstdint>

namespace wz::rhi
{
    struct DrawItem
    {
        // Which render program this draw uses — a registry Tag, never an enum.
        Tag program{};

        // Whether this item should render. A cheap visibility/enable toggle
        // that does not require removing the item from any list.
        bool enabled = true;

        // Resource bindings (geometry view, shader-resource groups, root
        // constants) attach here as handles in subsequent commits.
    };

    using DrawItemSortKey = uint64_t;

    // The sortable view over a DrawItem. Build an array of these, sort by
    // sort_key, then submit — the item itself never moves.
    struct DrawItemProperties
    {
        const DrawItem* item = nullptr;
        DrawItemSortKey sort_key = 0;
        float depth = 0.0f;

        friend bool operator<(const DrawItemProperties& a,
                              const DrawItemProperties& b)
        {
            return a.sort_key < b.sort_key;
        }
    };
}
