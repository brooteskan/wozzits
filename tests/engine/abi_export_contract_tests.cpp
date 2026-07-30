// tests/engine/abi_export_contract_tests.cpp
//
// The first tests that CALL the C ABI's exports rather than only reading its
// structs. wozzits_abi.cpp carried 78 exports and no direct test for months --
// the standing TEST-GAP on issue #308 (part A1) -- because every test target
// links only window_engine, and the exports live in the wozzits_abi shared
// library. This file links that library (see rs_add_abi_test_group).
//
// Scope is the HOSTILE-CALL surface: what an export does with a null handle, a
// null out-parameter, and an out-of-range value. That is deliberately the half
// no other test can reach, and it is where A1's confirmed findings lived. The
// exports that need a live GPU runtime stay out of scope here.

#include <gtest/gtest.h>

#include <engine/abi/wozzits_abi.h>

#include <filesystem>
#include <string>

namespace
{
    // A buffer the caller "already used", so a failed call that forgets to reset
    // it is detectable. Nothing dereferences these -- the contract is that an
    // export zeroes them before it can fail, so the value only has to be
    // recognisable.
    constexpr uint64_t kDirtySize = 4242u;

    WzBuffer dirty_buffer()
    {
        WzBuffer buffer{};
        buffer.data = reinterpret_cast<uint8_t*>(0xDEADBEEFull);
        buffer.size = kDirtySize;
        return buffer;
    }

    [[nodiscard]] bool is_zeroed(const WzBuffer& buffer)
    {
        return buffer.data == nullptr && buffer.size == 0u;
    }

    std::string message_of(const WzResult& result)
    {
        return result.message ? result.message : "";
    }

    std::filesystem::path fresh_dir(const char* stem)
    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / (std::string("wz_abi_contract_") + stem);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        return root;
    }
}

// The built DLL and the header a consumer compiled against must agree. This is
// the cheapest possible guard against the failure mode A1-C4 described from the
// other side: a mirror decoding at a version the engine no longer speaks. If
// this fires, someone bumped WZ_ABI_VERSION without rebuilding, or vice versa.
TEST(AbiExportContract, ReportedVersionMatchesTheHeader)
{
    EXPECT_EQ(wz_abi_version(), WZ_ABI_VERSION);
}

