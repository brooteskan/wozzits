#include <gtest/gtest.h>
#include <asset/system.h>
#include <asset/types.h>
#include <asset/compiler.h>

using namespace wz::asset;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static AssetNode make_node(const AssetKey& key, AssetType type, SchemaID schema,
    AssetStage stage = AssetStage::Source)
{
    AssetNode n;
    n.key = key;
    n.type = type;
    n.schema = schema;
    n.stage = stage;
    n.payload = std::vector<uint8_t>{};   // empty source bytes
    return n;
}

// A trivial compiler that always produces a valid GPUHandle.
// id = low 32 bits of content_hash.lo so different inputs yield different handles.
static AssetCompiler make_stub_compiler(SchemaID schema, AssetType type)
{
    return AssetCompiler{
        .input_schema = schema,
        .output_type = type,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const GPUHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            GPUHandle h;
            // Produce a unique-ish id from the content hash so we can tell
            // handles apart in multi-node tests.
            h.id = static_cast<uint32_t>(input.key.content_hash.lo & 0xFFFF'FFFFu);
            h.epoch = 1;
            // Ensure non-zero (id==0 means invalid).
            if (h.id == 0) h.id = 0xDEAD;
            out.payload = h;
            return out;
        }
    };
}

// A compiler that always signals failure (returns an un-compiled node).
static AssetCompiler make_failing_compiler(SchemaID schema, AssetType type)
{
    return AssetCompiler{
        .input_schema = schema,
        .output_type = type,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const GPUHandle>) -> AssetNode
        {
            // Returns the node unchanged — still Source stage, no GPUHandle.
            return input;
        }
    };
}

// ─── Schema / Type constants used across tests ────────────────────────────────

static constexpr SchemaID kMeshSchema{ 123 };
static constexpr SchemaID kTexSchema{ 456 };
static constexpr SchemaID kMatSchema{ 789 };

static constexpr AssetKey kKeyA{ Hash{1,1},  Hash{2,2},  Hash{3,3},  Hash{4,4} };
static constexpr AssetKey kKeyB{ Hash{5,5},  Hash{6,6},  Hash{7,7},  Hash{8,8} };
static constexpr AssetKey kKeyC{ Hash{9,9},  Hash{10,10},Hash{11,11},Hash{12,12} };
static constexpr AssetKey kKeyD{ Hash{13,13},Hash{14,14},Hash{15,15},Hash{16,16} };


// ─── Fixture ──────────────────────────────────────────────────────────────────

class AssetSystemTest : public ::testing::Test {
protected:
    CompilerRegistry registry;
    // Populated in SetUp so subclasses can override before build.
    std::unique_ptr<AssetSystem> sys;

    void SetUp() override {
        // Register stub compilers for the types used across all tests.
        registry.register_compiler(make_stub_compiler(kMeshSchema, AssetType::Mesh));
        registry.register_compiler(make_stub_compiler(kTexSchema, AssetType::Texture));
        registry.register_compiler(make_stub_compiler(kMatSchema, AssetType::Material));
        sys = std::make_unique<AssetSystem>(std::move(registry));
    }
};


// ─── Registration ─────────────────────────────────────────────────────────────

TEST_F(AssetSystemTest, RegisterAsset_Succeeds)
{
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
}

TEST_F(AssetSystemTest, RegisterAsset_DuplicateKeyRejected)
{
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    // Second registration with the same key must be rejected.
    EXPECT_FALSE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
}

TEST_F(AssetSystemTest, RegisterAsset_WithValidDependency)
{
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    // kKeyB depends on kKeyA — both registered, so commit should succeed.
    EXPECT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    EXPECT_TRUE(sys->commit());
}


// ─── Commit ───────────────────────────────────────────────────────────────────

TEST_F(AssetSystemTest, Commit_RejectsMissingDependencyKey)
{
    // kKeyB declares kKeyA as a dep, but kKeyA is never registered.
    EXPECT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    // commit() must reject the missing dep reference.
    EXPECT_FALSE(sys->commit());
}

TEST_F(AssetSystemTest, Commit_DetectsCycle)
{
    // Build a genuine two-node cycle: A depends on B AND B depends on A.
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema),
        { kKeyB }));   // A → B (B is prereq of A)
    EXPECT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));   // B → A (A is prereq of B)
    EXPECT_FALSE(sys->commit());
}

TEST_F(AssetSystemTest, Commit_IdempotentOnSuccess)
{
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    EXPECT_TRUE(sys->commit());
    EXPECT_TRUE(sys->committed());
}


// ─── Resolve — basic ──────────────────────────────────────────────────────────

