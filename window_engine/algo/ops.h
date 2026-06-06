#pragma once
#include <cstdint>
#include <utility>


/*
================================================================================
ALGO OPS LAYER

This layer provides stateless operation adapters for wz::core::algo.

It exists ONLY to enable composition syntax.

It does NOT:
- store runtime or conatiner state
- allocate memory
- retain buffers
- build graphs
- defer execution

Each operation is executed immediately when called.
================================================================================
*/

namespace wz::core::algo::ops
{
    /*
    =============================================================================
    MAP OPERATOR
    =============================================================================
    */

    template<typename F>
    struct map_t
    {
        F fn;

        explicit map_t(F&& f)
            : fn(std::forward<F>(f))
        {
        }

        template<typename InBuffer, typename OutBuffer>
        void operator()(const InBuffer& in, OutBuffer& out) const
        {
            auto* data = in.data();
            uint32_t n = in.count();

            for (uint32_t i = 0; i < n; ++i)
            {
                out.push(fn(data[i]));
            }
        }
    };

    template<typename F>
    map_t<F> map(F&& f)
    {
        return map_t<F>(std::forward<F>(f));
    }

    /*
    =============================================================================
    FILTER OPERATOR
    =============================================================================
    */

    template<typename P>
    struct filter_t
    {
        P pred;

        explicit filter_t(P&& p)
            : pred(std::forward<P>(p))
        {
        }

        template<typename InBuffer, typename OutBuffer>
        void operator()(const InBuffer& in, OutBuffer& out) const
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
    };

    template<typename P>
    filter_t<P> filter(P&& p)
    {
        return filter_t<P>(std::forward<P>(p));
    }

    /*
    =============================================================================
    REDUCE OPERATOR (optional adapter form)
    =============================================================================
    */

    template<typename F>
    struct reduce_t
    {
        F fn;

        explicit reduce_t(F&& f)
            : fn(std::forward<F>(f))
        {
        }

        template<typename BufferT, typename T>
        T operator()(const BufferT& b, T init) const
        {
            auto* data = b.data();
            uint32_t n = b.count();

            for (uint32_t i = 0; i < n; ++i)
            {
                init = fn(init, data[i]);
            }

            return init;
        }
    };

    template<typename F>
    reduce_t<F> reduce_op(F&& f)
    {
        return reduce_t<F>(std::forward<F>(f));
    }
}