#pragma once

#include <external/json/json_document.h>

#include <graph/static_polytree.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wz::json
{
    enum class JSONTreeEdgeKind : uint8_t
    {
        ArrayElement,
        ObjectMember,
    };

    struct JSONTreeNode
    {
        const JSONValue* value = nullptr;
    };

    struct JSONTreeEdge
    {
        JSONTreeEdgeKind kind = JSONTreeEdgeKind::ArrayElement;

        // Non-empty only when kind == ObjectMember.
        std::string_view key;
    };

    using JSONPolytreeStorage =
        wz::core::graph::PolytreeStorage<JSONTreeNode, JSONTreeEdge>;

    std::optional<JSONPolytreeStorage>
        build_json_polytree(const JSONDocument& document);

} // namespace wz::json