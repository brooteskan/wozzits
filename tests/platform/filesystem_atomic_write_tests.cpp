// tests/platform/filesystem_atomic_write_tests.cpp
//
// wz::fs::write_file_atomic / write_file_text_atomic -- the crash-atomic replace
// behind every scene-document write.
//
// The defect it exists for: a plain write_file opens the destination
// CREATE_ALWAYS, which TRUNCATES it before the first byte is written. A process
// death anywhere in that window (a TDR taking the app down, a power loss, a
// terminate on an escaped exception) leaves scene.json half-serialized. The next
// save reads it, fails to parse, and the create-path fallback treats an
// unparseable document as "no scene on disk yet" -- re-emitting from the node
// snapshot alone and permanently discarding the lights, sky, and defaults the
// read-modify-write exists to preserve. One crash becomes silent authored-data
// loss, long after the crash.
//
// WHAT CAN AND CANNOT BE TESTED HERE. The literal scenario -- kill the process
// mid-write -- is not reachable in-process. What IS reachable, and is what these
// tests pin, is the property that makes the crash survivable: THE DESTINATION IS
// NEVER OPENED FOR WRITING. It is only ever replaced, in one step, by a file
// that is already complete on disk. So every failure mode short of the rename
// leaves the old bytes untouched, and the tests below drive the failure modes
// that ARE reachable (unwritable staging directory, undeletable destination) and
// assert exactly that. A test that only checked the happy path would pass
// identically against the truncating writer it replaces.

#include <gtest/gtest.h>

