// tests/asset/asset_system_adversarial_tests.cpp
//
// Adversarial suite for the AssetSystem DAG/cache/compiled-node invariants —
// the standing ask on issue #75, built during the B1 audit visit (#320).
//
// The theme: AssetCache is PUBLIC MUTABLE STATE (AssetSystem::cache() hands out
// a non-const reference), compilers are caller-supplied and may violate their
// contract, and the registered graph is rebuilt in place. None of those may
// leave a stale ResourceHandle visible through find_compiled() / query() /
// compiled_terminals(), and a failed rebuild may not destroy the graph that was
// working before it.
//
// Every test here drives the real AssetSystem; the only fixture is a compiler
// whose misbehaviour the test steers.

#include <gtest/gtest.h>

#include <asset/system.h>

#include <span>
#include <vector>

using namespace wz::asset;

namespace
{
    constexpr SchemaID kSchema{ 0xB1B1 };
    constexpr AssetType kType = static_cast<AssetType>(77);

    AssetKey key_n(uint64_t n)
    {
        AssetKey k{};
        k.content_hash = { n, 0xB1 };
        k.schema_hash = { kSchema.value, 0 };
        k.compiler_hash = { 1, 0 };
        return k;
    }

    AssetNode source(uint64_t n)
    {
        AssetNode node{};
        node.key = key_n(n);
        node.type = kType;
        node.schema = kSchema;
        node.stage = AssetStage::Source;
        node.payload = std::vector<uint8_t>{ static_cast<uint8_t>(n) };
        return node;
    }

    // How the stub compiler should misbehave on its next run.
    struct Behaviour
    {
        bool fail = false;             // return a Source-stage node
        bool mutate_key = false;
        bool mutate_type = false;
        bool mutate_schema = false;
        bool invalid_handle = false;   // Compiled stage, id/epoch zero
        bool carrier = false;          // Compiled stage, bytes payload (LEGAL)
        bool wrong_payload = false;    // Compiled stage, AssetIR payload
        uint32_t next_id = 1;
        int compiles = 0;
    };

    Behaviour g_behave;

    CompilerRegistry make_registry()
    {
        g_behave = Behaviour{};
        CompilerRegistry r;
        AssetCompiler c{};
        c.input_schema = kSchema;
        c.output_type = kType;
        c.compile =
            [](const AssetNode& in,
               std::span<const AssetNode>,
               std::span<const ResourceHandle>) -> AssetNode
            {
                ++g_behave.compiles;
                AssetNode out = in;
                if (g_behave.fail) {
                    out.stage = AssetStage::Source;
                    return out;
                }
                out.stage = AssetStage::Compiled;
                if (g_behave.mutate_key)    out.key.content_hash.lo ^= 0xDEAD;
                if (g_behave.mutate_type)   out.type = static_cast<AssetType>(78);
                if (g_behave.mutate_schema) out.schema = SchemaID{ 0xDEAD };
                if (g_behave.carrier) {
                    out.payload = std::vector<uint8_t>{ 7, 7, 7 };
                }
                else if (g_behave.wrong_payload) {
                    out.payload = AssetIR{ kType, { 1, 2 } };
                }
                else if (g_behave.invalid_handle) {
                    out.payload = ResourceHandle{ 0, 0, kType };
                }
                else {
                    out.payload = ResourceHandle{ g_behave.next_id++, 1, kType };
                }
                return out;
            };
        r.register_compiler(std::move(c));
        return r;
    }

    bool resolved(const Result<ResourceHandle>& r)
    {
        return std::holds_alternative<ResourceHandle>(r);
    }
}

// ─── Public AssetCache mutation ───────────────────────────────────────────────

TEST(AssetSystemAdversarial, ForgedCacheEntryForUnknownKeyIsNeverServed)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());

    sys.cache().store(key_n(999), ResourceHandle{ 5, 1, kType });

    const auto r = sys.resolve(key_n(999));
    ASSERT_FALSE(resolved(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::NodeNotFound);
    EXPECT_EQ(sys.find_compiled(key_n(999)), nullptr);
    EXPECT_TRUE(sys.query(kType).empty());
}

