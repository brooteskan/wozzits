#include "behavior_test_support.h"

#include <fstream>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
    std::string loaded_copy_from_detail(const std::string& detail)
    {
        const std::string separator = " -> ";
        const size_t separator_pos = detail.find(separator);
        return separator_pos == std::string::npos
            ? std::string{}
            : detail.substr(separator_pos + separator.size());
    }
}

TEST(BehaviorPluginAbi, RejectsVersionMismatch)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    EXPECT_FALSE(plugins.register_static_pack(
        registry,
        register_empty_pack,
        nullptr,
        WZ_BEHAVIOR_ABI_VERSION + 1u));
    EXPECT_FALSE(plugins.register_static_pack(
        registry,
        register_empty_pack,
        nullptr,
        6u));
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, RejectsMissingPath)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{
            "definitely_missing_behavior_plugin_wozzits_test.dll" });

    EXPECT_EQ(
        result.status,
        BehaviorPluginHost::DynamicLoadStatus::InvalidPath);
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, RejectsMissingRegisterSymbol)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_MISSING_SYMBOL_DLL });

    EXPECT_EQ(
        result.status,
        BehaviorPluginHost::DynamicLoadStatus::MissingRegisterSymbol);
    EXPECT_TRUE(registry.registrations().empty());
}

TEST(BehaviorPluginDynamicModule, LoadsRegistersAndDispatchesBehavior)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(result.ok()) << result.detail;
    ASSERT_TRUE(registry.find(
        "dynamic_test",
        "always_add_local_y").has_value());

    SceneInstance scene = scene_with_behavior(
        12u,
        "dynamic_test",
        "always_add_local_y");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.entity, 12u);
    EXPECT_EQ(command.kind, BehaviorCommandKind::AddLocalTranslation);
    EXPECT_FLOAT_EQ(command.values[0], 0.0f);
    EXPECT_FLOAT_EQ(command.values[1], 2.0f);
    EXPECT_FLOAT_EQ(command.values[2], 0.0f);
}

TEST(BehaviorPluginDynamicModule, LoadsFromCopyAndCleansUpCopy)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto result = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(result.ok()) << result.detail;

#if defined(_WIN32)
    const std::string loaded_path =
        loaded_copy_from_detail(result.detail);
    ASSERT_FALSE(loaded_path.empty()) << result.detail;
    EXPECT_NE(loaded_path.find(".wzload."), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(loaded_path));

    plugins.clear();

    EXPECT_FALSE(std::filesystem::exists(loaded_path));
#else
    plugins.clear();
#endif
}

TEST(BehaviorPluginDynamicModule, ReloadReplacesLoadedCopy)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto first = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(first.ok()) << first.detail;

#if defined(_WIN32)
    const std::string first_loaded_path =
        loaded_copy_from_detail(first.detail);
    ASSERT_FALSE(first_loaded_path.empty()) << first.detail;
    ASSERT_TRUE(std::filesystem::exists(first_loaded_path));

    const auto reloaded = plugins.reload_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(reloaded.ok()) << reloaded.detail;

    const std::string second_loaded_path =
        loaded_copy_from_detail(reloaded.detail);
    ASSERT_FALSE(second_loaded_path.empty()) << reloaded.detail;
    EXPECT_NE(second_loaded_path, first_loaded_path);
    EXPECT_FALSE(std::filesystem::exists(first_loaded_path));
    EXPECT_TRUE(std::filesystem::exists(second_loaded_path));

    ASSERT_TRUE(registry.find(
        "dynamic_test",
        "always_add_local_y").has_value());
    EXPECT_EQ(registry.registrations().size(), 1u);

    plugins.clear();

    EXPECT_FALSE(std::filesystem::exists(second_loaded_path));
#else
    const auto reloaded = plugins.reload_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });
    EXPECT_EQ(
        reloaded.status,
        BehaviorPluginHost::DynamicLoadStatus::UnsupportedPlatform);
    plugins.clear();
#endif
}

TEST(BehaviorPluginDynamicModule, FailedReloadKeepsExistingModuleUsable)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;

    const auto first = plugins.load_dynamic_module(
        registry,
        std::filesystem::path{ WZ_TEST_BEHAVIOR_PLUGIN_DLL });

    ASSERT_TRUE(first.ok()) << first.detail;

