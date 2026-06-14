#include <gtest/gtest.h>
#include <asset/system.h>
#include <asset/types.h>
#include <asset/compiler.h>

#include <algorithm>
#include <optional>
#include <string>

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

// A trivial compiler that always produces a valid ResourceHandle.
// id = low 32 bits of content_hash.lo so different inputs yield different handles.
static AssetCompiler make_stub_compiler(SchemaID schema, AssetType type)
{
    return AssetCompiler{
        .input_schema = schema,
        .output_type = type,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode
        {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            ResourceHandle h;
            // Produce a unique-ish id from the content hash so we can tell
            // handles apart in multi-node tests.
            h.id = static_cast<uint32_t>(input.key.content_hash.lo & 0xFFFF'FFFFu);
            h.epoch = 1;
            h.type = input.type;
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
                      std::span<const ResourceHandle>) -> AssetNode
        {
            // Returns the node unchanged — still Source stage, no ResourceHandle.
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

class CountingExternalCacheProvider final : public ExternalCacheProvider
{
public:
    bool can_load(SchemaID, AssetType, const AssetKey&) const override
    {
        ++can_load_calls;
        return false;
    }

    std::optional<ResourceHandle> load(
        SchemaID,
        AssetType,
        const AssetKey&) override
    {
        ++load_calls;
        return std::nullopt;
    }

    mutable uint32_t can_load_calls = 0;
    uint32_t load_calls = 0;
};

class SingleEntryExternalCacheProvider final : public ExternalCacheProvider
{
public:
    explicit SingleEntryExternalCacheProvider(
        AssetKey key,
        ResourceHandle handle)
        : key_(key)
        , handle_(handle)
    {
    }

    bool can_load(SchemaID, AssetType, const AssetKey& key) const override
    {
        ++can_load_calls;
        return key == key_;
    }

    std::optional<ResourceHandle> load(
        SchemaID,
        AssetType,
        const AssetKey& key) override
    {
        ++load_calls;
        if (key == key_) {
            return handle_;
        }
        return std::nullopt;
    }

    mutable uint32_t can_load_calls = 0;
    uint32_t load_calls = 0;

private:
    AssetKey key_{};
    ResourceHandle handle_{};
};

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

TEST_F(AssetSystemTest, RegisterAsset_EmptyDependencySlotsAreOptional)
{
    EXPECT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    EXPECT_TRUE(sys->register_asset(
        make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA, AssetKey{} }));

    EXPECT_TRUE(sys->commit());

    const AssetGraph* graph = sys->graph();
    ASSERT_NE(graph, nullptr);
    const NodeHandle mesh_node = find_asset_node(sys->index(), kKeyB);
    ASSERT_NE(mesh_node, INVALID_ASSET_NODE);

    const auto deps = prerequisites(*graph, mesh_node);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(wz::core::graph::node_data(*graph, deps[0]).key, kKeyA);

    const auto resolved = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(resolved));
    EXPECT_TRUE(std::get<ResourceHandle>(resolved).valid());
}

TEST_F(AssetSystemTest, RegisterAsset_DefaultResidencyIntentIsRuntimeResident)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    const AssetGraph* graph = sys->graph();
    ASSERT_NE(graph, nullptr);
    const NodeHandle node = find_asset_node(sys->index(), kKeyA);
    ASSERT_NE(node, INVALID_ASSET_NODE);
    EXPECT_EQ(
        wz::core::graph::node_data(*graph, node).residency,
        ResidencyIntent::RuntimeResident);
}

TEST_F(AssetSystemTest, RegisterAsset_ExplicitResidencyIntentSurvivesResolve)
{
    AssetNode node = make_node(kKeyA, AssetType::Mesh, kMeshSchema);
    node.residency = ResidencyIntent::CompileOnly;

    ASSERT_TRUE(sys->register_asset(std::move(node)));
    ASSERT_TRUE(sys->commit());

    auto resolved = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(resolved));

    const auto* compiled = sys->find_compiled(kKeyA);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->node->residency, ResidencyIntent::CompileOnly);

    const std::string dot = sys->debug_graph_dot();
    EXPECT_NE(dot.find("residency=compile-only"), std::string::npos);
}