TEST(AssetSystemAdversarial, ForgedCacheEntryWithNoCompiledNodeTriggersRecompile)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());

    // A cache entry with no matching compiled_nodes_ record must not be
    // trusted: the handle is unbacked by anything this system produced.
    sys.cache().store(key_n(1), ResourceHandle{ 42, 9, kType });

    const auto r = sys.resolve(key_n(1));
    ASSERT_TRUE(resolved(r));
    EXPECT_NE(std::get<ResourceHandle>(r).id, 42u);
    EXPECT_EQ(g_behave.compiles, 1);
}

TEST(AssetSystemAdversarial, CacheAndCompiledNodeDisagreementIsRejected)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));
    ASSERT_EQ(g_behave.compiles, 1);

    // Overwrite the cache so it disagrees with the compiled node it mirrors.
    sys.cache().store(key_n(1), ResourceHandle{ 4242, 7, kType });

    const auto r = sys.resolve(key_n(1));
    ASSERT_TRUE(resolved(r));
    EXPECT_NE(std::get<ResourceHandle>(r).id, 4242u);
    EXPECT_EQ(g_behave.compiles, 2) << "must recompile, not trust either side";
}

TEST(AssetSystemAdversarial, EvictUnregisteredSweepsPoisonedEntries)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));

    sys.cache().store(key_n(999), ResourceHandle{ 5, 1, kType });

    EXPECT_EQ(sys.evict_unregistered(), 1u);
    EXPECT_TRUE(sys.cache().contains(key_n(1)));
}

// ─── Compiler contract violations ─────────────────────────────────────────────

TEST(AssetSystemAdversarial, CompilerContractViolationsLeaveNoStaleState)
{
    struct Case
    {
        const char* name;
        void (*apply)();
    };
    const Case cases[] = {
        { "returns Source stage",    [] { g_behave.fail = true; } },
        { "mutates the key",         [] { g_behave.mutate_key = true; } },
        { "mutates the type",        [] { g_behave.mutate_type = true; } },
        { "mutates the schema",      [] { g_behave.mutate_schema = true; } },
        { "returns invalid handle",  [] { g_behave.invalid_handle = true; } },
        { "returns AssetIR payload", [] { g_behave.wrong_payload = true; } },
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        AssetSystem sys(make_registry());
        c.apply();
        ASSERT_TRUE(sys.register_asset(source(1)));
        ASSERT_TRUE(sys.commit());

        const auto r = sys.resolve(key_n(1));
        ASSERT_FALSE(resolved(r));
        EXPECT_EQ(std::get<ResolveError>(r), ResolveError::CompileFailed);
        EXPECT_EQ(sys.find_compiled(key_n(1)), nullptr);
        EXPECT_TRUE(sys.query(kType).empty());
        EXPECT_TRUE(sys.compiled_terminals().empty());
    }
}

TEST(AssetSystemAdversarial, CarrierNodeWithBytePayloadIsLegal)
{
    AssetSystem sys(make_registry());
    g_behave.carrier = true;
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());

    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));
    // No ResourceHandle, so it is absent from the handle-shaped queries...
    EXPECT_EQ(sys.find_compiled(key_n(1)), nullptr);
    EXPECT_TRUE(sys.query(kType).empty());
    // ...but present as a node, which is how dependents read its bytes.
    EXPECT_NE(sys.find_compiled_node(key_n(1)), nullptr);
}

// ─── Failure and recovery ─────────────────────────────────────────────────────

TEST(AssetSystemAdversarial, FailedRecompileClearsPreviousSuccessAndRecovers)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));
    ASSERT_EQ(sys.query(kType).size(), 1u);

    sys.cache().evict(key_n(1));
    g_behave.fail = true;
    ASSERT_FALSE(resolved(sys.resolve(key_n(1))));

    EXPECT_EQ(sys.find_compiled(key_n(1)), nullptr)
        << "a failed recompile must not leave the previous handle visible";
    EXPECT_TRUE(sys.query(kType).empty());
    EXPECT_FALSE(sys.cache().contains(key_n(1)));

    g_behave.fail = false;
    EXPECT_TRUE(resolved(sys.resolve(key_n(1))));
    EXPECT_EQ(sys.query(kType).size(), 1u);
}

TEST(AssetSystemAdversarial, DependentDoesNotResolveAgainstAFailedDependency)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.register_asset(source(2), { key_n(1) }));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(2))));

    sys.cache().evict(key_n(1));
    sys.cache().evict(key_n(2));
    g_behave.fail = true;

    const auto r = sys.resolve(key_n(2));
    ASSERT_FALSE(resolved(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::DependencyFailed);
    EXPECT_EQ(sys.find_compiled(key_n(2)), nullptr);
    EXPECT_TRUE(sys.query(kType).empty());
}

