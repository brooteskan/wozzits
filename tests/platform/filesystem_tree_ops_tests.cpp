// tests/platform/filesystem_tree_ops_tests.cpp
//
// Whole-tree operations added to wz::fs in #141: walk, copy_file, copy_tree,
// rename. These are the APIs that let the last std::filesystem tree-walkers
// (chiefly the standalone exporter) migrate onto one filesystem layer.
//
// Two behaviours are OS-policy sensitive and cannot be forced on every machine,
// so they GTEST_SKIP rather than fail when the environment won't cooperate:
//   - "unreadable subdirectory is surfaced" needs a deny-read DACL to take
//     effect (a process with backup privilege bypasses it).
//   - "walk does not descend a reparse point" needs a directory symlink, which
//     needs Developer Mode or admin to create.
// The non-ASCII round-trip extends the explicit-UTF-8-byte style of
// filesystem_whole_file_io_tests.cpp to a whole tree.

#include <gtest/gtest.h>

#include <file/filesystem.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <aclapi.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

namespace fs = wz::fs;

namespace
{
    fs::Path make_temp_dir(const char *tag)
    {
        const fs::Path dir =
            fs::join(fs::temp_directory_path(), std::string("wz_fs_tree_") + tag);
        fs::remove_directory(dir, /*recursive=*/true);
        fs::create_directories(dir);
        return dir;
    }

    std::wstring to_wide(const std::string &s)
    {
        if (s.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                    nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    // "café_日本_ü" as explicit UTF-8 bytes -- identical style to the whole-file
    // test, so the bytes on disk are unambiguous regardless of this file's
    // encoding.
    const std::string kNonAscii = "caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC_\xC3\xBC";

    // Apply a deny-read DACL for the current user to `wpath`. Returns false if
    // the ACL machinery is unavailable so the caller can skip.
    bool try_deny_directory_read(const std::wstring &wpath)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        DWORD len = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        std::vector<BYTE> buf(len);
        const bool got =
            len != 0 && GetTokenInformation(token, TokenUser, buf.data(), len, &len);
        CloseHandle(token);
        if (!got)
            return false;

        PSID sid = reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid;

        EXPLICIT_ACCESSW ea = {};
        ea.grfAccessPermissions = GENERIC_READ | FILE_LIST_DIRECTORY | FILE_TRAVERSE;
        ea.grfAccessMode = DENY_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
        ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);

        PACL acl = nullptr;
        if (SetEntriesInAclW(1, &ea, nullptr, &acl) != ERROR_SUCCESS)
            return false;

        const DWORD rc = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(wpath.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, acl, nullptr);

        if (acl)
            LocalFree(acl);

        return rc == ERROR_SUCCESS;
    }

    // Restore access by setting a NULL DACL (everyone full control) so cleanup
    // can delete the directory again.
    void restore_directory_access(const std::wstring &wpath)
    {
        SetNamedSecurityInfoW(
            const_cast<LPWSTR>(wpath.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr);
    }
}

// --- walk -----------------------------------------------------------------

TEST(FilesystemTreeOps, WalkVisitsEveryRegularFileExactlyOnce)
{
    const fs::Path root = make_temp_dir("walk");

    ASSERT_EQ(fs::create_directories(fs::join(root, "sub/deep")),
              fs::FileError::None);
    ASSERT_EQ(fs::create_directories(fs::join(root, "empty")),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(root, "a.txt"), "A", true),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(root, "sub/b.txt"), "B", true),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(root, "sub/deep/c.txt"), "C", true),
              fs::FileError::None);

    std::map<std::string, int> file_hits;
    std::set<std::string> dirs;

    const fs::FileError err =
        fs::walk(root, [&](const fs::Path &full, const fs::DirEntry &e) {
            if (e.is_directory)
                dirs.insert(fs::filename(full));
            else
                file_hits[fs::filename(full)]++;
        });

    EXPECT_EQ(err, fs::FileError::None);

    // Every regular file under the tree, each exactly once.
    EXPECT_EQ(file_hits.size(), 3u);
    EXPECT_EQ(file_hits["a.txt"], 1);
    EXPECT_EQ(file_hits["b.txt"], 1);
    EXPECT_EQ(file_hits["c.txt"], 1);

    // Directories are visited too, including the empty one.
    EXPECT_EQ(dirs.count("sub"), 1u);
    EXPECT_EQ(dirs.count("deep"), 1u);
    EXPECT_EQ(dirs.count("empty"), 1u);

    fs::remove_directory(root, true);
}

