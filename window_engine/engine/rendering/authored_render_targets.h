#pragma once

// engine/rendering/authored_render_targets.h
//
// Issue #287: resolve the scene's authored render-to-texture sources into a
// per-target list of node indices, so the frame can render each target with no
// per-target code.
//
// This is the SELECTION half, and it is deliberately pure -- no device, no asset
// library, just the node array -- because that is the part with the interesting
// behaviour (subtree gathering, disabled/unresolved sources, which nodes the
// main pass must then skip) and the part worth testing without a GPU.

#include <engine/assets/scene/scene_asset_data.h>

#include <asset/types.h>

#include <cstddef>
#include <span>
#include <vector>

namespace wz::engine::rendering
{
    // One authored target and the scene nodes that fill it.
    struct AuthoredRenderTarget
    {
        // The render-target texture asset (already bridged onto the node).
        wz::asset::AssetKey texture{};

        // Indices into the node array handed to collect_authored_render_targets,
        // in ARRAY ORDER -- so the offscreen pass draws them in the same order
        // the main pass would, render_order and all.
        std::vector<std::size_t> node_indices;

        // The node carrying the component, for diagnostics.
        std::size_t source_index = 0;
    };

    struct AuthoredRenderTargets
    {
        std::vector<AuthoredRenderTarget> targets;

        // Nodes the main pass must SKIP: gathered by a target whose source did
        // not ask to also draw in the scene. A node claimed by two targets, one
        // of which draws in scene, is drawn -- "also draw" wins, since the
        // authored request to see it is explicit and the omission is not.
        std::vector<bool> excluded_from_scene;

        [[nodiscard]] bool empty() const noexcept { return targets.empty(); }
    };

    // Collect every enabled render-to-texture source in `nodes` whose target key
    // has resolved. A source with no resolved target contributes nothing (the
    // caller warns): rendering it into the backbuffer instead would put art
    // meant for a surface on the screen.
    [[nodiscard]] AuthoredRenderTargets collect_authored_render_targets(
        std::span<const assets::SceneNodeAsset> nodes);
}
