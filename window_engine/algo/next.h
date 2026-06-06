#pragma once

#include <concepts>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace wz::core::algo::next
{
    /* readable range */
    template<typename R>
    concept ReadableRange =
        requires(R r)
    {
        { std::begin(r) };
        { std::end(r) };
        { *std::begin(r) };
    };

    /* transform sink */
    template<typename Out, typename InVal, typename F>
    concept CanTransformInto =
        requires(Out o, F f, InVal v)
    {
        { std::invoke(f, v) };
        { o.push(std::invoke(f, v)) } -> std::convertible_to<bool>;
    };

    /* transform */
    template<ReadableRange In, typename Out, typename F>
        requires CanTransformInto<
            Out,
                decltype(*std::begin(std::declval<In&>())),
                F
        >
    void transform(const In& in, Out& out, F&& fn)
    {
        for (auto&& v : in)
        {
            if (!out.push(std::invoke(fn, v)))
            {
                // policy: truncate
                break;
            }
        }
    }


    /* filter */
    template<ReadableRange In, typename Out, typename Pred>
        requires requires(Out o, decltype(*std::begin(std::declval<In&>())) v, Pred p)
    {
        { o.push(v) } -> std::convertible_to<bool>;
        { std::invoke(p, v) } -> std::convertible_to<bool>;
    }
    void filter(const In& in, Out& out, Pred&& pred)
    {
        for (auto&& v : in)
        {
            if (std::invoke(pred, v))
            {
                if (!out.push(v))
                    break;
            }
        }
    }

    /* reduce */
    template<ReadableRange In, typename T, typename F>
        requires requires(T acc, decltype(*std::begin(std::declval<In&>())) v, F f)
    {
        { std::invoke(f, acc, v) } -> std::convertible_to<T>;
    }
    T reduce(const In& in, T init, F&& fn)
    {
        for (auto&& v : in)
        {
            init = std::invoke(fn, init, v);
        }
        return init;
    }

    /* pipeline ops */
    template<typename F>
    struct map_t
    {
        F fn;

        template<typename In, typename Out>
        void operator()(const In& in, Out& out) const
        {
            transform(in, out, fn);
        }
    };

    template<typename P>
    struct filter_t
    {
        P pred;

        template<typename In, typename Out>
        void operator()(const In& in, Out& out) const
        {
            next::filter(in, out, pred);
        }
    };

    template<typename F, typename P>
    struct map_filter_t
    {
        F fn;
        P pred;

        template<typename In, typename Out>
        void operator()(const In& in, Out& out) const
        {
            for (auto&& v : in)
            {
                auto x = std::invoke(fn, v);

                if (std::invoke(pred, x))
                {
                    if (!out.push(std::move(x)))
                        break;
                }
            }
        }
    };

    template<typename F, typename P>
    map_filter_t<F, P> operator|(map_t<F> m, filter_t<P> f)
    {
        return { m.fn, f.pred };
    }

    template<typename F>
    map_t<std::decay_t<F>> map(F&& f)
    {
        return { std::forward<F>(f) };
    }

    template<typename P>
    filter_t<std::decay_t<P>> filter(P&& p)
    {
        return { std::forward<P>(p) };
    }

} // namespace wz::core::algo::next