TEST(FilesystemTreeOps, WalkOnMissingRootReturnsError)
{
    const fs::Path root = make_temp_dir("walk_missing");
    const fs::Path missing = fs::join(root, "not_here");

    int hits = 0;
    const fs::FileError err =
        fs::walk(missing, [&](const fs::Path &, const fs::DirEntry &) { hits++; });

    EXPECT_EQ(err, fs::FileError::NotFound);
    EXPECT_EQ(hits, 0);

    fs::remove_directory(root, true);
}

TEST(FilesystemTreeOps, WalkSurfacesUnreadableSubdirectory)
{
    const fs::Path root = make_temp_dir("walk_denied");
    const fs::Path locked = fs::join(root, "locked");
    ASSERT_EQ(fs::create_directories(locked), fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(locked, "secret.txt"), "x", true),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(root, "visible.txt"), "y", true),
              fs::FileError::None);

    const std::wstring wlocked = to_wide(locked);

    if (!try_deny_directory_read(wlocked))
    {
        fs::remove_directory(root, true);
        GTEST_SKIP() << "could not apply a deny-read DACL in this environment";
    }

    // If the deny didn't actually take (e.g. we hold backup privilege), skip
    // rather than assert a pass the environment can't back.
    if (static_cast<bool>(fs::list_directory(locked)))
    {
        restore_directory_access(wlocked);
        fs::remove_directory(root, true);
        GTEST_SKIP() << "deny-read DACL not enforced in this environment";
    }

    bool saw_visible = false;
    const fs::FileError err =
        fs::walk(root, [&](const fs::Path &full, const fs::DirEntry &e) {
            if (!e.is_directory && fs::filename(full) == "visible.txt")
                saw_visible = true;
        });

    // The unreadable subdirectory is surfaced as an error...
    EXPECT_NE(err, fs::FileError::None);
    // ...but the walk still visited the readable sibling.
    EXPECT_TRUE(saw_visible);

    restore_directory_access(wlocked);
    fs::remove_directory(root, true);
}

TEST(FilesystemTreeOps, WalkDoesNotDescendReparsePoint)
{
    const fs::Path base = make_temp_dir("walk_reparse");
    const fs::Path root = fs::join(base, "root");
    const fs::Path target = fs::join(base, "target");
    ASSERT_EQ(fs::create_directories(root), fs::FileError::None);
    ASSERT_EQ(fs::create_directories(target), fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(target, "inside.txt"), "z", true),
              fs::FileError::None);

    const fs::Path link = fs::join(root, "link");
    const std::wstring wlink = to_wide(link);
    const std::wstring wtarget = to_wide(target);

    const DWORD flags =
        SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (!CreateSymbolicLinkW(wlink.c_str(), wtarget.c_str(), flags))
    {
        fs::remove_directory(base, true);
        GTEST_SKIP() << "could not create a directory symlink "
                        "(needs Developer Mode or admin)";
    }

    bool saw_link_dir = false;
    bool saw_inside = false;
    const fs::FileError err =
        fs::walk(root, [&](const fs::Path &full, const fs::DirEntry &e) {
            const fs::Path name = fs::filename(full);
            if (e.is_directory && name == "link")
                saw_link_dir = true;
            if (name == "inside.txt")
                saw_inside = true;
        });

    EXPECT_EQ(err, fs::FileError::None);
    EXPECT_TRUE(saw_link_dir);   // the reparse point itself is visited
    EXPECT_FALSE(saw_inside);    // but the walk did not follow it

    // Remove the link itself first so cleanup never deletes through it.
    RemoveDirectoryW(wlink.c_str());
    fs::remove_directory(base, true);
}

// --- copy_file / copy_tree / rename --------------------------------------

TEST(FilesystemTreeOps, CopyFileHonoursExistencePolicy)
{
    const fs::Path dir = make_temp_dir("copyfile");
    const fs::Path src = fs::join(dir, "src.txt");
    const fs::Path dst = fs::join(dir, "dst.txt");
    ASSERT_EQ(fs::write_file_text(src, "ORIGINAL", true), fs::FileError::None);

    // Into an empty slot: succeeds.
    ASSERT_EQ(fs::copy_file(src, dst, fs::CopyOptions::FailIfExists),
              fs::FileError::None);
    EXPECT_EQ(fs::read_file_text(dst).value, "ORIGINAL");

    ASSERT_EQ(fs::write_file_text(src, "CHANGED", true), fs::FileError::None);

    // FailIfExists refuses and leaves the destination untouched.
    EXPECT_EQ(fs::copy_file(src, dst, fs::CopyOptions::FailIfExists),
              fs::FileError::AlreadyExists);
    EXPECT_EQ(fs::read_file_text(dst).value, "ORIGINAL");

    // SkipExisting also leaves it untouched, but reports success.
    EXPECT_EQ(fs::copy_file(src, dst, fs::CopyOptions::SkipExisting),
              fs::FileError::None);
    EXPECT_EQ(fs::read_file_text(dst).value, "ORIGINAL");

    // OverwriteExisting replaces the content.
    EXPECT_EQ(fs::copy_file(src, dst, fs::CopyOptions::OverwriteExisting),
              fs::FileError::None);
    EXPECT_EQ(fs::read_file_text(dst).value, "CHANGED");

    fs::remove_directory(dir, true);
}

