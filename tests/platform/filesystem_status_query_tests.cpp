// tests/platform/filesystem_status_query_tests.cpp
//
// Status queries, path resolution helpers, and error diagnostics added to
// wz::fs in #141. These touch the disk (via the system temp directory) because
// exists/is_directory/status/canonical query real filesystem state; the pure
// string helpers are covered in filesystem_path_semantics_tests.cpp.
//
// Each test works inside its own uniquely-named temp subdirectory and removes it
// at the end, so a crashed prior run cannot poison the next.

#include <gtest/gtest.h>

#include <file/filesystem.h>

#include <set>
#include <string>

namespace fs = wz::fs;

namespace
{
    // A fresh, empty temp subdirectory. Any leftover from a previous aborted run
    // is cleared first so the tests are deterministic.
    fs::Path make_temp_dir(const char *tag)
    {
        const fs::Path dir =
            fs::join(fs::temp_directory_path(), std::string("wz_fs_sq_") + tag);
        fs::remove_directory(dir, /*recursive=*/true);
        fs::create_directories(dir);
        return dir;
    }
}

// --- status / query -------------------------------------------------------

TEST(FilesystemStatusQuery, DistinguishesMissingFileDirectory)
{
    const fs::Path dir = make_temp_dir("kinds");
    const fs::Path file = fs::join(dir, "a.txt");
    const fs::Path subdir = fs::join(dir, "sub");
    const fs::Path missing = fs::join(dir, "nope.txt");

    ASSERT_EQ(fs::write_file_text(file, "hi", true), fs::FileError::None);
    ASSERT_EQ(fs::create_directories(subdir), fs::FileError::None);

    EXPECT_TRUE(fs::exists(file));
    EXPECT_TRUE(fs::exists(subdir));
    EXPECT_FALSE(fs::exists(missing));

    EXPECT_TRUE(fs::is_regular_file(file));
    EXPECT_FALSE(fs::is_regular_file(subdir));
    EXPECT_FALSE(fs::is_regular_file(missing));

    EXPECT_FALSE(fs::is_directory(file));
    EXPECT_TRUE(fs::is_directory(subdir));
    EXPECT_FALSE(fs::is_directory(missing));

    const auto sf = fs::status(file);
    ASSERT_TRUE(static_cast<bool>(sf));
    EXPECT_TRUE(sf.value.exists);
    EXPECT_TRUE(sf.value.is_regular_file);
    EXPECT_FALSE(sf.value.is_directory);
    EXPECT_EQ(sf.value.size, 2u);

    const auto sd = fs::status(subdir);
    ASSERT_TRUE(static_cast<bool>(sd));
    EXPECT_TRUE(sd.value.is_directory);
    EXPECT_FALSE(sd.value.is_regular_file);
    EXPECT_EQ(sd.value.size, 0u);

    const auto sm = fs::status(missing);
    EXPECT_FALSE(static_cast<bool>(sm));
    EXPECT_EQ(sm.error, fs::FileError::NotFound);

    fs::remove_directory(dir, true);
}

// --- create_parent_directories -------------------------------------------

TEST(FilesystemStatusQuery, CreateParentDirectoriesMakesAncestors)
{
    const fs::Path dir = make_temp_dir("cpd");
    const fs::Path nested = fs::join(dir, "x/y/z");
    const fs::Path deep = fs::join(nested, "file.txt");

    ASSERT_EQ(fs::create_parent_directories(deep), fs::FileError::None);
    EXPECT_TRUE(fs::is_directory(nested));

    // With the parent now present, the write itself succeeds.
    EXPECT_EQ(fs::write_file_text(deep, "ok", true), fs::FileError::None);

    // A bare filename has no parent component -> no-op success, not an error.
    EXPECT_EQ(fs::create_parent_directories("just_a_name.txt"),
              fs::FileError::None);

    fs::remove_directory(dir, true);
}

// --- absolute / weakly_canonical / canonical -----------------------------

TEST(FilesystemStatusQuery, AbsoluteResolvesRelativeAndPreservesAbsolute)
{
    const auto resolved = fs::absolute("some\\relative\\path");
    ASSERT_TRUE(static_cast<bool>(resolved));
    EXPECT_TRUE(fs::is_absolute(resolved.value));

    const auto kept = fs::absolute("C:\\already\\absolute");
    ASSERT_TRUE(static_cast<bool>(kept));
    EXPECT_EQ(kept.value, "C:\\already\\absolute");
}