TEST_F(AssetSystemTest, Resolve_BeforeCommit_ReturnsNodeNotFound)
{
    // resolve() before commit() must not crash — should return NodeNotFound.
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::NodeNotFound);
}

TEST_F(AssetSystemTest, Resolve_UnknownKey_ReturnsNodeNotFound)
{
    ASSERT_TRUE(sys->commit());
    auto r = sys->resolve(kKeyA);   // kKeyA was never registered
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::NodeNotFound);
}

TEST_F(AssetSystemTest, Resolve_SingleNode_ReturnsValidHandle)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r));
    EXPECT_TRUE(std::get<GPUHandle>(r).valid());
}

TEST_F(AssetSystemTest, Resolve_SecondCall_ReturnsSameHandleFromCache)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r1 = sys->resolve(kKeyA);
    auto r2 = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r1));
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r2));
    EXPECT_EQ(std::get<GPUHandle>(r1), std::get<GPUHandle>(r2));
}

TEST_F(AssetSystemTest, Resolve_NoCompilerRegistered_ReturnsCompilerNotFound)
{
    // kKeyA uses SchemaID{999} for which no compiler was registered.
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, SchemaID{ 999 })));
    ASSERT_TRUE(sys->commit());

    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::CompilerNotFound);
}

TEST_F(AssetSystemTest, Resolve_FailingCompiler_ReturnsCompileFailed)
{
    // Override the mesh compiler with one that always fails.
    CompilerRegistry reg2;
    reg2.register_compiler(make_failing_compiler(kMeshSchema, AssetType::Mesh));
    AssetSystem sys2(std::move(reg2));

    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto r = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::CompileFailed);
}


// ─── Resolve — dependency chain ───────────────────────────────────────────────

TEST_F(AssetSystemTest, Resolve_DependencyChain_ResolvesInOrder)
{
    // Chain: C depends on B, B depends on A.
    // All should resolve; C's handle is valid.
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyC, AssetType::Material, kMatSchema),
        { kKeyB }));
    ASSERT_TRUE(sys->commit());

    auto rC = sys->resolve(kKeyC);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(rC));
    EXPECT_TRUE(std::get<GPUHandle>(rC).valid());

    // Prerequisites must also be in the cache after resolving C.
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_TRUE(sys->cache().contains(kKeyB));
}

TEST_F(AssetSystemTest, Resolve_FailedDependency_ReturnsDependencyFailed)
{
    // kKeyA has no compiler → its resolve will return CompilerNotFound.
    // kKeyB depends on kKeyA → resolve(kKeyB) must propagate DependencyFailed.
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, SchemaID{ 999 })));
    // Register compiler for kKeyB but not kKeyA (SchemaID{999} is unregistered).
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));
    ASSERT_TRUE(sys->commit());

    auto r = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::DependencyFailed);
}

TEST_F(AssetSystemTest, Resolve_SharedDependency_CompiledOnce)
{
    // Diamond: C and D both depend on A.
    // A's compiler should only be invoked once.
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kTexSchema,
        .output_type = AssetType::Texture,
        .compile = [&](const AssetNode& in,
                       std::span<const AssetNode>,
                       std::span<const GPUHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = in;
            out.stage = AssetStage::Compiled;
            out.payload = GPUHandle{ 42, 1 };
            return out;
        }
        });
    reg2.register_compiler(make_stub_compiler(kMeshSchema, AssetType::Mesh));
    AssetSystem sys2(std::move(reg2));

    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyC, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    sys2.resolve(kKeyB);
    sys2.resolve(kKeyC);
    EXPECT_EQ(compile_count, 1u);
}


// ─── resolve_all ──────────────────────────────────────────────────────────────

TEST_F(AssetSystemTest, ResolveAll_CompilesCachedEverything)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys->commit());

    uint32_t ok = sys->resolve_all();
    EXPECT_EQ(ok, 2u);
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_TRUE(sys->cache().contains(kKeyB));
}

TEST_F(AssetSystemTest, ResolveAll_CollectsErrors)
{
    // kKeyA has no compiler → will fail; kKeyB should succeed.
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, SchemaID{ 999 })));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys->commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    uint32_t ok = sys->resolve_all(&errors);

    EXPECT_EQ(ok, 1u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyA);
    EXPECT_EQ(errors[0].second, ResolveError::CompilerNotFound);
}


// ─── Cache ────────────────────────────────────────────────────────────────────

TEST_F(AssetSystemTest, Cache_StoredAfterResolve)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r));

    auto cached = sys->cache().lookup(kKeyA);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*cached, std::get<GPUHandle>(r));
}

