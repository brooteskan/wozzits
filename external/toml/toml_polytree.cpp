#include <external/toml/toml_polytree.h>

namespace wz::toml
{
    namespace
    {
        wz::core::graph::NodeHandle add_value_subtree(
            wz::core::graph::PolytreeBuilder<TOMLTreeNode, TOMLTreeEdge>& builder,
            const TOMLValue& value)
        {
            const wz::core::graph::NodeHandle self =
                wz::core::graph::add_node(builder, TOMLTreeNode{
                    .value = &value,
                    });

            if (value.kind == TOMLValueKind::Array) {
                for (const auto& child_ptr : value.array_values) {
                    if (!child_ptr)
                        continue;

                    const wz::core::graph::NodeHandle child =
                        add_value_subtree(builder, *child_ptr);

                    const bool ok = wz::core::graph::add_edge(
                        builder,
                        self,
                        child,
                        TOMLTreeEdge{
                            .kind = TOMLTreeEdgeKind::ArrayElement,
                            .key = {},
                        }
                        );

                    if (!ok)
                        return self;
                }
            }

            if (value.kind == TOMLValueKind::Table) {
                for (const TOMLMember& member : value.table_members) {
                    if (!member.value)
                        continue;

                    const wz::core::graph::NodeHandle child =
                        add_value_subtree(builder, *member.value);

                    const bool ok = wz::core::graph::add_edge(
                        builder,
                        self,
                        child,
                        TOMLTreeEdge{
                            .kind = TOMLTreeEdgeKind::TableMember,
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

    std::optional<TOMLPolytreeStorage>
        build_toml_polytree(const TOMLDocument& document)
    {
        if (!document.root)
            return std::nullopt;

        wz::core::graph::PolytreeBuilder<TOMLTreeNode, TOMLTreeEdge> builder;

        add_value_subtree(builder, *document.root);

        return wz::core::graph::build(std::move(builder));
    }

} // namespace wz::toml