TEST_F(AssetSystemTest, RegisterDemandRoot_AddsExplicitSyntheticNode)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->register_demand_root(DemandRoot::GPURuntime, { kKeyA }));
    ASSERT_TRUE(sys->commit());

    const AssetGraph* graph = sys->graph();
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(wz::core::graph::node_count(*graph), 2u);

    const NodeHandle root_node =
        find_asset_node(sys->index(), make_demand_root_key(DemandRoot::GPURuntime));
    ASSERT_NE(root_node, INVALID_ASSET_NODE);

    const AssetNode& root = wz::core::graph::node_data(*graph, root_node);
    EXPECT_EQ(root.kind, AssetNodeKind::DemandRoot);
    EXPECT_EQ(root.demand_root, DemandRoot::GPURuntime);
    EXPECT_EQ(prerequisites(*graph, root_node).size(), 1u);

    const std::string dot = sys->debug_graph_dot();
    EXPECT_NE(dot.find("kind=demand-root"), std::string::npos);
    EXPECT_NE(dot.find("root=gpu-runtime"), std::string::npos);
    EXPECT_NE(dot.find("shape=\"diamond\""), std::string::npos);
}

TEST_F(AssetSystemTest, RegisterDemandRoot_RejectsDuplicateAndNone)
{
    EXPECT_FALSE(sys->register_demand_root(DemandRoot::None));
    EXPECT_TRUE(sys->register_demand_root(DemandRoot::Editor));
    EXPECT_FALSE(sys->register_demand_root(DemandRoot::Editor));
}

TEST_F(AssetSystemTest, Commit_RejectsDemandRootMissingDependencyKey)
{
    EXPECT_TRUE(sys->register_demand_root(DemandRoot::CPURuntime, { kKeyA }));
    EXPECT_FALSE(sys->commit());
}

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
    EXPECT_TRUE(sys->commit());
    EXPECT_TRUE(sys->committed());
}

TEST_F(AssetSystemTest, RegisterAfterCommit_RecommitMakesNodeResolvable)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto before_recommit = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(before_recommit));
    EXPECT_EQ(std::get<ResolveError>(before_recommit), ResolveError::NodeNotFound);

    ASSERT_TRUE(sys->register_asset(make_node(kKeyB, AssetType::Texture, kTexSchema)));

    before_recommit = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(before_recommit));
    EXPECT_EQ(std::get<ResolveError>(before_recommit), ResolveError::NodeNotFound);

    ASSERT_TRUE(sys->commit());

    auto after_recommit = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(after_recommit));
    EXPECT_TRUE(std::get<ResourceHandle>(after_recommit).valid());
}

TEST_F(AssetSystemTest, RecommitPreservesCachedCompiledAssets)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&](const AssetNode& input,
                       std::span<const AssetNode>,
                       std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            ResourceHandle h;
            h.id = static_cast<uint32_t>(
                input.key.content_hash.lo & 0xFFFF'FFFFu);
            if (h.id == 0) h.id = 0xDEAD;
            h.epoch = 1;
            h.type = input.type;
            out.payload = h;
            return out;
        }
    });
    reg2.register_compiler(make_stub_compiler(kTexSchema, AssetType::Texture));

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto first = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(first));
    EXPECT_EQ(compile_count, 1u);

    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    auto second = sys2.resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(second));
    EXPECT_EQ(compile_count, 1u);
    EXPECT_EQ(sys2.cache().lookup(kKeyA), std::get<ResourceHandle>(first));
}

