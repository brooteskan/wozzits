#include <external/json/json_polytree.h>

namespace wz::json
{
    namespace
    {
        wz::core::graph::NodeHandle add_value_subtree(
            wz::core::graph::PolytreeBuilder<JSONTreeNode, JSONTreeEdge>& builder,
            const JSONValue& value)
        {
            const wz::core::graph::NodeHandle self =
                wz::core::graph::add_node(builder, JSONTreeNode{
                    .value = &value,
                    });

            if (value.kind == JSONValueKind::Array) {
                for (const auto& child_ptr : value.array_values) {
                    if (!child_ptr)
                        continue;

                    const wz::core::graph::NodeHandle child =
                        add_value_subtree(builder, *child_ptr);

                    const bool ok = wz::core::graph::add_edge(
                        builder,
                        self,
                        child,
                        JSONTreeEdge{
                            .kind = JSONTreeEdgeKind::ArrayElement,
                            .key = {},
                        }
                        );

                    if (!ok)
                        return self;
                }
            }

            if (value.kind == JSONValueKind::Object) {
                for (const JSONMember& member : value.object_members) {
                    if (!member.value)
                        continue;

                    const wz::core::graph::NodeHandle child =
                        add_value_subtree(builder, *member.value);

                    const bool ok = wz::core::graph::add_edge(
                        builder,
                        self,
                        child,
                        JSONTreeEdge{
                            .kind = JSONTreeEdgeKind::ObjectMember,
                            .key = member.key,
                        }
                        );

                    if (!ok)
                        return self;
                }
            }

            return self;
        }
    }

    std::optional<JSONPolytreeStorage>
        build_json_polytree(const JSONDocument& document)
    {
        if (!document.root)
            return std::nullopt;

        wz::core::graph::PolytreeBuilder<JSONTreeNode, JSONTreeEdge> builder;

        add_value_subtree(builder, *document.root);

        return wz::core::graph::build(std::move(builder));
    }

} // namespace wz::json