#include <file/filesystem.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <AccCtrl.h>
#include <AclAPI.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    namespace fs = wz::fs;

    // A scratch directory per test, removed on the way out however the body
    // leaves -- a leaked staging file is one of the things under test, so the
    // tests must not inherit litter from each other.
    class AtomicWriteDir : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const ::testing::TestInfo* info =
                ::testing::UnitTest::GetInstance()->current_test_info();
            dir = fs::join(fs::temp_directory_path(),
                           std::string("wz_atomic_") + info->name());
            fs::remove_directory(dir, /*recursive*/ true);
            ASSERT_EQ(fs::create_directories(dir), fs::FileError::None);
        }

        void TearDown() override
        {
            fs::remove_directory(dir, /*recursive*/ true);
        }

        std::string in_dir(const char* name) const { return fs::join(dir, name); }

        // Everything in the scratch directory, so a test can assert that a
        // staging file did not survive.
        std::vector<std::string> entries() const
        {
            std::vector<std::string> names;
            const fs::FileResult<std::vector<fs::DirEntry>> listed =
                fs::list_directory(dir);
            if (listed)
                for (const fs::DirEntry& e : listed.value)
                    names.push_back(e.name);
            return names;
        }

        std::string dir;
    };

    TEST_F(AtomicWriteDir, WritesANewFileByteExactly)
    {
        const std::string path = in_dir("fresh.json");
        const std::string text = "{\"nodes\":[],\"sky\":{\"turbidity\":3}}";

        ASSERT_EQ(fs::write_file_text_atomic(path, text), fs::FileError::None);

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_EQ(back.value, text);
    }

    TEST_F(AtomicWriteDir, ReplacesAnExistingFileCompletely)
    {
        const std::string path = in_dir("scene.json");
        // Longer than the replacement, so a writer that overwrote IN PLACE would
        // leave a tail of the old document behind and fail this.
        ASSERT_EQ(fs::write_file_text(path, std::string(4096, 'A')),
                  fs::FileError::None);

        ASSERT_EQ(fs::write_file_text_atomic(path, "short"), fs::FileError::None);

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_EQ(back.value, "short");
    }

    // The staging file is an implementation detail that must not outlive the
    // call. A leak here means a scene directory accumulating a .wztmp sibling per
    // save.
    TEST_F(AtomicWriteDir, LeavesNoStagingFileBehindOnSuccess)
    {
        const std::string path = in_dir("scene.json");
        for (int i = 0; i < 5; ++i)
            ASSERT_EQ(fs::write_file_text_atomic(path, "payload"),
                      fs::FileError::None) << "write " << i;

        const std::vector<std::string> names = entries();
        ASSERT_EQ(names.size(), 1u) << "expected only scene.json to remain";
        EXPECT_EQ(names[0], "scene.json");
    }

    TEST_F(AtomicWriteDir, HandlesNonAsciiPaths)
    {
        // "café_日本" as explicit UTF-8 bytes, matching the convention in
        // filesystem_whole_file_io_tests: the staging name is derived from the
        // destination, so a widening bug would hit the temp and the rename too.
        const std::string path =
            in_dir("caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC.json");
        ASSERT_EQ(fs::write_file_text_atomic(path, "unicode"),
                  fs::FileError::None);

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_EQ(back.value, "unicode");

        const std::vector<std::string> names = entries();
        EXPECT_EQ(names.size(), 1u) << "a staging file survived a non-ASCII write";
    }

    TEST_F(AtomicWriteDir, WritesEmptyContent)
    {
        const std::string path = in_dir("empty.json");
        ASSERT_EQ(fs::write_file_text(path, "not empty"), fs::FileError::None);

        ASSERT_EQ(fs::write_file_text_atomic(path, ""), fs::FileError::None);

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_TRUE(back.value.empty());
        EXPECT_EQ(entries().size(), 1u);
    }

    // ─── Failure modes: the destination must survive all of them ────────────

    // Deny THIS user FILE_ADD_FILE on `wdir`, keeping every other permission
    // (including write access to files already in it) by folding the deny ACE
    // into the existing DACL rather than replacing it. That is the one
    // permission split that separates the two writers: creating the staging
    // sibling needs it, truncating an existing destination does not.
    bool try_deny_file_creation(const std::wstring& wdir)
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

        PSID sid = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;

        // The CURRENT DACL, so the deny is added to the existing grants instead
        // of wiping them -- the test still has to read the destination back.
        PACL old_acl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (GetNamedSecurityInfoW(wdir.c_str(), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, &old_acl, nullptr,
                                  &sd) != ERROR_SUCCESS)
            return false;

        EXPLICIT_ACCESSW ea = {};
        ea.grfAccessPermissions = FILE_ADD_FILE;
        ea.grfAccessMode = DENY_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
        ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);

        PACL acl = nullptr;
        const bool built = SetEntriesInAclW(1, &ea, old_acl, &acl) == ERROR_SUCCESS;

        DWORD rc = ERROR_ACCESS_DENIED;
        if (built)
        {
            rc = SetNamedSecurityInfoW(const_cast<LPWSTR>(wdir.c_str()),
                                       SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                       nullptr, nullptr, acl, nullptr);
        }

        if (acl)
            LocalFree(acl);
        if (sd)
            LocalFree(sd);

        return rc == ERROR_SUCCESS;
    }

    // Back to a NULL DACL (everyone full control) so TearDown can delete the
    // directory again.
    void restore_directory_access(const std::wstring& wdir)
    {
        SetNamedSecurityInfoW(const_cast<LPWSTR>(wdir.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              nullptr, nullptr);
    }

    // THE TEST THAT ACTUALLY DISCRIMINATES. Every other test here would pass
    // unchanged against the truncating writer; this one fails against it.
    //
    // The directory refuses new files but the destination is still writable, so
    // the two writers diverge: write_file opens the EXISTING destination
    // CREATE_ALWAYS -- which needs no directory permission -- and empties it
    // before discovering nothing else can go wrong, while write_file_atomic
    // fails trying to create its staging sibling and never opens the
    // destination at all. This is the in-process stand-in for "the process died
    // mid-write": the observable that matters is not how the write failed, it is
    // that the destination was never opened for writing, so its bytes are still
    // there afterwards.
    TEST_F(AtomicWriteDir, AFailedWriteNeverTouchesTheDestination)
    {
        const std::string path = in_dir("scene.json");
        const std::string original = "{\"lights\":[1,2,3],\"sky\":\"dusk\"}";
        ASSERT_EQ(fs::write_file_text(path, original), fs::FileError::None);

        const std::wstring wdir(dir.begin(), dir.end());  // ASCII scratch path
        if (!try_deny_file_creation(wdir))
            GTEST_SKIP() << "could not apply a deny-create DACL in this environment";

        struct Restore
        {
            const std::wstring& w;
            ~Restore() { restore_directory_access(w); }
        } restore{ wdir };

        // Skip rather than assert a pass the environment cannot back (an admin
        // shell holding backup privilege bypasses the DACL).
        if (fs::write_file_text(in_dir("probe.tmp"), "x") == fs::FileError::None)
        {
            fs::remove_file(in_dir("probe.tmp"));
            GTEST_SKIP() << "deny-create DACL not enforced in this environment";
        }

        EXPECT_NE(fs::write_file_text_atomic(path, "REPLACEMENT"),
                  fs::FileError::None)
            << "staging should have failed with file creation denied";

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_EQ(back.value, original)
            << "the destination was modified by a write that failed";
    }

    // The publish fails: the destination is held open by another handle with no
    // sharing, so MoveFileEx cannot replace it. The old bytes must remain, AND
    // the staging file must be cleaned up rather than left as litter -- a failing
    // save that also drops a temp beside the scene is the worst of both.
    TEST_F(AtomicWriteDir, PublishFailureLeavesNeitherDamageNorLitter)
    {
        const std::string path = in_dir("scene.json");
        const std::string original = "{\"sky\":\"preserved\"}";
        ASSERT_EQ(fs::write_file_text(path, original), fs::FileError::None);

        // FILE_SHARE_NONE: nothing may rename or delete this file while the
        // handle is open. This is also a real scenario -- an editor or a sync
        // client holding the scene file open.
        const std::wstring wpath(path.begin(), path.end());  // ASCII-only here
        HANDLE hold = CreateFileW(wpath.c_str(),
                                  GENERIC_READ,
                                  0,  // no sharing
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        ASSERT_NE(hold, INVALID_HANDLE_VALUE);

        const fs::FileError err = fs::write_file_text_atomic(path, "replacement");
        EXPECT_NE(err, fs::FileError::None) << "the replace should have been refused";

        CloseHandle(hold);

        const fs::FileResult<std::string> back = fs::read_file_text(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        EXPECT_EQ(back.value, original) << "a failed atomic write damaged the file";

        const std::vector<std::string> names = entries();
        ASSERT_EQ(names.size(), 1u) << "the staging file was not cleaned up";
        EXPECT_EQ(names[0], "scene.json");
    }

    // Byte-exactness over a payload big enough to cross the chunked-write loop's
    // 64 MiB boundary is already covered for write_file itself
    // (filesystem_whole_file_io_tests); this checks the atomic wrapper carries a
    // non-trivial binary payload through staging + rename unchanged, including
    // embedded NULs, which the text path cannot express.
    TEST_F(AtomicWriteDir, CarriesBinaryPayloadsThroughTheRename)
    {
        const std::string path = in_dir("blob.bin");

        fs::Buffer data(64 * 1024);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<fs::Byte>((i * 31u + (i >> 9)) & 0xFFu);

        ASSERT_EQ(fs::write_file_atomic(path, data), fs::FileError::None);

        const fs::FileResult<fs::Buffer> back = fs::read_file(path);
        ASSERT_EQ(back.error, fs::FileError::None);
        ASSERT_EQ(back.value.size(), data.size());
        EXPECT_TRUE(back.value == data);
    }
}