TEST_F(AssetSystemTest, ResolveAllAfterRecommitKeepsExistingCacheUsable)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    std::vector<std::pair<AssetKey, ResolveError>> first_errors;
    EXPECT_EQ(sys->resolve_all(&first_errors), 1u);
    EXPECT_TRUE(first_errors.empty());

    const auto first_handle = sys->cache().lookup(kKeyA);
    ASSERT_TRUE(first_handle.has_value());

    ASSERT_TRUE(sys->register_asset(
        make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));
    ASSERT_TRUE(sys->commit());

    std::vector<std::pair<AssetKey, ResolveError>> second_errors;
    EXPECT_EQ(sys->resolve_all(&second_errors), 2u);
    EXPECT_TRUE(second_errors.empty());
    EXPECT_EQ(sys->cache().lookup(kKeyA), first_handle);
    EXPECT_TRUE(sys->cache().contains(kKeyB));
}

TEST_F(AssetSystemTest, DuplicateDeterministicKeyAfterCommitIsRejectedWithoutChangingCache)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto first = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(first));

    EXPECT_FALSE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto second = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(second));
    EXPECT_EQ(std::get<ResourceHandle>(second), std::get<ResourceHandle>(first));
}

TEST_F(AssetSystemTest, FailedSourceDoesNotLeaveStaleCompiledState)
{
    CompilerRegistry reg2;
    reg2.register_compiler(make_failing_compiler(kMeshSchema, AssetType::Mesh));

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyA);
    EXPECT_EQ(errors[0].second, ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, DependentDoesNotResolveAgainstFailedDependency)
{
    CompilerRegistry reg2;
    reg2.register_compiler(make_failing_compiler(kMeshSchema, AssetType::Mesh));
    reg2.register_compiler(make_stub_compiler(kTexSchema, AssetType::Texture));

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);

    ASSERT_EQ(errors.size(), 2u);
    const auto source = std::find_if(
        errors.begin(),
        errors.end(),
        [](const auto& e) { return e.first == kKeyA; });
    const auto dependent = std::find_if(
        errors.begin(),
        errors.end(),
        [](const auto& e) { return e.first == kKeyB; });

    ASSERT_NE(source, errors.end());
    ASSERT_NE(dependent, errors.end());
    EXPECT_EQ(source->second, ResolveError::CompileFailed);
    EXPECT_EQ(dependent->second, ResolveError::DependencyFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_FALSE(sys2.cache().contains(kKeyB));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
    EXPECT_EQ(sys2.find_compiled(kKeyB), nullptr);
}

TEST_F(AssetSystemTest, NodeResolveStatusMirrorsJobStatus)
{
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Pending),
        static_cast<uint8_t>(wz::jobs::JobStatus::Pending));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Ready),
        static_cast<uint8_t>(wz::jobs::JobStatus::Ready));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Queued),
        static_cast<uint8_t>(wz::jobs::JobStatus::Queued));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Running),
        static_cast<uint8_t>(wz::jobs::JobStatus::Running));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Done),
        static_cast<uint8_t>(wz::jobs::JobStatus::Done));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Skipped),
        static_cast<uint8_t>(wz::jobs::JobStatus::Skipped));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Failed),
        static_cast<uint8_t>(wz::jobs::JobStatus::Failed));
    EXPECT_EQ(
        static_cast<uint8_t>(NodeResolveStatus::Cancelled),
        static_cast<uint8_t>(wz::jobs::JobStatus::Cancelled));
}

TEST_F(AssetSystemTest, FailedNodesKeepQueryableResolveState)
{
    CompilerRegistry reg2;
    reg2.register_compiler(make_failing_compiler(kMeshSchema, AssetType::Mesh));
    reg2.register_compiler(make_stub_compiler(kTexSchema, AssetType::Texture));

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);

    const auto source_state = sys2.node_resolve_state(kKeyA);
    ASSERT_TRUE(source_state.has_value());
    EXPECT_EQ(source_state->status, NodeResolveStatus::Failed);
    ASSERT_TRUE(source_state->error.has_value());
    EXPECT_EQ(*source_state->error, ResolveError::CompileFailed);

    const auto dependent_state = sys2.node_resolve_state(kKeyB);
    ASSERT_TRUE(dependent_state.has_value());
    EXPECT_EQ(dependent_state->status, NodeResolveStatus::Failed);
    ASSERT_TRUE(dependent_state->error.has_value());
    EXPECT_EQ(*dependent_state->error, ResolveError::DependencyFailed);
}

