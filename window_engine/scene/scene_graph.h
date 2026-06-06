#pragma once

// wz/scene/scene_graph.h
//
// Free functions operating on Polytree<TransformNode, SceneEdge>.
// The graph and algo helpers from algo-math are the traversal surface here.

#include <scene/transform_node.h>
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>
#include <algo/next.h>

#include <span>

namespace wz::scene {

    using SceneEdge = struct {};
    using SceneGraph = wz::core::graph::Polytree<TransformNode, SceneEdge>;
    using SceneStorage = wz::core::graph::PolytreeStorage<TransformNode, SceneEdge>;
    using SceneBuilder = wz::core::graph::PolytreeBuilder<TransformNode, SceneEdge>;

    inline wz::math::Mat4 compute_world(
        const wz::math::Mat4& parent_world,
        const wz::math::Mat4& local)
    {
        return wz::math::mul(parent_world, local);
    }

    inline bool is_dirty(const TransformNode& node)
    {
        return node.last_updated_frame == TransformNode::INVALID_FRAME;
    }

    namespace detail {

        inline void propagate_node(
            SceneGraph&                 g,
            wz::core::graph::NodeHandle n,
            uint32_t                    current_frame,
            bool                        mark_clean)
        {
            auto& node = const_cast<TransformNode&>(
                wz::core::graph::node_data(g, n));
            const wz::core::graph::NodeHandle p = wz::core::graph::parent(g, n);
            node.world = (p == wz::core::graph::INVALID_NODE)
                ? node.local
                : compute_world(wz::core::graph::node_data(g, p).world, node.local);

            if (mark_clean)
                node.last_updated_frame = current_frame;
        }

        struct PropagateSink {
            SceneGraph& g;
            uint32_t    current_frame{ TransformNode::INVALID_FRAME };
            bool        mark_clean{ false };

            bool push(wz::core::graph::NodeHandle n)
            {
                propagate_node(g, n, current_frame, mark_clean);
                return true;
            }
        };

        struct SpanSink {
            std::span<wz::core::graph::NodeHandle> buf;
            uint32_t                                count{ 0 };

            bool push(wz::core::graph::NodeHandle n)
            {
                if (count >= static_cast<uint32_t>(buf.size()))
                    return false;
                buf[count++] = n;
                return true;
            }

            std::span<wz::core::graph::NodeHandle> result() const
            {
                return buf.subspan(0, count);
            }
        };

        struct StaticRootSink {
            SceneGraph& g;
            uint32_t    current_frame;

            bool push(wz::core::graph::NodeHandle root)
            {
                PropagateSink sink{ g, current_frame, true };
                wz::core::graph::bfs(g, root, sink);
                return true;
            }
        };

    } // namespace detail

    inline void propagate_all(SceneGraph& g)
    {
        detail::PropagateSink sink{ g };
        wz::core::algo::next::transform(
            wz::core::graph::topo_order(g),
            sink,
            [](wz::core::graph::NodeHandle n) { return n; });
    }

    inline void mark_dirty(SceneGraph& g, wz::core::graph::NodeHandle n)
    {
        const_cast<TransformNode&>(wz::core::graph::node_data(g, n)).last_updated_frame
            = TransformNode::INVALID_FRAME;
    }

    inline void set_local(
        SceneGraph& g,
        wz::core::graph::NodeHandle n,
        const wz::math::Mat4& local)
    {
        auto& node = const_cast<TransformNode&>(wz::core::graph::node_data(g, n));
        node.local = local;
        node.last_updated_frame = TransformNode::INVALID_FRAME;
    }

    inline std::span<wz::core::graph::NodeHandle> collect_dirty_roots(
        const SceneGraph& g,
        uint32_t,
        std::span<wz::core::graph::NodeHandle> scratch)
    {
        detail::SpanSink sink{ scratch };
        wz::core::algo::next::filter(
            wz::core::graph::topo_order(g),
            sink,
            [&g](wz::core::graph::NodeHandle n) {
                const auto& node = wz::core::graph::node_data(g, n);
                if (!is_dirty(node))
                    return false;

                const wz::core::graph::NodeHandle p = wz::core::graph::parent(g, n);
                return p == wz::core::graph::INVALID_NODE ||
                    !is_dirty(wz::core::graph::node_data(g, p));
            });

        return sink.result();
    }

    inline void update_static(
        SceneGraph& g,
        std::span<const wz::core::graph::NodeHandle> dirty_roots,
        uint32_t                                      current_frame)
    {
        detail::StaticRootSink sink{ g, current_frame };
        wz::core::algo::next::transform(
            dirty_roots,
            sink,
            [](wz::core::graph::NodeHandle n) { return n; });
    }

    inline std::span<wz::core::graph::NodeHandle> build_animated_list(
        const SceneGraph& g,
        std::span<wz::core::graph::NodeHandle> scratch)
    {
        detail::SpanSink sink{ scratch };
        wz::core::algo::next::filter(
            wz::core::graph::topo_order(g),
            sink,
            [&g](wz::core::graph::NodeHandle n) {
                return wz::core::graph::node_data(g, n).motion_type ==
                    TransformNode::MotionType::Animated;
            });

        return sink.result();
    }

    inline void update_animated(
        SceneGraph& g,
        std::span<const wz::core::graph::NodeHandle> animated_list,
        uint32_t                                      current_frame)
    {
        detail::PropagateSink sink{ g, current_frame, true };
        wz::core::algo::next::transform(
            animated_list,
            sink,
            [](wz::core::graph::NodeHandle n) { return n; });
    }

} // namespace wz::scene
