#include <engine/rendering/authored_render_targets.h>

#include <string>
#include <unordered_map>

namespace wz::engine::rendering
{
    namespace
    {
        // Index of each node by authored id, for the ancestor walk below. A
        // duplicate id keeps the FIRST node, matching the scene tree's own
        // first-wins resolution.
        std::unordered_map<std::string, std::size_t> index_by_id(
            std::span<const assets::SceneNodeAsset> nodes)
        {
            std::unordered_map<std::string, std::size_t> out;
            out.reserve(nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                out.emplace(nodes[i].id, i);
            }
            return out;
        }

        // Is `candidate` inside the subtree rooted at `root_index`? Walks the
        // parent chain, bounded by the node count so a malformed scene (a cycle
        // through parent_id) cannot spin here.
        bool is_in_subtree(
            std::span<const assets::SceneNodeAsset> nodes,
            const std::unordered_map<std::string, std::size_t>& by_id,
            std::size_t candidate,
            std::size_t root_index)
        {
            std::size_t current = candidate;
            for (std::size_t steps = 0; steps <= nodes.size(); ++steps) {
                if (current == root_index) {
                    return true;
                }
                const std::optional<wz::scene::AuthoredEntityId>& parent =
                    nodes[current].parent_id;
                if (!parent) {
                    return false;
                }
                const auto it = by_id.find(*parent);
                if (it == by_id.end()) {
                    return false;
                }
                current = it->second;
            }
            return false;
        }
    }

    AuthoredRenderTargets collect_authored_render_targets(
        std::span<const assets::SceneNodeAsset> nodes)
    {
        AuthoredRenderTargets out{};
        out.excluded_from_scene.assign(nodes.size(), false);

        // Cheap pre-pass: almost every scene has no render-to-texture source at
        // all, and this is called every frame.
        bool any = false;
        for (const assets::SceneNodeAsset& node : nodes) {
            if (node.render_to_texture && node.render_to_texture->enabled) {
                any = true;
                break;
            }
        }
        if (!any) {
            return out;
        }

        const std::unordered_map<std::string, std::size_t> by_id =
            index_by_id(nodes);

        for (std::size_t source = 0; source < nodes.size(); ++source) {
            const std::optional<assets::SceneRenderToTextureAsset>& rtt =
                nodes[source].render_to_texture;
            if (!rtt || !rtt->enabled) {
                continue;
            }
            // An unresolved target means the offscreen pass has nowhere to go.
            // Contribute nothing rather than fall back to the backbuffer: art
            // authored for a surface must not appear on the screen instead.
            if (rtt->target == wz::asset::AssetKey{}) {
                continue;
            }

            AuthoredRenderTarget target{};
            target.texture = rtt->target;
            target.source_index = source;

            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const bool gathered = rtt->include_descendants
                    ? is_in_subtree(nodes, by_id, i, source)
                    : i == source;
                if (!gathered) {
                    continue;
                }
                target.node_indices.push_back(i);
                if (!rtt->also_draw_in_scene) {
                    out.excluded_from_scene[i] = true;
                }
            }

            out.targets.push_back(std::move(target));
        }

        // "Also draw in scene" wins over a sibling target's exclusion: asking to
        // see a node is explicit, while another target merely not asking is not.
        for (std::size_t source = 0; source < nodes.size(); ++source) {
            const std::optional<assets::SceneRenderToTextureAsset>& rtt =
                nodes[source].render_to_texture;
            if (!rtt || !rtt->enabled || !rtt->also_draw_in_scene
                || rtt->target == wz::asset::AssetKey{})
            {
                continue;
            }
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const bool gathered = rtt->include_descendants
                    ? is_in_subtree(nodes, by_id, i, source)
                    : i == source;
                if (gathered) {
                    out.excluded_from_scene[i] = false;
                }
            }
        }

        return out;
    }
}