TEST_F(AssetSystemTest, SuccessfulReresolveClearsPreviousError)
{
    bool fail_compile = true;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&fail_compile](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            if (fail_compile) {
                return input;
            }

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 9, 1, AssetType::Mesh };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto failed = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(failed));
    ASSERT_TRUE(sys2.node_resolve_state(kKeyA).has_value());
    EXPECT_EQ(
        sys2.node_resolve_state(kKeyA)->status,
        NodeResolveStatus::Failed);

    fail_compile = false;
    auto resolved = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(resolved));

    const auto state = sys2.node_resolve_state(kKeyA);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->status, NodeResolveStatus::Done);
    EXPECT_FALSE(state->error.has_value());
}

TEST_F(AssetSystemTest, CompiledResourceHandleMustBeValid)
{
    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{};
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyA);
    EXPECT_EQ(errors[0].second, ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, CompiledResourceNodeMustKeepInputIdentity)
{
    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode {
            AssetNode out = input;
            out.key = kKeyB;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 42, 1, AssetType::Mesh };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyA);
    EXPECT_EQ(errors[0].second, ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, CompiledResourceNodeMustKeepInputType)
{
    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode {
            AssetNode out = input;
            out.type = AssetType::Texture;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 42, 1, AssetType::Texture };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyA);
    EXPECT_EQ(errors[0].second, ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, CompiledStageRejectsIntermediatePayload)
{
    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = AssetIR{ .type = AssetType::Mesh };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].second, ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, CompiledCarrierPayloadCanSatisfyDependent)
{
    static constexpr SchemaID kCarrierSchema{ 901 };
    static constexpr SchemaID kConsumerSchema{ 902 };
    static constexpr AssetType kCarrierType = AssetType::ShaderSource;
    static constexpr AssetType kConsumerType = AssetType::Shader;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kCarrierSchema,
        .output_type = kCarrierType,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode>,
                      std::span<const ResourceHandle>) -> AssetNode {
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = std::vector<uint8_t>{ 'o', 'k' };
            return out;
        }
    });
    reg2.register_compiler(AssetCompiler{
        .input_schema = kConsumerSchema,
        .output_type = kConsumerType,
        .compile = [](const AssetNode& input,
                      std::span<const AssetNode> dep_nodes,
                      std::span<const ResourceHandle> dep_handles) -> AssetNode {
            if (dep_nodes.size() != 1 || dep_handles.size() != 1)
                return input;

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
            if (!bytes || *bytes != std::vector<uint8_t>{ 'o', 'k' })
                return input;
            if (dep_handles[0].valid())
                return input;

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 77, 1, kConsumerType };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, kCarrierType, kCarrierSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, kConsumerType, kConsumerSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys2.resolve_all(&errors), 2u);
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(sys2.cache().contains(kKeyA));
    EXPECT_TRUE(sys2.cache().contains(kKeyB));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
    ASSERT_NE(sys2.find_compiled(kKeyB), nullptr);
}

TEST_F(AssetSystemTest, RecompileAfterInvalidationUpdatesCompiledNode)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&compile_count](const AssetNode& input,
                                    std::span<const AssetNode>,
                                    std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{
                .id = compile_count,
                .epoch = 1,
                .type = AssetType::Mesh,
            };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto first = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(first));
    EXPECT_EQ(std::get<ResourceHandle>(first).id, 1u);

    const auto* first_compiled = sys2.find_compiled(kKeyA);
    ASSERT_NE(first_compiled, nullptr);
    EXPECT_EQ(first_compiled->handle.id, 1u);

    sys2.cache().invalidate(kKeyA);

    auto second = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(second));
    EXPECT_EQ(std::get<ResourceHandle>(second).id, 2u);

    const auto* second_compiled = sys2.find_compiled(kKeyA);
    ASSERT_NE(second_compiled, nullptr);
    EXPECT_EQ(second_compiled->handle.id, 2u);
}

