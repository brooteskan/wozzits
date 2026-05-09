#pragma once

// external/toml/toml_polytree.h
//
// Polytree adapter for Wozzits-owned TOMLDocument.
//
// This is a non-owning traversal/index view over TOMLDocument.
// The TOMLDocument must outlive the returned TOMLPolytreeStorage.

#include <external/toml/toml_document.h>

#include <graph/static_polytree.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace wz::toml
{
    enum class TOMLTreeEdgeKind : uint8_t
    {
        ArrayElement,
        TableMember,
    };

    struct TOMLTreeNode
    {
        const TOMLValue* value = nullptr;
    };

    struct TOMLTreeEdge
    {
        TOMLTreeEdgeKind kind = TOMLTreeEdgeKind::ArrayElement;

        // Non-empty only when kind == TableMember.
        // Points into the owning TOMLDocument's member key storage.
        std::string_view key;
    };

    using TOMLPolytreeStorage =
        wz::core::graph::PolytreeStorage<TOMLTreeNode, TOMLTreeEdge>;

    std::optional<TOMLPolytreeStorage>
        build_toml_polytree(const TOMLDocument& document);

} // namespace wz::toml