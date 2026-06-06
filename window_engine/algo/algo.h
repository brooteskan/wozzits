#pragma once
#include <cstdint>
#include <utility>

namespace wz::core::algo
{
    /**
     * @brief Execution model contract for all algo functions.
     *
     * This module defines a deterministic, non-owning set of algorithms
     * operating over Buffer-like ranges.
     *
     * ## Core rules:
     * - Buffer is treated as a (pointer, count) range abstraction.
     * - Algo functions do NOT own or extend lifetime of data.
     * - No allocation is permitted.
     * - Input buffers are never mutated unless explicitly stated.
     * - All operations are deterministic given identical input.
     */

    template<typename BufferT, typename F>
    void for_each(BufferT& b, F&& fn)
    {
        auto* data = b.data();
        auto n = b.count();

        for (uint32_t i = 0; i < n; ++i)
        {
            fn(data[i]);
        }
    }

    /**
     * @brief Applies a transformation function to each element of an input buffer.
     *
     * Each element of `in` is passed through `fn`, and the result is pushed into `out`.
     *
     * @tparam BufferT input buffer type
     * @tparam F transformation function type
     * @tparam OutBufferT output buffer type
     *
     * @param in input buffer (read-only view semantics)
     * @param out output buffer (must support push)
     * @param fn transformation function applied to each element
     *
     * ## Guarantees:
     * - Input buffer is never modified
     * - Elements are processed in order [0..count)
     * - Output buffer may overflow if capacity is exceeded
     */
    template<typename BufferT, typename F, typename OutBufferT>
    void transform(const BufferT& in, OutBufferT& out, F&& fn)
    {
        auto* data = in.data();
        auto n = in.count();

        for (uint32_t i = 0; i < n; ++i)
        {
            out.push(fn(data[i]));
        }
    }

    /**
     * @brief Reduces a buffer into a single accumulated value.
     *
     * Applies a binary function across all elements in order.
     *
     * @tparam BufferT input buffer type
     * @tparam T accumulator type
     * @tparam F reduction function type
     *
     * @param b input buffer
     * @param init initial accumulator value
     * @param fn reduction function: (acc, value) -> new acc
     *
     * @return final accumulated result
     *
     * ## Guarantees:
     * - Deterministic left-to-right evaluation
     * - No mutation of input buffer
     */
    template<typename BufferT, typename T, typename F>
    T reduce(const BufferT& b, T init, F&& fn)
    {
        auto* data = b.data();
        auto n = b.count();

        for (uint32_t i = 0; i < n; ++i)
        {
            init = fn(init, data[i]);
        }

        return init;
    }

    /**
     * @brief Filters elements from an input buffer into an output buffer.
     *
     * Only elements satisfying the predicate are pushed into `out`.
     *
     * @tparam InBuffer input buffer type
     * @tparam OutBuffer output buffer type
     * @tparam Pred predicate type (returns bool)
     *
     * @param in input buffer
     * @param out output buffer (receives filtered elements)
     * @param pred predicate function applied to each element
     *
     * ## Guarantees:
     * - Input buffer is never modified
     * - Order of retained elements is preserved
     * - Only elements for which pred(v) == true are written to output
     */
    template<typename InBuffer, typename OutBuffer, typename Pred>
    void filter(const InBuffer& in, OutBuffer& out, Pred&& pred)
    {
        auto* data = in.data();
        uint32_t n = in.count();

        for (uint32_t i = 0; i < n; ++i)
        {
            const auto& v = data[i];

            if (pred(v))
            {
                out.push(v);
            }
        }
    }


    /**
        * @brief Executes a sequence of stateless operations immediately.
        *
        * This is NOT a pipeline system.
        * This is just ordered function invocation.
        *
        * No state is stored. No graph is built.
        */
    template<typename InBuffer, typename OutBuffer, typename... Ops>
    void apply(const InBuffer& in, OutBuffer& out, Ops&&... ops)
    {
        (ops(in, out), ...);
    }
    

} // namespace wz::core::algo