TEST_F(AssetSystemTest, ResolveIgnoresCacheEntryForUnknownKey)
{
    ASSERT_TRUE(sys->commit());

    sys->cache().store(kKeyA, ResourceHandle{
        .id = 123,
        .epoch = 1,
        .type = AssetType::Mesh,
    });

    auto result = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(result));
    EXPECT_EQ(std::get<ResolveError>(result), ResolveError::NodeNotFound);
}

TEST_F(AssetSystemTest, ResolveIgnoresCacheEntryWithoutCompiledNode)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&compile_count](const AssetNode& input,
                                    std::span<const AssetNode>,
                                    std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{
                .id = compile_count,
                .epoch = 1,
                .type = AssetType::Mesh,
            };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    sys2.cache().store(kKeyA, ResourceHandle{
        .id = 999,
        .epoch = 1,
        .type = AssetType::Mesh,
    });

    auto result = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(result));
    EXPECT_EQ(std::get<ResourceHandle>(result).id, 1u);
    EXPECT_EQ(compile_count, 1u);

    const auto* compiled = sys2.find_compiled(kKeyA);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->handle.id, 1u);
}

TEST_F(AssetSystemTest, FailedRecompileClearsStaleCompiledNode)
{
    bool fail_next = false;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&fail_next](const AssetNode& input,
                                std::span<const AssetNode>,
                                std::span<const ResourceHandle>) -> AssetNode {
            if (fail_next)
                return input;

            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{
                .id = 1,
                .epoch = 1,
                .type = AssetType::Mesh,
            };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto first = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(first));
    ASSERT_NE(sys2.find_compiled(kKeyA), nullptr);

    fail_next = true;
    sys2.cache().invalidate(kKeyA);

    auto second = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(second));
    EXPECT_EQ(std::get<ResolveError>(second), ResolveError::CompileFailed);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_EQ(sys2.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, ResetSystemAfterFailedSourceAllowsCleanRecovery)
{
    CompilerRegistry bad_registry;
    bad_registry.register_compiler(make_failing_compiler(kMeshSchema, AssetType::Mesh));

    AssetSystem bad_system(std::move(bad_registry));
    ASSERT_TRUE(bad_system.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(bad_system.commit());

    std::vector<std::pair<AssetKey, ResolveError>> bad_errors;
    EXPECT_EQ(bad_system.resolve_all(&bad_errors), 0u);
    ASSERT_EQ(bad_errors.size(), 1u);
    EXPECT_EQ(bad_errors[0].second, ResolveError::CompileFailed);

    CompilerRegistry good_registry;
    good_registry.register_compiler(make_stub_compiler(kMeshSchema, AssetType::Mesh));

    AssetSystem good_system(std::move(good_registry));
    ASSERT_TRUE(good_system.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(good_system.commit());

    std::vector<std::pair<AssetKey, ResolveError>> good_errors;
    EXPECT_EQ(good_system.resolve_all(&good_errors), 1u);
    EXPECT_TRUE(good_errors.empty());
    EXPECT_TRUE(good_system.cache().contains(kKeyA));
    EXPECT_NE(good_system.find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, FailedRecommitLeavesPreviousGraphActive)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto original = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(original));

    ASSERT_TRUE(sys->register_asset(
        make_node(kKeyB, AssetType::Texture, kTexSchema),
        { kKeyD }));
    EXPECT_FALSE(sys->commit());

    auto still_resolves = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(still_resolves));
    EXPECT_EQ(std::get<ResourceHandle>(still_resolves),
        std::get<ResourceHandle>(original));

    auto missing_delta = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResolveError>(missing_delta));
    EXPECT_EQ(std::get<ResolveError>(missing_delta), ResolveError::NodeNotFound);

    ASSERT_TRUE(sys->register_asset(
        make_node(kKeyD, AssetType::Material, kMatSchema)));
    ASSERT_TRUE(sys->commit());
    auto resolved_delta = sys->resolve(kKeyB);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(resolved_delta));
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
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(r));
    EXPECT_TRUE(std::get<ResourceHandle>(r).valid());
}