TEST(FilesystemTreeOps, CopyTreeReproducesNestedTreeWithContent)
{
    const fs::Path dir = make_temp_dir("copytree");
    const fs::Path src = fs::join(dir, "src");
    const fs::Path dst = fs::join(dir, "dst");

    ASSERT_EQ(fs::create_directories(fs::join(src, "a/b")), fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(src, "root.txt"), "R", true),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(src, "a/one.txt"), "1", true),
              fs::FileError::None);
    ASSERT_EQ(fs::write_file_text(fs::join(src, "a/b/two.txt"), "2", true),
              fs::FileError::None);

    ASSERT_EQ(fs::copy_tree(src, dst, fs::CopyOptions::OverwriteExisting),
              fs::FileError::None);

    EXPECT_EQ(fs::read_file_text(fs::join(dst, "root.txt")).value, "R");
    EXPECT_EQ(fs::read_file_text(fs::join(dst, "a/one.txt")).value, "1");
    EXPECT_EQ(fs::read_file_text(fs::join(dst, "a/b/two.txt")).value, "2");
    EXPECT_TRUE(fs::is_directory(fs::join(dst, "a/b")));

    fs::remove_directory(dir, true);
}

TEST(FilesystemTreeOps, RenameMovesAndReplaces)
{
    const fs::Path dir = make_temp_dir("rename");
    const fs::Path a = fs::join(dir, "a.txt");
    const fs::Path b = fs::join(dir, "b.txt");
    ASSERT_EQ(fs::write_file_text(a, "AA", true), fs::FileError::None);

    ASSERT_EQ(fs::rename(a, b), fs::FileError::None);
    EXPECT_FALSE(fs::exists(a));
    EXPECT_TRUE(fs::exists(b));
    EXPECT_EQ(fs::read_file_text(b).value, "AA");

    // A rename over an existing destination replaces it.
    ASSERT_EQ(fs::write_file_text(a, "NEW", true), fs::FileError::None);
    ASSERT_EQ(fs::rename(a, b), fs::FileError::None);
    EXPECT_FALSE(fs::exists(a));
    EXPECT_EQ(fs::read_file_text(b).value, "NEW");

    fs::remove_directory(dir, true);
}

// --- non-ASCII whole-tree round trip -------------------------------------

TEST(FilesystemTreeOps, NonAsciiTreeCopiesAndWalks)
{
    const fs::Path dir = make_temp_dir("nonascii");
    const fs::Path src = fs::join(dir, "src");
    const fs::Path subdir = fs::join(src, kNonAscii + "_dir");
    ASSERT_EQ(fs::create_directories(subdir), fs::FileError::None);

    const fs::Path file = fs::join(subdir, kNonAscii + ".txt");
    const std::string payload = "payload \xE2\x9C\x93";   // ...✓
    ASSERT_EQ(fs::write_file_text(file, payload, true), fs::FileError::None);

    const fs::Path dst = fs::join(dir, "dst");
    ASSERT_EQ(fs::copy_tree(src, dst, fs::CopyOptions::OverwriteExisting),
              fs::FileError::None);

    // Content survived byte-exact under the non-ASCII path components.
    const fs::Path copied =
        fs::join(fs::join(dst, kNonAscii + "_dir"), kNonAscii + ".txt");
    const auto read = fs::read_file_text(copied);
    ASSERT_TRUE(static_cast<bool>(read));
    EXPECT_EQ(read.value, payload);

    // walk finds the non-ASCII file under the copy exactly once.
    int hits = 0;
    fs::walk(dst, [&](const fs::Path &full, const fs::DirEntry &e) {
        if (!e.is_directory && fs::filename(full) == (kNonAscii + ".txt"))
            hits++;
    });
    EXPECT_EQ(hits, 1);

    fs::remove_directory(dir, true);
}
