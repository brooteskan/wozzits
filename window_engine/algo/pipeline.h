#pragma once
#include <cstdint>
#include <utility>

namespace wz::core::algo::pipeline
{
    template<typename... Ops>
    struct pipeline_t
    {
        std::tuple<Ops...> ops;

        template<typename In, typename Out>
        void operator()(const In& in, Out& out) const
        {
            for (auto&& v : in)
            {
                if (!apply_all(v, out))
                    break;
            }
        }

        template<typename V, typename Out>
        bool apply_all(V&& v, Out& out) const
        {
            return apply_all_impl<0>(std::forward<V>(v), out);
        }

        // 🔓 make this public
        template<std::size_t I, typename V, typename Out>
        bool apply_all_impl(V&& v, Out& out) const
        {
            if constexpr (I == sizeof...(Ops))
            {
                return out.push(std::forward<V>(v));
            }
            else
            {
                const auto& op = std::get<I>(ops);
                return op.template apply<I>(
                    std::forward<V>(v),
                    out,
                    *this
                );
            }
        }
    };

    template<typename F>
    struct map_t
    {
        F fn;

        template<std::size_t I, typename V, typename Out, typename Pipeline>
        bool apply(V&& v, Out& out, const Pipeline& p) const
        {
            auto x = std::invoke(fn, std::forward<V>(v));
            return p.template apply_all_impl<I + 1>(std::move(x), out);
        }
    };

    template<typename P>
    struct filter_t
    {
        P pred;

        template<std::size_t I, typename V, typename Out, typename Pipeline>
        bool apply(V&& v, Out& out, const Pipeline& p) const
        {
            if (std::invoke(pred, v))
            {
                return p.template apply_all_impl<I + 1>(std::forward<V>(v), out);
            }
            return true; // skip element, continue loop
        }
    };

    template<typename... L, typename... R>
    auto operator|(pipeline_t<L...> lhs, pipeline_t<R...> rhs)
    {
        return pipeline_t<L..., R...>{
            std::tuple_cat(lhs.ops, rhs.ops)
        };
    }

    template<typename F>
    auto map(F&& f)
    {
        return pipeline_t<map_t<std::decay_t<F>>>{
            std::tuple{ map_t<std::decay_t<F>>{ std::forward<F>(f) } }
        };
    }

    template<typename P>
    auto filter(P&& p)
    {
        return pipeline_t<filter_t<std::decay_t<P>>>{
            std::tuple{ filter_t<std::decay_t<P>>{ std::forward<P>(p) } }
        };
    }

    template<typename F, typename G>
    auto operator|(map_t<F> lhs, map_t<G> rhs)
    {
        auto composed = [f = lhs.fn, g = rhs.fn](auto&& v)
            {
                return std::invoke(g, std::invoke(f, std::forward<decltype(v)>(v)));
            };

        return map_t<std::decay_t<decltype(composed)>>{
            std::move(composed)
        };
    }

    template<typename P, typename Q>
    auto operator|(filter_t<P> lhs, filter_t<Q> rhs)
    {
        auto combined = [p = lhs.pred, q = rhs.pred](auto&& v)
            {
                return std::invoke(p, v) && std::invoke(q, v);
            };

        return filter_t<std::decay_t<decltype(combined)>>{
            std::move(combined)
        };
    }

    template<typename F, typename P, typename G>
    struct map_filter_map_t
    {
        F f;
        P p;
        G g;

        template<std::size_t I, typename V, typename Out, typename Pipeline>
        bool apply(V&& v, Out& out, const Pipeline&) const
        {
            auto y = std::invoke(f, std::forward<V>(v));

            if (std::invoke(p, y))
            {
                return out.push(std::invoke(g, std::move(y)));
            }

            return true;
        }
    };

    template<typename F, typename P, typename G>
    auto operator|(
        pipeline_t<map_t<F>, filter_t<P>> lhs,
        map_t<G> rhs)
    {
        using fused_t = map_filter_map_t<F, P, G>;

        return pipeline_t<fused_t>{
            std::tuple{
                fused_t{
                    std::get<0>(lhs.ops).fn,
                    std::get<1>(lhs.ops).pred,
                    rhs.fn
                }
            }
        };
    }

}

