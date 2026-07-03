#include <cognition/group_topology.h>

#include <algorithm>

namespace wz::engine::cognition
{
    namespace
    {
        // Disjoint-set (union-find) over agent_count nodes, with path
        // compression. Used to classify the canonicalized bond graph: a bond
        // whose two endpoints are already in the same set closes a cycle.
        struct UnionFind
        {
            std::vector<uint32_t> parent;

            explicit UnionFind(uint32_t n) : parent(n)
            {
                for (uint32_t i = 0; i < n; ++i)
                    parent[i] = i;
            }

            uint32_t find(uint32_t x)
            {
                while (parent[x] != x)
                {
                    parent[x] = parent[parent[x]];  // path halving
                    x = parent[x];
                }
                return x;
            }

            // Union the two sets. Returns true if they were already the SAME
            // set (i.e. this edge closes a cycle), false if it joined two
            // distinct components.
            bool unite(uint32_t a, uint32_t b)
            {
                const uint32_t ra = find(a);
                const uint32_t rb = find(b);
                if (ra == rb)
                    return true;
                parent[ra] = rb;
                return false;
            }
        };
    }

    CanonicalBonds canonicalize_bonds(
        uint32_t agent_count, const std::vector<ExactBond>& bonds)
    {
        // Collect the surviving bonds as (a<b, j), summing parallels. We keep a
        // parallel vector of unordered pairs and running sums; small groups
        // (fireteams) make a linear scan cheaper and simpler than a hash map,
        // and it keeps the merge order stable before the final sort.
        std::vector<ExactBond> merged;

        for (const ExactBond& raw : bonds)
        {
            uint32_t a = raw.a;
            uint32_t b = raw.b;

            // Rule 1: drop self-bonds (constant energy shift, not a self-field).
            if (a == b)
                continue;

            // Rule 4: drop out-of-range endpoints (defensive).
            if (a >= agent_count || b >= agent_count)
                continue;

            // Rule 2: normalize orientation to the unordered pair a < b.
            if (a > b)
                std::swap(a, b);

            // Rule 2 (cont.): sum onto an existing bond for the same pair.
            auto it = std::find_if(
                merged.begin(), merged.end(),
                [a, b](const ExactBond& m) { return m.a == a && m.b == b; });
            if (it != merged.end())
                it->j += raw.j;
            else
                merged.push_back(ExactBond{ .a = a, .b = b, .j = raw.j });
        }

        CanonicalBonds result;
        result.bonds.reserve(merged.size());

        // Rule 3: drop couplings that summed to exactly 0.0 (dynamically inert).
        // No epsilon threshold -- a tiny nonzero j is a real coupling.
        for (const ExactBond& m : merged)
        {
            if (m.j != 0.0)
                result.bonds.push_back(m);
        }

        // Rule 5: deterministic order -- sort ascending by (a, then b). Pairs
        // are unique after the merge, so (a, b) is a total order on them.
        std::sort(
            result.bonds.begin(), result.bonds.end(),
            [](const ExactBond& x, const ExactBond& y)
            {
                if (x.a != y.a)
                    return x.a < y.a;
                return x.b < y.b;
            });

        // Classify topology of the canonicalized graph via union-find.
        if (result.bonds.empty())
        {
            result.topology = GroupTopology::None;
            return result;
        }

        UnionFind uf(agent_count);
        bool cyclic = false;
        for (const ExactBond& b : result.bonds)
        {
            if (uf.unite(b.a, b.b))
            {
                cyclic = true;
                break;
            }
        }
        result.topology = cyclic ? GroupTopology::Cyclic : GroupTopology::Tree;
        return result;
    }
}