TEST_F(AssetSystemTest, Cache_HardEvict_RemovesEntry)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys->commit());

    sys->resolve(kKeyA);
    sys->resolve(kKeyB);

    sys->cache().evict(kKeyA);
    EXPECT_FALSE(sys->cache().contains(kKeyA));
    EXPECT_TRUE(sys->cache().contains(kKeyB));   // kKeyB unaffected
}

TEST_F(AssetSystemTest, Cache_SoftInvalidate_HidesEntryUntilRestore)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    sys->resolve(kKeyA);
    ASSERT_TRUE(sys->cache().contains(kKeyA));

    sys->cache().invalidate(kKeyA);
    EXPECT_FALSE(sys->cache().contains(kKeyA));   // appears absent after invalidate

    // Re-resolving re-populates the cache.
    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r));
    EXPECT_TRUE(sys->cache().contains(kKeyA));
}

TEST_F(AssetSystemTest, CacheInvalidateTriggersRecompile_0)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg;
    reg.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&](const AssetNode& in,
                       std::span<const AssetNode>,
                       std::span<const GPUHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = in;
            out.stage = AssetStage::Compiled;
            out.payload = GPUHandle{ 42, 1 };
            return out;
        }
        });
    AssetSystem sys2(std::move(reg));

    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto r1 = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r1));
    EXPECT_EQ(compile_count, 1u);   // compiled once

    sys2.cache().invalidate(kKeyA);

    auto r2 = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r2));
    EXPECT_EQ(compile_count, 2u);   // recompiled after invalidation

    // Same input → same output. This is correct and expected.
    EXPECT_EQ(std::get<GPUHandle>(r1), std::get<GPUHandle>(r2));
}

// sugartits' tests ====================================================

TEST_F(AssetSystemTest, Resolve_MultipleCallsNoChanges)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r1 = sys->resolve(kKeyA);
    auto r2 = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r1));
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r2));
    EXPECT_EQ(std::get<GPUHandle>(r1), std::get<GPUHandle>(r2));
}

TEST_F(AssetSystemTest, Resolve_DependencyFailure)
{
    // Register a failing asset (no compiler available)
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, SchemaID{ 999 })));
    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Mesh, kMeshSchema), { kKeyA }));
    ASSERT_TRUE(sys->commit());

    // Resolve kKeyB which depends on kKeyA — should fail due to kKeyA being unresolved.
    auto r = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(r));
    EXPECT_EQ(std::get<ResolveError>(r), ResolveError::DependencyFailed);
}



TEST_F(AssetSystemTest, LargeDependencyChain)
{
    constexpr size_t chain_length = 100;
    std::vector<AssetKey> keys(chain_length);
    for (size_t i = 0; i < chain_length; ++i) {
        keys[i] = AssetKey{ Hash{1,1}, Hash{2,2}, Hash{3,3}, Hash{4,4} };
    }

    // Register assets with a deep dependency chain
    for (size_t i = 0; i < chain_length - 1; ++i) {
        sys->register_asset(make_node(keys[i], AssetType::Mesh, kMeshSchema), { keys[i + 1] });
    }
    sys->register_asset(make_node(keys[chain_length - 1], AssetType::Mesh, kMeshSchema));

    ASSERT_TRUE(sys->commit());

    // Resolve the last asset; it should resolve the entire chain
    auto r = sys->resolve(keys[0]);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r));
}

TEST_F(AssetSystemTest, CacheEvictionAndCompilation)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys->commit());

    // Resolve once to store in cache
    sys->resolve(kKeyA);
    sys->cache().evict(kKeyA);

    // Eviction and resolve should trigger a fresh compile
    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<GPUHandle>(r));
}

TEST_F(AssetSystemTest, CacheClearOnDeviceReset)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    // Resolve once to populate the cache
    sys->resolve(kKeyA);
    ASSERT_TRUE(sys->cache().contains(kKeyA));

    // Simulate a device reset (clear cache)
    sys->cache().clear();
    EXPECT_FALSE(sys->cache().contains(kKeyA));  // Cache should be cleared
}

TEST_F(AssetSystemTest, PerformanceTest_ManyAssets)
{
    constexpr size_t num_assets = 10000;

    for (size_t i = 0; i < num_assets; ++i) {
        sys->register_asset(make_node(AssetKey{ Hash{i,i}, Hash{i + 1,i + 1}, Hash{i + 2,i + 2}, Hash{i + 3,i + 3} },
            AssetType::Texture, kTexSchema));
    }

    ASSERT_TRUE(sys->commit());

    // Resolve all registered assets
    uint32_t resolved = sys->resolve_all();
    EXPECT_EQ(resolved, num_assets);
}