TEST_F(AssetSystemTest, Resolve_SecondCall_ReturnsSameHandleFromCache)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r1 = sys->resolve(kKeyA);
    auto r2 = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(r1));
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(r2));
    EXPECT_EQ(std::get<ResourceHandle>(r1), std::get<ResourceHandle>(r2));
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
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(rC));
    EXPECT_TRUE(std::get<ResourceHandle>(rC).valid());

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
                       std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = in;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 42, 1 };
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

TEST_F(AssetSystemTest, ResolveAll_SkipsDemandRootsWithoutChangingAssetTerminals)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->register_demand_root(DemandRoot::GPURuntime, { kKeyA }));
    ASSERT_TRUE(sys->commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys->resolve_all(&errors), 1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_FALSE(sys->cache().contains(make_demand_root_key(DemandRoot::GPURuntime)));
    EXPECT_EQ(
        sys->find_compiled(make_demand_root_key(DemandRoot::GPURuntime)),
        nullptr);

    const std::vector<AssetSystem::CompiledAsset> terminals =
        sys->compiled_terminals();
    ASSERT_EQ(terminals.size(), 1u);
    EXPECT_EQ(terminals[0].key, kKeyA);
}

TEST_F(AssetSystemTest, ResolveRoots_CacheRequiredMissDoesNotResolvePrerequisites)
{
    uint32_t source_compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kTexSchema,
        .output_type = AssetType::Texture,
        .compile = [&source_compile_count](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            ++source_compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 1, 1, AssetType::Texture };
            return out;
        }
    });
    reg2.register_compiler(make_stub_compiler(kMeshSchema, AssetType::Mesh));
    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    CountingExternalCacheProvider provider;
    const AssetKey roots[]{ kKeyB };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys2.resolve_roots(
            roots,
            ResolvePolicy::CacheRequired,
            &provider,
            &errors),
        0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].first, kKeyB);
    EXPECT_EQ(errors[0].second, ResolveError::ExternalCacheMiss);
    EXPECT_EQ(source_compile_count, 0u);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_FALSE(sys2.cache().contains(kKeyB));

    const auto state = sys2.node_resolve_state(kKeyB);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->status, NodeResolveStatus::Failed);
    ASSERT_TRUE(state->error.has_value());
    EXPECT_EQ(*state->error, ResolveError::ExternalCacheMiss);
}

TEST_F(AssetSystemTest, ResolveRoots_CachePreferredHitSkipsPrerequisites)
{
    CompilerRegistry reg2;
    reg2.register_compiler(make_failing_compiler(kTexSchema, AssetType::Texture));
    reg2.register_compiler(make_stub_compiler(kMeshSchema, AssetType::Mesh));
    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    SingleEntryExternalCacheProvider provider{
        kKeyB,
        ResourceHandle{ 77, 1, AssetType::Mesh },
    };
    const AssetKey roots[]{ kKeyB };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys2.resolve_roots(
            roots,
            ResolvePolicy::CachePreferred,
            &provider,
            &errors),
        1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(provider.can_load_calls, 1u);
    EXPECT_EQ(provider.load_calls, 1u);
    EXPECT_FALSE(sys2.cache().contains(kKeyA));
    EXPECT_TRUE(sys2.cache().contains(kKeyB));

    const auto* compiled = sys2.find_compiled(kKeyB);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->handle.id, 77u);
}