// ─── Persistent rebuilds ──────────────────────────────────────────────────────

TEST(AssetSystemAdversarial, FailedRecommitPreservesTheWorkingGraph)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));
    const ResourceHandle first = std::get<ResourceHandle>(sys.resolve(key_n(1)));

    // Registered after commit: staged, invisible until the next commit.
    ASSERT_TRUE(sys.register_asset(source(2), { key_n(1) }));
    EXPECT_FALSE(resolved(sys.resolve(key_n(2))));
    ASSERT_TRUE(sys.commit());
    EXPECT_TRUE(resolved(sys.resolve(key_n(2))));
    EXPECT_EQ(std::get<ResourceHandle>(sys.resolve(key_n(1))), first)
        << "a recommit must preserve compiled entries by key";

    // A node naming a dependency that was never registered.
    ASSERT_TRUE(sys.register_asset(source(3), { key_n(4242) }));
    EXPECT_FALSE(sys.commit());

    EXPECT_TRUE(resolved(sys.resolve(key_n(1))));
    EXPECT_TRUE(resolved(sys.resolve(key_n(2))));

    // ...and it recovers once the missing dependency shows up.
    ASSERT_TRUE(sys.register_asset(source(4242)));
    EXPECT_TRUE(sys.commit());
    EXPECT_TRUE(resolved(sys.resolve(key_n(3))));
}

TEST(AssetSystemAdversarial, DuplicateDeterministicKeyIsRejected)
{
    AssetSystem sys(make_registry());
    EXPECT_TRUE(sys.register_asset(source(1)));
    EXPECT_FALSE(sys.register_asset(source(1)))
        << "the same content-addressed key is the same asset";
}

TEST(AssetSystemAdversarial, CyclesAreRejectedAndPublishNoGraph)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1), { key_n(2) }));
    ASSERT_TRUE(sys.register_asset(source(2), { key_n(1) }));
    EXPECT_FALSE(sys.commit());
    EXPECT_EQ(sys.graph(), nullptr);
}

// B1-C5. add_edge() refuses from == to, and commit() used to discard that
// result: the self-edge vanished, commit SUCCEEDED, and the compiler then saw
// one fewer dependency than the registration declared while the node's
// deps_hash still folded the prerequisite the DAG no longer had.
TEST(AssetSystemAdversarial, SelfDependencyIsRejectedAtCommit)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1), { key_n(1) }));
    EXPECT_FALSE(sys.commit());
    EXPECT_EQ(sys.graph(), nullptr);
}

TEST(AssetSystemAdversarial, ReplaceRegisteredAssetsIsAtomic)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));

    std::vector<AssetSystem::RegistrationEntry> missing_dep;
    missing_dep.push_back({ source(5), { key_n(4242) } });
    EXPECT_FALSE(sys.replace_registered_assets(missing_dep));
    EXPECT_TRUE(sys.is_registered(key_n(1)));
    EXPECT_TRUE(resolved(sys.resolve(key_n(1))));

    std::vector<AssetSystem::RegistrationEntry> duplicates;
    duplicates.push_back({ source(6), {} });
    duplicates.push_back({ source(6), {} });
    EXPECT_FALSE(sys.replace_registered_assets(duplicates));

    std::vector<AssetSystem::RegistrationEntry> self_dep;
    self_dep.push_back({ source(7), { key_n(7) } });
    EXPECT_FALSE(sys.replace_registered_assets(self_dep));

    EXPECT_TRUE(resolved(sys.resolve(key_n(1))))
        << "every rejected replacement leaves the working graph intact";
}

// B1-H3. replace_registered_assets() has always rejected the empty key;
// register_asset() accepted it, even though the empty key is the "unwired
// optional port" sentinel in dep_keys and so can never be depended upon.
TEST(AssetSystemAdversarial, EmptyKeyIsRejectedByBothRegistrationPaths)
{
    AssetSystem sys(make_registry());

    AssetNode keyless = source(1);
    keyless.key = AssetKey{};
    EXPECT_FALSE(sys.register_asset(keyless));
    EXPECT_FALSE(sys.is_registered(AssetKey{}));

    std::vector<AssetSystem::RegistrationEntry> entries;
    entries.push_back({ keyless, {} });
    EXPECT_FALSE(sys.replace_registered_assets(entries));
}

