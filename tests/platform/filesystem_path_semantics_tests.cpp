// tests/platform/filesystem_path_semantics_tests.cpp
//
// Pure lexical path logic for wz::fs (#141): normalize, is_absolute, and the
// split helpers parent_path / filename / stem / extension. No disk is touched --
// these pin the STRING semantics, which is where the Windows-specific footguns
// live (UNC roots, drive-relative "C:foo", trailing separators).
//
// Two behaviours here are load-bearing for existing callers and are pinned so a
// future "cleanup" cannot quietly flip them:
//   - extension() returns the suffix WITHOUT a leading dot ("json", not
//     ".json"). wozzits_app_v1_spawn.cpp branches on `extension(x) != "json"`.
//   - is_absolute() drives the `is_absolute(p) ? p : join(base, p)` idiom across
//     the asset/project code; "C:foo" reporting absolute would skip the join and
//     resolve against the wrong (per-drive) current directory.

#include <gtest/gtest.h>

#include <file/filesystem.h>

namespace fs = wz::fs;

// --- normalize ------------------------------------------------------------

TEST(FilesystemPathSemantics, NormalizeCollapsesAndUnifiesSeparators)
{
    EXPECT_EQ(fs::normalize("a/b/c"), "a\\b\\c");
    EXPECT_EQ(fs::normalize("a\\\\b//c"), "a\\b\\c");
    EXPECT_EQ(fs::normalize("a/b/"), "a\\b");   // trailing separator dropped
    EXPECT_EQ(fs::normalize("a\\b\\"), "a\\b");
}

TEST(FilesystemPathSemantics, NormalizePreservesDriveRoot)
{
    EXPECT_EQ(fs::normalize("C:\\"), "C:\\");
    EXPECT_EQ(fs::normalize("C:/"), "C:\\");
    EXPECT_EQ(fs::normalize("C:/foo/"), "C:\\foo");
}

// The regression this file most exists for: the old normalize folded EVERY run
// of separators to one, so a UNC share "\\server\share" came out "\server\share"
// -- a different, non-UNC path. The leading double separator must survive.
TEST(FilesystemPathSemantics, NormalizePreservesUncRoot)
{
    EXPECT_EQ(fs::normalize("\\\\server\\share"), "\\\\server\\share");
    EXPECT_EQ(fs::normalize("//server/share"), "\\\\server\\share");
    EXPECT_EQ(fs::normalize("\\\\server\\share\\"), "\\\\server\\share");
    EXPECT_EQ(fs::normalize("\\\\server\\share\\dir\\file.txt"),
              "\\\\server\\share\\dir\\file.txt");
}

TEST(FilesystemPathSemantics, NormalizeEmptyIsEmpty)
{
    EXPECT_EQ(fs::normalize(""), "");
}

// --- is_absolute ----------------------------------------------------------

TEST(FilesystemPathSemantics, IsAbsoluteDriveNeedsSeparatorAfterColon)
{
    EXPECT_TRUE(fs::is_absolute("C:\\foo"));
    EXPECT_TRUE(fs::is_absolute("C:/foo"));
    EXPECT_TRUE(fs::is_absolute("C:\\"));

    // "C:foo" is drive-RELATIVE (resolves against C:'s current directory), so it
    // is not a fully-qualified path.
    EXPECT_FALSE(fs::is_absolute("C:foo"));
    EXPECT_FALSE(fs::is_absolute("C:"));
}

TEST(FilesystemPathSemantics, IsAbsoluteUncIsAbsoluteButSingleRootIsNot)
{
    EXPECT_TRUE(fs::is_absolute("\\\\server\\share"));
    EXPECT_TRUE(fs::is_absolute("//server/share"));

    // A single leading separator is root-relative to the current drive.
    EXPECT_FALSE(fs::is_absolute("\\foo"));
    EXPECT_FALSE(fs::is_absolute("/foo"));

    EXPECT_FALSE(fs::is_absolute("foo\\bar"));
    EXPECT_FALSE(fs::is_absolute(""));
}

// --- parent_path / filename ----------------------------------------------

TEST(FilesystemPathSemantics, FilenameHandlesTrailingSeparators)
{
    EXPECT_EQ(fs::filename("a/b/c"), "c");
    EXPECT_EQ(fs::filename("a/b/"), "b");      // trailing sep ignored
    EXPECT_EQ(fs::filename("a\\b\\"), "b");
    EXPECT_EQ(fs::filename("foo"), "foo");     // no separator at all
}

TEST(FilesystemPathSemantics, ParentPathHandlesTrailingSeparatorsAndRoots)
{
    // parent_path is a substring split -- it preserves the input's separator
    // style rather than normalizing, so the '/' here stays a '/'.
    EXPECT_EQ(fs::parent_path("a/b/c"), "a/b");
    EXPECT_EQ(fs::parent_path("a/b/"), "a");   // matches filename("a/b/")=="b"
    EXPECT_EQ(fs::parent_path("C:\\foo"), "C:\\");
    EXPECT_EQ(fs::parent_path("C:\\foo\\"), "C:\\");
    EXPECT_EQ(fs::parent_path("\\foo"), "\\");
    EXPECT_EQ(fs::parent_path("foo"), "");     // no parent
}

// --- stem / extension -----------------------------------------------------

TEST(FilesystemPathSemantics, StemAndExtensionSplitAtLastDot)
{
    EXPECT_EQ(fs::stem("archive.tar.gz"), "archive.tar");
    EXPECT_EQ(fs::extension("archive.tar.gz"), "gz");   // no leading dot

    EXPECT_EQ(fs::stem("plain"), "plain");
    EXPECT_EQ(fs::extension("plain"), "");
}

// Dotfiles: a leading dot names the file, it is not an extension boundary.
TEST(FilesystemPathSemantics, DotfilesAreNotExtensions)
{
    EXPECT_EQ(fs::stem(".gitignore"), ".gitignore");
    EXPECT_EQ(fs::extension(".gitignore"), "");
}

TEST(FilesystemPathSemantics, StemAndExtensionSeeThroughTrailingAndDirs)
{
    EXPECT_EQ(fs::stem("dir/tank.scene.json"), "tank.scene");
    EXPECT_EQ(fs::extension("dir/tank.scene.json"), "json");
    EXPECT_EQ(fs::extension("dir/name.txt/"), "txt");   // trailing sep ignored
}
