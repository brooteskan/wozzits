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

#include <wozzits/rhi/draw_list_tag.h>
#include <wozzits/rhi/geometry_view.h>
#include <wozzits/rhi/tag_registry.h>

#include <cstdint>

namespace wz::rhi
{
    class ShaderResourceGroup;

    using DrawItemSortKey = uint64_t;

    struct DrawArgs
    {
        bool     indexed        = false;
        uint32_t index_count    = 0;
        uint32_t vertex_count   = 0;
        uint32_t instance_count = 1;
        uint32_t first_index    = 0;
        uint32_t first_vertex   = 0;
        int32_t  vertex_offset  = 0;
    };

    struct DrawItem
    {
        // Which render program this draw uses — a registry Tag, never an enum.
        Tag program{};

        // Closed surface/sort class, independent of which pass consumes it.
        RenderDomain render_domain = RenderDomain::Opaque;

        // Open pass route ("depth", "forward", "shadow", "debug", ...).
        DrawListTag pass{};

        // Per-pass resources and routing state. Geometry/root constants/shared
        // SRGs live on DrawPacket; only the unique per-pass SRG pointer is here.
        StreamBufferIndices streams;
        const ShaderResourceGroup* unique_srg = nullptr;

        // DECLARED, NOT READ. Every DrawRequest in every repo passes 0 here, and
        // nothing computes or consumes it: the engine's actual ordering is a
        // stable two-pass partition by DrawLayer preserving authored node index
        // (rhi_scene_renderer.cpp), which never looks at this field.
        //
        // Kept as the seam for Draw submission R2 (batching + state sort) and
        // for Transparency R2 (sorted alpha) -- the first time two overlapping
        // alpha-blended renderables need depth-sorted transparency, the submit
        // loop has nowhere to hook, and this is the hook. #317 D1-Q6.
        DrawItemSortKey sort_key = 0;

        DrawListMask filter_mask{};

        // DECLARED, NOT READ -- and not implementable as it stands: the depth
        // format is D32_FLOAT, which has no stencil plane, and RenderProgramDesc
        // has no way to declare stencil state. Finishing it is a DSV format
        // migration, not a recorder change. #317 D1-Q3.
        uint32_t stencil_ref = 0;
    };

    // The sortable view over a DrawItem -- the intended shape being: build an
    // array of these, sort by sort_key, then submit, so the item itself never
    // moves.
    //
    // NO SUCH ARRAY IS EVER BUILT. This type has no non-test reference in any
    // repo. It is kept with sort_key above, as the same seam; see that comment.
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