// B1-H4. register_asset() had no counterpart, so a node naming a dependency
// that is never registered wedged the system: commit() failed, the bad
// registration stayed in registered_, and every later commit() failed too. The
// only escape was rebuilding the whole authoring set.
TEST(AssetSystemAdversarial, DeregisterWithdrawsANodeThatWedgedCommit)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));

    // A node whose declared dependency does not exist.
    ASSERT_TRUE(sys.register_asset(source(2), { key_n(4242) }));
    ASSERT_FALSE(sys.commit());
    ASSERT_FALSE(sys.commit()) << "and it stays broken";

    EXPECT_FALSE(sys.deregister_asset(key_n(999))) << "never registered";
    ASSERT_TRUE(sys.deregister_asset(key_n(2)));
    EXPECT_FALSE(sys.is_registered(key_n(2)));

    EXPECT_TRUE(sys.commit());
    EXPECT_TRUE(resolved(sys.resolve(key_n(1))));
}

// The erased slot shifts every later index down, so the index must be rebuilt.
// A stale slot number would wire the next commit()'s edges to the wrong node --
// which resolves successfully and silently compiles against the wrong input.
TEST(AssetSystemAdversarial, DeregisterKeepsRemainingEdgesPointingAtTheRightNodes)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.register_asset(source(2)));   // erased below
    ASSERT_TRUE(sys.register_asset(source(3)));
    ASSERT_TRUE(sys.register_asset(source(4), { key_n(3) }));
    ASSERT_TRUE(sys.commit());

    ASSERT_TRUE(sys.deregister_asset(key_n(2)));
    ASSERT_TRUE(sys.commit());

    for (const AssetSystem::RegistrationEntry& e : sys.registered_assets()) {
        EXPECT_TRUE(sys.is_registered(e.node.key));
    }
    ASSERT_TRUE(resolved(sys.resolve(key_n(4))));

    // Node 4's single prerequisite must still be node 3.
    const AssetGraph* g = sys.graph();
    ASSERT_NE(g, nullptr);
    const NodeHandle nh = find_asset_node(sys.index(), key_n(4));
    ASSERT_NE(nh, INVALID_ASSET_NODE);
    const auto prereqs = prerequisites(*g, nh);
    ASSERT_EQ(prereqs.size(), 1u);
    EXPECT_EQ(wz::core::graph::node_data(*g, prereqs[0]).key, key_n(3));
}

// B1-H5. AssetCache::invalidate() marks only the cache slot stale, but
// compiled_nodes_ is documented as parallel to it -- so contains() reported
// absent while find_compiled()/query() still handed out the stale handle.
TEST(AssetSystemAdversarial, InvalidateKeepsBothQuerySurfacesInStep)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_TRUE(sys.commit());
    ASSERT_TRUE(resolved(sys.resolve(key_n(1))));
    ASSERT_NE(sys.find_compiled(key_n(1)), nullptr);
    ASSERT_EQ(sys.query(kType).size(), 1u);

    sys.invalidate(key_n(1));

    EXPECT_FALSE(sys.cache().contains(key_n(1)));
    EXPECT_EQ(sys.find_compiled(key_n(1)), nullptr);
    EXPECT_TRUE(sys.query(kType).empty());

    // ...and the point of a soft invalidate: the next resolve recompiles.
    const int before = g_behave.compiles;
    EXPECT_TRUE(resolved(sys.resolve(key_n(1))));
    EXPECT_EQ(g_behave.compiles, before + 1);
    EXPECT_NE(sys.find_compiled(key_n(1)), nullptr);
    EXPECT_EQ(sys.query(kType).size(), 1u);
}

// B1-H6. The committed_ guard was assert-only, so a pre-commit call
// dereferenced an empty optional in release — UB in exactly the build that
// ships. resolve() has always treated "not committed" as a clean miss.
TEST(AssetSystemAdversarial, ResolveAllBeforeCommitIsACleanNoOp)
{
    AssetSystem sys(make_registry());
    ASSERT_TRUE(sys.register_asset(source(1)));
    ASSERT_FALSE(sys.committed());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys.resolve_all(&errors), 0u);
    EXPECT_TRUE(sys.query(kType).empty());
}
