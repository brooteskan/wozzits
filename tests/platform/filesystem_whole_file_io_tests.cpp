// tests/platform/filesystem_whole_file_io_tests.cpp
//
// The FIRST test of src/file (issue #310, A4-C1/A4-C5: that layer had zero
// direct tests, which is why a silent truncation bug lived in the one function
// every asset in the engine is loaded through).
//
// The defect: read_file and write_file each moved a whole file in a SINGLE
// ReadFile/WriteFile call whose count argument is a 32-bit DWORD, and neither
// compared the transferred total against the size it set out to move.
// Consequences, both measured on issue #310 before the fix:
//   - read_file on a 4 GiB file returned 0 bytes with FileError::None;
//     on a 4 GiB + 1 KiB file it returned the low 1024 bytes, also as success.
//   - write_file with a >= 4 GiB payload wrote (size mod 2^32) bytes and
//     reported success -- and it is the disk-cache write path and, via
//     write_file_text, the scene/scenelet JSON writer.
// A caller could not distinguish a whole file from a prefix, so a large but
// perfectly valid asset silently decoded as a small one.
//
// WHY THE PAYLOAD HERE IS DELIBERATELY LARGER THAN 64 MiB -- DO NOT SHRINK IT.
// The fix loops in kMaxIOChunkBytes (64 MiB) chunks. A payload below that
// threshold takes exactly ONE iteration and therefore exercises none of the new
// code: offset advancement, partial-transfer accumulation, and the final
// total-vs-expected check are all skipped. The size below forces at least two
// iterations, so a mis-advanced offset or a dropped chunk fails here.
//
// BE HONEST ABOUT WHAT THESE THREE TESTS DO NOT CATCH. They pin the REPLACEMENT
// machinery (that the chunk loop reassembles a file byte-exactly), not the
// ORIGINAL defect. Reverting the loop to a single ReadFile/WriteFile call would
// leave all three PASSING, because 64 MiB + 4 KiB fits in a DWORD and a local
// file transfers it in one go. The only test that actually fails against the
// old code needs a file at the 4 GiB boundary -- that is the DISABLED_ test at
// the bottom of this file, and it is where the real regression pin lives.
// Do not read a green run of the first three as proof the truncation bug is
// gone; run the boundary test for that.

#include <gtest/gtest.h>

#include <file/filesystem.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>   // FSCTL_SET_SPARSE

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
    // 64 MiB (one full chunk) + 4 KiB, so the loop runs twice and the second
    // iteration is a SHORT tail rather than another whole chunk.
    constexpr std::size_t kMultiChunkBytes = 64u * 1024u * 1024u + 4096u;

    // A position-dependent pattern: a loop that reassembles chunks at the wrong
    // offset produces the right LENGTH but the wrong bytes, which a fill of a
    // single repeated value would not catch.
    std::uint8_t pattern_byte(std::size_t i)
    {
        return static_cast<std::uint8_t>((i * 31u + (i >> 13)) & 0xFFu);
    }

    std::string scratch_path(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }
}

TEST(FilesystemWholeFileIO, MultiChunkRoundTripIsByteExact)
{
    const std::string path = scratch_path("wz_fs_multichunk_roundtrip.bin");

    wz::fs::Buffer written(kMultiChunkBytes);
    for (std::size_t i = 0; i < written.size(); ++i)
        written[i] = pattern_byte(i);

    ASSERT_EQ(wz::fs::write_file(path, written, true), wz::fs::FileError::None);

    // The write must have moved every byte, not (size mod 2^32) of them and not
    // whatever one WriteFile happened to accept.
    const auto size_result = wz::fs::file_size(path);
    ASSERT_TRUE(static_cast<bool>(size_result));
    EXPECT_EQ(size_result.value, static_cast<std::uint64_t>(kMultiChunkBytes));

    const auto read_result = wz::fs::read_file(path);
    ASSERT_TRUE(static_cast<bool>(read_result));
    ASSERT_EQ(read_result.value.size(), kMultiChunkBytes);

    // Compare positionally rather than with a whole-buffer == so a failure
    // reports WHERE the reassembly went wrong, which is the useful signal.
    std::size_t first_mismatch = kMultiChunkBytes;
    for (std::size_t i = 0; i < kMultiChunkBytes; ++i) {
        if (read_result.value[i] != pattern_byte(i)) {
            first_mismatch = i;
            break;
        }
    }
    EXPECT_EQ(first_mismatch, kMultiChunkBytes)
        << "chunk reassembly diverged at byte " << first_mismatch;

    wz::fs::remove_file(path);
}