TEST_F(AssetSystemTest, ResolveRoots_CachePreferredMissFallsBackToSource)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kTexSchema,
        .output_type = AssetType::Texture,
        .compile = [&compile_count](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ compile_count, 1, AssetType::Texture };
            return out;
        }
    });
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&compile_count](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ compile_count, 1, AssetType::Mesh };
            return out;
        }
    });
    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Texture, kTexSchema)));
    ASSERT_TRUE(sys2.register_asset(
        make_node(kKeyB, AssetType::Mesh, kMeshSchema),
        { kKeyA }));
    ASSERT_TRUE(sys2.commit());

    CountingExternalCacheProvider provider;
    const AssetKey roots[]{ kKeyB };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys2.resolve_roots(
            roots,
            ResolvePolicy::CachePreferred,
            &provider,
            &errors),
        2u);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(compile_count, 2u);
    EXPECT_TRUE(sys2.cache().contains(kKeyA));
    EXPECT_TRUE(sys2.cache().contains(kKeyB));
}

TEST_F(AssetSystemTest, ResolveRoots_ForceRecompileIgnoresExternalProvider)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&compile_count](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload = ResourceHandle{ 5, 1, AssetType::Mesh };
            return out;
        }
    });
    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    SingleEntryExternalCacheProvider provider{
        kKeyA,
        ResourceHandle{ 99, 1, AssetType::Mesh },
    };
    const AssetKey roots[]{ kKeyA };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys2.resolve_roots(
            roots,
            ResolvePolicy::ForceRecompile,
            &provider,
            &errors),
        1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(provider.can_load_calls, 0u);
    EXPECT_EQ(provider.load_calls, 0u);
    EXPECT_EQ(compile_count, 1u);

    const auto* compiled = sys2.find_compiled(kKeyA);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->handle.id, 5u);
}

TEST_F(AssetSystemTest, ResolveRoots_ForceRecompileRefreshesCachedState)
{
    uint32_t compile_count = 0;

    CompilerRegistry reg2;
    reg2.register_compiler(AssetCompiler{
        .input_schema = kMeshSchema,
        .output_type = AssetType::Mesh,
        .compile = [&compile_count](
            const AssetNode& input,
            std::span<const AssetNode>,
            std::span<const ResourceHandle>) -> AssetNode {
            ++compile_count;
            AssetNode out = input;
            out.stage = AssetStage::Compiled;
            out.payload =
                ResourceHandle{ compile_count, 1, AssetType::Mesh };
            return out;
        }
    });

    AssetSystem sys2(std::move(reg2));
    ASSERT_TRUE(sys2.register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys2.commit());

    auto first = sys2.resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(first));
    EXPECT_EQ(std::get<ResourceHandle>(first).id, 1u);

    const AssetKey roots[]{ kKeyA };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys2.resolve_roots(
            roots,
            ResolvePolicy::ForceRecompile,
            nullptr,
            &errors),
        1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(compile_count, 2u);

    const auto* compiled = sys2.find_compiled(kKeyA);
    ASSERT_NE(compiled, nullptr);
    EXPECT_EQ(compiled->handle.id, 2u);

    const auto state = sys2.node_resolve_state(kKeyA);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->status, NodeResolveStatus::Done);
    EXPECT_FALSE(state->error.has_value());
}

TEST_F(AssetSystemTest, ResolveRoots_DemandRootCountsAssetsNotRoot)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->register_demand_root(DemandRoot::GPURuntime, { kKeyA }));
    ASSERT_TRUE(sys->commit());

    SingleEntryExternalCacheProvider provider{
        kKeyA,
        ResourceHandle{ 42, 1, AssetType::Mesh },
    };
    const AssetKey roots[]{ make_demand_root_key(DemandRoot::GPURuntime) };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys->resolve_roots(
            roots,
            ResolvePolicy::CachePreferred,
            &provider,
            &errors),
        1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_FALSE(sys->cache().contains(make_demand_root_key(DemandRoot::GPURuntime)));
}

