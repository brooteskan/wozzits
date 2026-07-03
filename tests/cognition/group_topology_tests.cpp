#include <cognition/group_topology.h>

#include <gtest/gtest.h>

#include <vector>

using namespace wz::engine::cognition;

namespace
{
    // Assert two canonical bonds are equal (same pair, same coupling).
    void ExpectBond(const ExactBond& b, uint32_t a, uint32_t bb, double j)
    {
        EXPECT_EQ(b.a, a);
        EXPECT_EQ(b.b, bb);
        EXPECT_DOUBLE_EQ(b.j, j);
    }
}

// ---------------------------------------------------------------------------
// Rule 1: self-bonds are DROPPED (a self-coupling is a constant j*I, not a
// self-field). {0,0,1.0} contributes nothing -- no bonds, topology None.
// ---------------------------------------------------------------------------
TEST(GroupTopology, SelfBondIsDroppedNotFolded)
{
    CanonicalBonds c = canonicalize_bonds(3, { ExactBond{ .a = 0, .b = 0, .j = 1.0 } });
    EXPECT_TRUE(c.bonds.empty());
    EXPECT_EQ(c.topology, GroupTopology::None);
}

// A self-bond mixed in with a real bond drops the self part but keeps the rest;
// it does NOT bleed into any field or into the real bond's coupling.
TEST(GroupTopology, SelfBondDoesNotContaminateRealBonds)
{
    CanonicalBonds c = canonicalize_bonds(
        3,
        {
            ExactBond{ .a = 1, .b = 1, .j = 5.0 },   // self -> dropped
            ExactBond{ .a = 0, .b = 1, .j = 0.7 },   // real -> kept, unchanged
        });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 0, 1, 0.7);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// ---------------------------------------------------------------------------
// Rule 2: parallel bonds on the same UNORDERED pair sum into one, regardless of
// orientation. {0,1,0.3} + {1,0,0.2} -> single {0,1,0.5}.
// ---------------------------------------------------------------------------
TEST(GroupTopology, ParallelBondsSumRegardlessOfOrientation)
{
    CanonicalBonds c = canonicalize_bonds(
        2,
        {
            ExactBond{ .a = 0, .b = 1, .j = 0.3 },
            ExactBond{ .a = 1, .b = 0, .j = 0.2 },  // reversed orientation
        });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 0, 1, 0.5);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// Three parallel bonds (mixed orientation) sum to one.
TEST(GroupTopology, ManyParallelBondsSumToOne)
{
    CanonicalBonds c = canonicalize_bonds(
        2,
        {
            ExactBond{ .a = 1, .b = 0, .j = 1.0 },
            ExactBond{ .a = 0, .b = 1, .j = 2.0 },
            ExactBond{ .a = 1, .b = 0, .j = 0.5 },
        });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 0, 1, 3.5);
}

// ---------------------------------------------------------------------------
// Rule 3: parallel bonds that cancel to exactly 0 are dropped (a ferro + anti
// pair). {0,1,1.0} + {0,1,-1.0} -> none.
// ---------------------------------------------------------------------------
TEST(GroupTopology, CancellingParallelBondsAreDropped)
{
    CanonicalBonds c = canonicalize_bonds(
        2,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 0, .b = 1, .j = -1.0 },
        });
    EXPECT_TRUE(c.bonds.empty());
    EXPECT_EQ(c.topology, GroupTopology::None);
}

// A bond that is directly authored as zero is dropped too.
TEST(GroupTopology, DirectZeroCouplingIsDropped)
{
    CanonicalBonds c = canonicalize_bonds(2, { ExactBond{ .a = 0, .b = 1, .j = 0.0 } });
    EXPECT_TRUE(c.bonds.empty());
    EXPECT_EQ(c.topology, GroupTopology::None);
}

// A tiny-but-nonzero coupling is a REAL (weak) coupling and is NOT thresholded
// away -- rule 3 drops exact 0.0 only.
TEST(GroupTopology, TinyNonzeroCouplingIsKept)
{
    CanonicalBonds c = canonicalize_bonds(2, { ExactBond{ .a = 0, .b = 1, .j = 1e-12 } });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 0, 1, 1e-12);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// ---------------------------------------------------------------------------