// An empty file is the boundary on the other side of the loop: the body must
// not execute at all, and the total-vs-expected check must still call it a
// success rather than an IOError.
TEST(FilesystemWholeFileIO, EmptyFileRoundTripsAsSuccess)
{
    const std::string path = scratch_path("wz_fs_empty_roundtrip.bin");

    const wz::fs::Buffer empty;
    ASSERT_EQ(wz::fs::write_file(path, empty, true), wz::fs::FileError::None);

    const auto read_result = wz::fs::read_file(path);
    EXPECT_TRUE(static_cast<bool>(read_result));
    EXPECT_EQ(read_result.error, wz::fs::FileError::None);
    EXPECT_TRUE(read_result.value.empty());

    wz::fs::remove_file(path);
}

// read_file_text funnels through read_file, so it inherits the fix; pin that it
// does, since it is the project/scene JSON read path.
TEST(FilesystemWholeFileIO, TextRoundTripPreservesEveryByte)
{
    const std::string path = scratch_path("wz_fs_multichunk_roundtrip.txt");

    std::string text;
    text.reserve(kMultiChunkBytes);
    while (text.size() < kMultiChunkBytes)
        text += "0123456789abcdef";
    text.resize(kMultiChunkBytes);

    ASSERT_EQ(wz::fs::write_file_text(path, text, true), wz::fs::FileError::None);

    const auto read_result = wz::fs::read_file_text(path);
    ASSERT_TRUE(static_cast<bool>(read_result));
    EXPECT_EQ(read_result.value.size(), text.size());
    EXPECT_TRUE(read_result.value == text);

    wz::fs::remove_file(path);
}

// ---------------------------------------------------------------------------
// THE ACTUAL REGRESSION PIN FOR A4-C1. Run it with:
//
//   platform_filesystem_whole_file_io_tests.exe \
//       --gtest_also_run_disabled_tests \
//       --gtest_filter=*FourGiBBoundary*
//
// DISABLED by default because it commits ~4 GiB of RAM for the read buffer,
// which is not a reasonable cost on every ctest run. It is NOT disabled because
// it is flaky or slow on disk: the file is SPARSE, so it occupies no meaningful
// disk space and is created instantly.
//
// This is the only test here that fails against the pre-fix code. Measured on
// issue #310 before the fix: this exact file returned 0 bytes with
// FileError::None, because ReadFile's DWORD count argument received
// (4294967296 mod 2^32) == 0 and nothing compared the result against the file
// size. If you change read_file, run THIS.
// ---------------------------------------------------------------------------
TEST(FilesystemWholeFileIO, DISABLED_FourGiBBoundaryReadsWholeFileOrFails)
{
    const std::string path = scratch_path("wz_fs_4gib_boundary.bin");

    // Exactly 2^32: the size at which a single-call read truncates to a count
    // of ZERO. One byte either side would not expose it as starkly.
    constexpr std::uint64_t kFourGiB = 4294967296ull;

    {
        // Create the file at length without writing 4 GiB of bytes. Marking it
        // sparse first keeps the on-disk cost at ~0; the reads return zeros.
        const std::wstring wide(path.begin(), path.end());
        HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(h, INVALID_HANDLE_VALUE);

        DWORD returned = 0;
        DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0,
                        &returned, nullptr);

        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(kFourGiB);
        ASSERT_NE(SetFilePointerEx(h, li, nullptr, FILE_BEGIN), 0);
        ASSERT_NE(SetEndOfFile(h), 0);
        CloseHandle(h);
    }

    const auto size_result = wz::fs::file_size(path);
    ASSERT_TRUE(static_cast<bool>(size_result));
    ASSERT_EQ(size_result.value, kFourGiB);

    const auto read_result = wz::fs::read_file(path);

    // The contract: either the WHOLE file comes back, or an explicit error.
    // What must never happen -- and what the old code did -- is success with a
    // short buffer, which is indistinguishable from a genuinely small file.
    if (read_result) {
        EXPECT_EQ(static_cast<std::uint64_t>(read_result.value.size()), kFourGiB);
    }
    else {
        EXPECT_EQ(read_result.error, wz::fs::FileError::IOError);
    }

    wz::fs::remove_file(path);
}
