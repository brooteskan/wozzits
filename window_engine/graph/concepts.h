#pragma once

// wz/core/graph/concepts.h

#include <concepts>

namespace wz::core::graph {

    template<typename S, typename V>
    concept Sink = requires(S & s, V v) {
        { s.push(v) } -> std::convertible_to<bool>;
    };

} // namespace wz::core::graph