// Rule 4: bonds referencing an endpoint >= agent_count are dropped.
// ---------------------------------------------------------------------------
TEST(GroupTopology, OutOfRangeEndpointIsDropped)
{
    CanonicalBonds c = canonicalize_bonds(
        3,
        {
            ExactBond{ .a = 0, .b = 3, .j = 1.0 },  // 3 >= 3 -> out of range
            ExactBond{ .a = 5, .b = 1, .j = 1.0 },  // 5 >= 3 -> out of range
            ExactBond{ .a = 0, .b = 2, .j = 1.0 },  // in range -> kept
        });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 0, 2, 1.0);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// With no agents at all, every bond is out of range.
TEST(GroupTopology, ZeroAgentsDropsEverything)
{
    CanonicalBonds c = canonicalize_bonds(0, { ExactBond{ .a = 0, .b = 1, .j = 1.0 } });
    EXPECT_TRUE(c.bonds.empty());
    EXPECT_EQ(c.topology, GroupTopology::None);
}

// ---------------------------------------------------------------------------
// Rule 5: output is deterministically ordered -- a shuffled input yields the
// same sorted (a, then b) output.
// ---------------------------------------------------------------------------
TEST(GroupTopology, OutputOrderIsDeterministic)
{
    const std::vector<ExactBond> shuffled = {
        ExactBond{ .a = 2, .b = 3, .j = 1.0 },
        ExactBond{ .a = 0, .b = 2, .j = 1.0 },
        ExactBond{ .a = 3, .b = 1, .j = 1.0 },  // -> (1,3)
        ExactBond{ .a = 0, .b = 1, .j = 1.0 },
    };
    CanonicalBonds c = canonicalize_bonds(4, shuffled);

    ASSERT_EQ(c.bonds.size(), 4u);
    ExpectBond(c.bonds[0], 0, 1, 1.0);
    ExpectBond(c.bonds[1], 0, 2, 1.0);
    ExpectBond(c.bonds[2], 1, 3, 1.0);
    ExpectBond(c.bonds[3], 2, 3, 1.0);

    // Re-running on a differently-ordered permutation gives byte-identical
    // output.
    const std::vector<ExactBond> other_perm = {
        ExactBond{ .a = 0, .b = 1, .j = 1.0 },
        ExactBond{ .a = 2, .b = 3, .j = 1.0 },
        ExactBond{ .a = 0, .b = 2, .j = 1.0 },
        ExactBond{ .a = 1, .b = 3, .j = 1.0 },
    };
    CanonicalBonds c2 = canonicalize_bonds(4, other_perm);
    ASSERT_EQ(c2.bonds.size(), c.bonds.size());
    for (size_t i = 0; i < c.bonds.size(); ++i)
    {
        EXPECT_EQ(c2.bonds[i].a, c.bonds[i].a);
        EXPECT_EQ(c2.bonds[i].b, c.bonds[i].b);
        EXPECT_DOUBLE_EQ(c2.bonds[i].j, c.bonds[i].j);
    }
}

// ---------------------------------------------------------------------------
// Topology classification.
// ---------------------------------------------------------------------------

TEST(GroupTopology, EmptyBondListIsNone)
{
    CanonicalBonds c = canonicalize_bonds(4, {});
    EXPECT_TRUE(c.bonds.empty());
    EXPECT_EQ(c.topology, GroupTopology::None);
}

// A chain 0-1-2-3 is acyclic -> Tree.
TEST(GroupTopology, ChainIsTree)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 2, .b = 3, .j = 1.0 },
        });
    ASSERT_EQ(c.bonds.size(), 3u);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// A star (hub 0 to 1,2,3) is acyclic -> Tree.
TEST(GroupTopology, StarIsTree)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 0, .b = 2, .j = 1.0 },
            ExactBond{ .a = 0, .b = 3, .j = 1.0 },
        });
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// A disconnected forest (two separate edges, 0-1 and 2-3) is acyclic -> Tree.
TEST(GroupTopology, DisconnectedForestIsTree)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 2, .b = 3, .j = 1.0 },
        });
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}