// A1-C7. The caller owns the WzBuffer and is entitled to assume a FAILED call
// leaves it empty: a host reusing one buffer across calls would otherwise read
// the previous response's bytes as fresh, and a host passing an uninitialized
// stack buffer would free a garbage pointer. Seven exports validated their
// handle first and so returned with the buffer untouched on every early-out.
//
// Each case below fails for a reason that is NOT about the buffer (a null
// session or a null runtime), which is exactly the path that used to skip the
// reset.
TEST(AbiExportContract, BufferFillingExportsZeroTheOutBufferBeforeFailing)
{
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_session_asset_graph_snapshot(nullptr, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "asset_graph_snapshot";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_asset_graph_can_connect(nullptr, 1u, 2u, 0u, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "can_connect";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_asset_graph_connect(nullptr, 1u, 2u, 0u, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "connect";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_runtime_behavior_module_catalog(nullptr, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "behavior_module_catalog";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_runtime_scenelet_catalog(nullptr, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "scenelet_catalog";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_runtime_add_child_node(nullptr, "parent", &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "add_child_node";
    }
    {
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_runtime_add_node_behavior(nullptr, "node", "mod", &buffer);
        EXPECT_NE(result.code, WZ_RESULT_OK);
        EXPECT_TRUE(is_zeroed(buffer)) << "add_node_behavior";
    }
    {
        // The export that always did it right, kept as the control: if this one
        // ever regresses the whole convention has moved.
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_load_project_snapshot(nullptr, nullptr, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "load_project_snapshot (control)";
    }
    {
        // A1-C6's report channel is newer than C7 but shares the rule.
        WzBuffer buffer = dirty_buffer();
        const WzResult result =
            wz_host_runtime_take_dropped_edits(nullptr, &buffer);
        EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
        EXPECT_TRUE(is_zeroed(buffer)) << "take_dropped_edits";
    }
}

// A null out-parameter must be reported, never dereferenced. Separate from the
// case above because the failure mode is a crash rather than stale bytes, so a
// regression here takes the whole editor down instead of returning a wrong
// answer.
TEST(AbiExportContract, BufferFillingExportsRejectANullOutParameter)
{
    EXPECT_EQ(
        wz_host_session_asset_graph_snapshot(nullptr, nullptr).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_behavior_module_catalog(nullptr, nullptr).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_scenelet_catalog(nullptr, nullptr).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_take_dropped_edits(nullptr, nullptr).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_load_project_snapshot(nullptr, nullptr, nullptr).code,
        WZ_RESULT_INVALID_ARGUMENT);
}

// A1-C8, the finding this harness existed to test. add_node took the type as
// uint32_t and cast it to AssetType's uint16_t, so the truncation was silent AND
// aliasing: 0x10001 arrived as Mesh(1) and minted a Mesh node nobody asked for,
// while 0x10000 became Unknown and was refused with a message about the wrong
// problem. Reaching the guard needs a real session, which is why the fix shipped
// code-reviewed only until now.
class AbiSessionFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // COPY the fixture rather than opening it in place: a session can be
        // saved, and a test that mutates a checked-in fixture poisons every other
        // test that reads it. wz_host_create_project is not usable here -- it
        // writes a manifest with no asset graph, so the session refuses to open.
        root_ = fresh_dir("session");
        std::error_code ec;
        std::filesystem::copy(
            std::filesystem::path{ WZ_ABI_TEST_PROJECT_DIR },
            root_,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing,
            ec);
        ASSERT_FALSE(ec) << "could not stage the fixture project: "
                         << ec.message();

        const WzResult open = wz_host_open_project_session(
            root_.string().c_str(),
            root_.string().c_str(),
            &session_);
        // Deliberately an ASSERT, not a SKIP. A skip here is how this whole area
        // stayed untested: the session is available on any machine (no GPU
        // involved), so a failure to open is a real defect, not an environment.
        ASSERT_EQ(open.code, WZ_RESULT_OK) << message_of(open);
        ASSERT_NE(session_, nullptr);
    }

    void TearDown() override
    {
        if (session_ != nullptr) {
            wz_host_close_session(session_);
            session_ = nullptr;
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    WzHostSession* session_ = nullptr;
    std::filesystem::path root_;
};

TEST_F(AbiSessionFixture, AddNodeRefusesATypeThatDoesNotFitAssetType)
{
    // 0x10001 truncated to Mesh(1) before the fix: OK plus a real node id, for a
    // type the caller never named.
    uint64_t node_id = 12345u;
    const WzResult aliased =
        wz_host_asset_graph_add_node(session_, 1u, 0x10001u, &node_id);
    EXPECT_EQ(aliased.code, WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_NE(message_of(aliased).find("out of range"), std::string::npos)
        << message_of(aliased);
    EXPECT_EQ(node_id, 0u) << "a refused add must not hand back a node id";

    // 0x10000 truncated to Unknown(0) and was refused with "could not add asset
    // graph node" -- true but useless. The reason now names the real problem.
    const WzResult truncating =
        wz_host_asset_graph_add_node(session_, 1u, 0x10000u, &node_id);
    EXPECT_EQ(truncating.code, WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_NE(message_of(truncating).find("out of range"), std::string::npos)
        << message_of(truncating);
}

TEST_F(AbiSessionFixture, AddNodeRefusesTheAnyPortWildcardAsANodeType)
{
    // AssetType::Any (0xFFFF) is an INPUT-PORT wildcard -- asset/types.h says it
    // is never a node's own type. Minting a node whose type matches every edge
    // check would make the connection rules meaningless.
    uint64_t node_id = 999u;
    const WzResult result =
        wz_host_asset_graph_add_node(session_, 1u, 0xFFFFu, &node_id);
    EXPECT_EQ(result.code, WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_NE(message_of(result).find("wildcard"), std::string::npos)
        << message_of(result);
    EXPECT_EQ(node_id, 0u);
}

// The guard must not reject everything -- a range check that refuses valid input
// would be a worse bug than the truncation. An in-range type may still fail for
// its own reasons (no compiler registered for that schema/type pair, which is the
// key-factory allowlist's business, not this export's), so assert only that the
// failure is NOT one of the two the guard produces.
TEST_F(AbiSessionFixture, AddNodeDoesNotRefuseAnInRangeType)
{
    uint64_t node_id = 0u;
    const WzResult result =
        wz_host_asset_graph_add_node(session_, 1u, 1u, &node_id);

    const std::string message = message_of(result);
    EXPECT_EQ(message.find("out of range"), std::string::npos) << message;
    EXPECT_EQ(message.find("wildcard"), std::string::npos) << message;
    if (result.code == WZ_RESULT_OK) {
        EXPECT_NE(node_id, 0u) << "draft node ids are allocated from 1";
    }
}

// A session-taking export must reject a null session rather than dereferencing
// it. Cheap, and it is the other half of the hostile-call surface: the buffer
// tests above prove WHAT the failure leaves behind, this proves it fails at all.
TEST(AbiExportContract, SessionExportsRejectANullSession)
{
    uint64_t node_id = 7u;
    EXPECT_EQ(
        wz_host_asset_graph_add_node(nullptr, 1u, 1u, &node_id).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(node_id, 0u) << "the out id is reset before the session check";

    EXPECT_EQ(
        wz_host_asset_graph_set_node_param_string(
            nullptr, 1u, "key", "value").code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wz_host_session_save(nullptr).code, WZ_RESULT_INVALID_ARGUMENT);
}

// Runtime-taking mutators must reject a null runtime and an empty node id. These
// are the verbs A1-C6 is about: they cannot validate the node's EXISTENCE
// synchronously (that is what the dropped-edit report is for), but the arguments
// they can check, they must.
TEST(AbiExportContract, RuntimeMutatorsRejectANullRuntimeAndEmptyIds)
{
    EXPECT_EQ(
        wz_host_runtime_set_node_render_order(nullptr, "node", 0).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_add_node_component(nullptr, "node", "collision").code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_set_node_collision(nullptr, "node", 1ull, 0).code,
        WZ_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(
        wz_host_runtime_remove_node(nullptr, "").code,
        WZ_RESULT_INVALID_ARGUMENT);
}

// wz_free_buffer is the host's only way to release what an export handed back,
// so it has to tolerate the shapes a host can produce: a null pointer, and an
// already-freed buffer (double free). Both are reachable from ordinary C#
// finally-blocks.
TEST(AbiExportContract, FreeBufferToleratesNullAndDoubleFree)
{
    wz_free_buffer(nullptr);

    WzBuffer buffer{};
    wz_free_buffer(&buffer);
    EXPECT_TRUE(is_zeroed(buffer));

    WzBuffer real{};
    const WzResult catalog = wz_host_asset_catalog(&real);
    ASSERT_EQ(catalog.code, WZ_RESULT_OK) << message_of(catalog);
    ASSERT_NE(real.data, nullptr);
    wz_free_buffer(&real);
    EXPECT_TRUE(is_zeroed(real)) << "free must reset the buffer it released";
    wz_free_buffer(&real);   // second free: a no-op, not a crash
}