#if defined(_WIN32)
    const std::string first_loaded_path =
        loaded_copy_from_detail(first.detail);
    ASSERT_FALSE(first_loaded_path.empty()) << first.detail;
    ASSERT_TRUE(std::filesystem::exists(first_loaded_path));

    const auto failed_reload = plugins.reload_dynamic_module(
        registry,
        std::filesystem::path{
            "definitely_missing_behavior_plugin_reload_test.dll" });

    EXPECT_EQ(
        failed_reload.status,
        BehaviorPluginHost::DynamicLoadStatus::InvalidPath);
    EXPECT_TRUE(std::filesystem::exists(first_loaded_path));
    EXPECT_EQ(registry.registrations().size(), 1u);

    SceneInstance scene = scene_with_behavior(
        12u,
        "dynamic_test",
        "always_add_local_y");
    wz::engine::FrameContext frame_context{};
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_context = &frame_context,
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_FLOAT_EQ(
        frame_storage.behavior_commands.commands[0].values[1],
        2.0f);

    plugins.clear();

    EXPECT_FALSE(std::filesystem::exists(first_loaded_path));
#else
    const auto failed_reload = plugins.reload_dynamic_module(
        registry,
        std::filesystem::path{
            "definitely_missing_behavior_plugin_reload_test.dll" });
    EXPECT_EQ(
        failed_reload.status,
        BehaviorPluginHost::DynamicLoadStatus::InvalidPath);
    plugins.clear();
#endif
}

TEST(BehaviorPluginAbi, RejectsInvalidRegistrations)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    InvalidRegistrationProbe probe{};
    g_invalid_registration_probe = &probe;

    EXPECT_TRUE(plugins.register_static_pack(
        registry,
        register_invalid_registration_pack));
    g_invalid_registration_probe = nullptr;

    EXPECT_EQ(probe.null_name_result, 0u);
    EXPECT_EQ(probe.null_function_result, 0u);
    EXPECT_TRUE(registry.registrations().empty());
}


// ─── Orphaned shadow copies (issue #313, B4-C14) ───────────────────────────
// A behavior DLL is loaded from a ".wzload.<pid>.<n>.dll" shadow copy, and the
// ONLY code that deletes one is unload_dynamic_module_copy, reached through
// ordinary C++ teardown. The editor's play host force-kills the runtime with
// TerminateProcess once its shutdown grace expires, and TerminateProcess runs
// no destructors -- so every force-killed play left its copies in the project's
// behavior folder, permanently. Nothing else deleted them: the module scan only
// SKIPPED them. A killed process cannot clean up after itself, so the sweep
// happens on the next scan instead.

#if defined(_WIN32)

TEST(BehaviorPluginDynamicModule, ScanReapsCopiesLeftByADeadProcess)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path()
        / "wz_behavior_reap_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec);

    const auto touch = [](const fs::path& p) {
        std::ofstream f(p, std::ios::binary);
        f << "not a real dll";
    };

    // A copy owned by a process that cannot exist: pid 0 is never a real
    // process, so use a plainly dead one instead -- spawn nothing and pick an
    // id far outside the live range by reusing our own with an offset, then
    // confirm it is not alive before relying on it.
    const uint32_t self = static_cast<uint32_t>(GetCurrentProcessId());
    uint32_t dead = self + 4;
    for (int i = 0; i < 4096 && dead != 0; ++i, dead += 4) {
        const HANDLE h =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dead);
        if (h == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
            break;  // provably no such process
        }
        if (h != nullptr) {
            CloseHandle(h);
        }
    }

    const fs::path orphan =
        dir / ("mod.wzload." + std::to_string(dead) + ".0.dll");
    const fs::path mine =
        dir / ("mod.wzload." + std::to_string(self) + ".0.dll");
    const fs::path real = dir / "unrelated.dll";
    touch(orphan);
    touch(mine);
    touch(real);

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    plugins.load_dynamic_modules_from_directory(registry, dir, nullptr);

    EXPECT_FALSE(fs::exists(orphan))
        << "a copy whose owning process is gone must be reaped -- nothing else "
           "ever deletes it, so it accumulates for the life of the project";
    EXPECT_TRUE(fs::exists(mine))
        << "a copy owned by a LIVE process (here, us) must be left alone: it "
           "may still be loaded";
    EXPECT_TRUE(fs::exists(real))
        << "the sweep must only touch .wzload copies";

    fs::remove_all(dir, ec);
}

TEST(BehaviorPluginDynamicModule, ScanLeavesCopiesWithNoParsablePidAlone)
{
    // Fail in the safe direction: an unrecognised name is left on disk rather
    // than deleted on a guess.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path()
        / "wz_behavior_reap_unparsable_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec);

    const fs::path odd = dir / "mod.wzload.notapid.0.dll";
    {
        std::ofstream f(odd, std::ios::binary);
        f << "x";
    }

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    plugins.load_dynamic_modules_from_directory(registry, dir, nullptr);

    EXPECT_TRUE(fs::exists(odd));

    fs::remove_all(dir, ec);
}

#endif  // _WIN32