// A triangle 0-1, 1-2, 0-2 has a girth-3 cycle -> Cyclic.
TEST(GroupTopology, TriangleIsCyclic)
{
    CanonicalBonds c = canonicalize_bonds(
        3,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 0, .b = 2, .j = 1.0 },
        });
    ASSERT_EQ(c.bonds.size(), 3u);
    EXPECT_EQ(c.topology, GroupTopology::Cyclic);
}

// A triangle where one edge is expressed as a redundant parallel pair still
// classifies Cyclic after the merge (the parallel bonds fuse to one edge, but
// the three distinct edges of the triangle remain).
TEST(GroupTopology, TriangleWithRedundantParallelBondIsCyclic)
{
    CanonicalBonds c = canonicalize_bonds(
        3,
        {
            ExactBond{ .a = 0, .b = 1, .j = 0.6 },
            ExactBond{ .a = 1, .b = 0, .j = 0.4 },  // parallel -> merges into 0-1
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 0, .b = 2, .j = 1.0 },
        });
    ASSERT_EQ(c.bonds.size(), 3u);  // merged 0-1, plus 1-2 and 0-2
    ExpectBond(c.bonds[0], 0, 1, 1.0);
    EXPECT_EQ(c.topology, GroupTopology::Cyclic);
}

// A square 4-cycle 0-1-2-3-0 -> Cyclic.
TEST(GroupTopology, SquareCycleIsCyclic)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 2, .b = 3, .j = 1.0 },
            ExactBond{ .a = 3, .b = 0, .j = 1.0 },
        });
    ASSERT_EQ(c.bonds.size(), 4u);
    EXPECT_EQ(c.topology, GroupTopology::Cyclic);
}

// A tree plus one extra edge that closes a loop -> Cyclic. Here a chain
// 0-1-2-3 (a tree) plus the edge 0-3 closes the whole thing into a 4-cycle.
TEST(GroupTopology, TreePlusClosingEdgeIsCyclic)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 2, .b = 3, .j = 1.0 },
            ExactBond{ .a = 0, .b = 3, .j = 1.0 },  // closes the loop
        });
    EXPECT_EQ(c.topology, GroupTopology::Cyclic);
}

// A cycle living in ONE component of an otherwise forested graph is still
// Cyclic -- the classifier flags the graph as a whole, not per component.
TEST(GroupTopology, CycleInOneComponentOfForestIsCyclic)
{
    CanonicalBonds c = canonicalize_bonds(
        6,
        {
            // Triangle on {0,1,2}.
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },
            ExactBond{ .a = 1, .b = 2, .j = 1.0 },
            ExactBond{ .a = 0, .b = 2, .j = 1.0 },
            // Separate tree edge on {3,4}.
            ExactBond{ .a = 3, .b = 4, .j = 1.0 },
        });
    EXPECT_EQ(c.topology, GroupTopology::Cyclic);
}

// After all rules, a graph that reduces to a single edge is a Tree; a graph
// that reduces to nothing is None -- combined-rules smoke test.
TEST(GroupTopology, CombinedRulesReduceCleanly)
{
    CanonicalBonds c = canonicalize_bonds(
        4,
        {
            ExactBond{ .a = 2, .b = 2, .j = 9.0 },   // self -> dropped
            ExactBond{ .a = 0, .b = 9, .j = 1.0 },   // out of range -> dropped
            ExactBond{ .a = 0, .b = 1, .j = 1.0 },   // pair {0,1} ...
            ExactBond{ .a = 1, .b = 0, .j = -1.0 },  // ... cancels to 0 -> dropped
            ExactBond{ .a = 3, .b = 2, .j = 0.5 },   // kept -> (2,3)
        });
    ASSERT_EQ(c.bonds.size(), 1u);
    ExpectBond(c.bonds[0], 2, 3, 0.5);
    EXPECT_EQ(c.topology, GroupTopology::Tree);
}