TEST(FilesystemStatusQuery, WeaklyCanonicalCollapsesDotDotWithoutExisting)
{
    // Purely lexical + CWD: the path need not exist, and ".." is folded.
    const auto wc = fs::weakly_canonical("C:\\a\\b\\..\\c");
    ASSERT_TRUE(static_cast<bool>(wc));
    EXPECT_EQ(wc.value, "C:\\a\\c");
}

TEST(FilesystemStatusQuery, CanonicalRequiresExistenceAndReturnsAbsolute)
{
    const fs::Path dir = make_temp_dir("canon");
    const fs::Path file = fs::join(dir, "real.txt");
    ASSERT_EQ(fs::write_file_text(file, "x", true), fs::FileError::None);

    const auto c = fs::canonical(file);
    ASSERT_TRUE(static_cast<bool>(c));
    EXPECT_TRUE(fs::is_absolute(c.value));
    EXPECT_TRUE(fs::exists(c.value));   // resolves to the same real file

    const auto miss = fs::canonical(fs::join(dir, "missing.txt"));
    EXPECT_FALSE(static_cast<bool>(miss));
    EXPECT_EQ(miss.error, fs::FileError::NotFound);

    fs::remove_directory(dir, true);
}

// --- relative -------------------------------------------------------------

TEST(FilesystemStatusQuery, RelativeComputesTailAscentAndDot)
{
    // base is a prefix -> just the tail.
    const auto tail = fs::relative("C:\\proj\\assets\\tex.png", "C:\\proj");
    ASSERT_TRUE(static_cast<bool>(tail));
    EXPECT_EQ(tail.value, "assets\\tex.png");

    // needs to ascend out of base before descending.
    const auto ascend = fs::relative("C:\\proj\\a\\file", "C:\\proj\\b\\c");
    ASSERT_TRUE(static_cast<bool>(ascend));
    EXPECT_EQ(ascend.value, "..\\..\\a\\file");

    // identical -> "."
    const auto same = fs::relative("C:\\proj\\x", "C:\\proj\\x");
    ASSERT_TRUE(static_cast<bool>(same));
    EXPECT_EQ(same.value, ".");

    // different roots -> empty path, but not an error (matches std::filesystem).
    const auto cross = fs::relative("D:\\x", "C:\\y");
    EXPECT_TRUE(static_cast<bool>(cross));
    EXPECT_TRUE(cross.value.empty());
}

// --- error diagnostics ----------------------------------------------------

TEST(FilesystemStatusQuery, FailingCallPopulatesNativeErrorAndToString)
{
    const fs::Path dir = make_temp_dir("err");
    const fs::Path missing = fs::join(dir, "does_not_exist.bin");

    const auto r = fs::read_file(missing);
    ASSERT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error, fs::FileError::NotFound);

    // The native Win32 detail for that failure is captured on this thread. Read
    // it immediately, before any other wz::fs call can overwrite it.
    EXPECT_NE(fs::last_os_error_code(), 0u);
    EXPECT_FALSE(fs::last_os_error_message().empty());

    // to_string is stateless and distinguishes enumerators.
    EXPECT_STRNE(fs::to_string(fs::FileError::NotFound), "");
    EXPECT_NE(std::string(fs::to_string(fs::FileError::NotFound)),
              std::string(fs::to_string(fs::FileError::PermissionDenied)));

    fs::remove_directory(dir, true);
}

// Completeness pin for to_string: every enumerator must map to its own non-empty
// description. A newly-added FileError with no switch case would fall through to
// the default ("unknown error") and collide with FileError::Unknown -- which the
// distinctness check below turns into a failure rather than a silent gap.
TEST(FilesystemStatusQuery, ToStringCoversEveryEnumeratorDistinctly)
{
    const fs::FileError all[] = {
        fs::FileError::None,
        fs::FileError::InvalidArgument,
        fs::FileError::NotFound,
        fs::FileError::PermissionDenied,
        fs::FileError::IOError,
        fs::FileError::InvalidPath,
        fs::FileError::AlreadyExists,
        fs::FileError::Unknown,
    };

    std::set<std::string> descriptions;
    for (const fs::FileError e : all)
    {
        const std::string s = fs::to_string(e);
        EXPECT_FALSE(s.empty());
        EXPECT_TRUE(descriptions.insert(s).second)
            << "to_string is not distinct at enumerator value "
            << static_cast<int>(e) << " (\"" << s << "\")";
    }
    EXPECT_EQ(descriptions.size(), sizeof(all) / sizeof(all[0]));
}