TEST_F(AssetSystemTest, EvictEvictableNotDemanded_ReleasesIndirectPrerequisites)
{
    AssetNode source = make_node(kKeyA, AssetType::Mesh, kMeshSchema);
    source.residency = ResidencyIntent::CompileOnly;
    AssetNode runtime = make_node(kKeyB, AssetType::Texture, kTexSchema);
    runtime.residency = ResidencyIntent::RuntimeResident;

    ASSERT_TRUE(sys->register_asset(std::move(source)));
    ASSERT_TRUE(sys->register_asset(std::move(runtime), { kKeyA }));
    ASSERT_TRUE(sys->register_demand_root(DemandRoot::GPURuntime, { kKeyB }));
    ASSERT_TRUE(sys->commit());

    const AssetKey roots[]{ make_demand_root_key(DemandRoot::GPURuntime) };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys->resolve_roots(
            roots,
            ResolvePolicy::CachePreferred,
            nullptr,
            &errors),
        2u);
    ASSERT_TRUE(errors.empty());
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_TRUE(sys->cache().contains(kKeyB));

    EXPECT_EQ(sys->evict_evictable_not_demanded(roots), 1u);
    EXPECT_FALSE(sys->cache().contains(kKeyA));
    EXPECT_TRUE(sys->cache().contains(kKeyB));
    EXPECT_EQ(sys->find_compiled(kKeyA), nullptr);
    EXPECT_NE(sys->find_compiled(kKeyB), nullptr);
}

TEST_F(AssetSystemTest, EvictEvictableNotDemanded_KeepsDirectDemand)
{
    AssetNode direct = make_node(kKeyA, AssetType::Mesh, kMeshSchema);
    direct.residency = ResidencyIntent::Transient;

    ASSERT_TRUE(sys->register_asset(std::move(direct)));
    ASSERT_TRUE(sys->register_demand_root(DemandRoot::CPURuntime, { kKeyA }));
    ASSERT_TRUE(sys->commit());

    const AssetKey roots[]{ make_demand_root_key(DemandRoot::CPURuntime) };
    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(
        sys->resolve_roots(
            roots,
            ResolvePolicy::CachePreferred,
            nullptr,
            &errors),
        1u);
    ASSERT_TRUE(errors.empty());

    EXPECT_EQ(sys->evict_evictable_not_demanded(roots), 0u);
    EXPECT_TRUE(sys->cache().contains(kKeyA));
    EXPECT_NE(sys->find_compiled(kKeyA), nullptr);
}

TEST_F(AssetSystemTest, ExternalCacheProvider_InterfaceIsGenericAssetCore)
{
    CountingExternalCacheProvider provider;

    EXPECT_FALSE(provider.can_load(kMeshSchema, AssetType::Mesh, kKeyA));
    EXPECT_EQ(provider.load(kMeshSchema, AssetType::Mesh, kKeyA), std::nullopt);
    EXPECT_EQ(provider.can_load_calls, 1u);
    EXPECT_EQ(provider.load_calls, 1u);

    constexpr ResolvePolicy cache_required = ResolvePolicy::CacheRequired;
    constexpr ResolvePolicy cache_preferred = ResolvePolicy::CachePreferred;
    constexpr ResolvePolicy force_recompile = ResolvePolicy::ForceRecompile;
    static_assert(cache_required == ResolvePolicy::CacheRequired);
    static_assert(cache_preferred == ResolvePolicy::CachePreferred);
    static_assert(force_recompile == ResolvePolicy::ForceRecompile);
}

TEST_F(AssetSystemTest, ResolveAll_DoesNotConsultExternalCacheProvider)
{
    CountingExternalCacheProvider provider;

    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    EXPECT_EQ(sys->resolve_all(&errors), 1u);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(provider.can_load_calls, 0u);
    EXPECT_EQ(provider.load_calls, 0u);
}

TEST_F(AssetSystemTest, Cache_StoredAfterResolve)
{
    ASSERT_TRUE(sys->register_asset(make_node(kKeyA, AssetType::Mesh, kMeshSchema)));
    ASSERT_TRUE(sys->commit());

    auto r = sys->resolve(kKeyA);
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(r));

    auto cached = sys->cache().lookup(kKeyA);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*cached, std::get<ResourceHandle>(r));
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
    ASSERT_TRUE(std::holds_alternative<ResourceHandle>(r));
    EXPECT_TRUE(sys->cache().contains(kKeyA));
}
