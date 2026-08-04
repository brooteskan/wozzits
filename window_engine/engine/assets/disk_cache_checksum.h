#pragma once

// engine/assets/disk_cache_checksum.h
//
// A trailing integrity checksum shared by every engine disk-cache format.
//
// Each cache blob is [header][descriptors][sample payload]; before this, the
// only integrity checks were the magic word, the format/compiler versions and
// the stored AssetKey -- all of which live in the header and none of which read
// the sample payload. So a flip in the cached floats, in a min/max bound, or a
// reshape of the extents that preserves the byte count (2x2x1 -> 4x1x1) loaded
// silently: the cache path is derived from the key and the stored-key check
// compares against that same key, so every existing check passed and the wrong
// bytes were served. See #75 B1-C4.
//
// serialize_* appends a checksum over the whole blob as its LAST bytes;
// deserialize_* verifies and strips it as its FIRST step, so the two remain a
// matched round-trip pair and the existing decode body is untouched -- it sees
// exactly the bytes the serializer emitted before the checksum. A mismatch (or
// a blob shorter than the checksum) is reported as a cache miss, which forces a
// recompile: the caches are locally written, so a failed check means bit rot, a
// torn write, or an in-place edit, never a value worth trusting.
//
// The checksum is FNV-1a 64, matching the key-hash family in
// engine_asset_key_core.h. It is a corruption guard, not a cryptographic MAC:
// the threat is accidental damage to a file this process wrote, not a forged
// entry.

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace wz::engine::assets {

    inline constexpr std::size_t kDiskCacheChecksumSize = sizeof(std::uint64_t);

    [[nodiscard]] inline std::uint64_t disk_cache_checksum(
        std::span<const std::uint8_t> bytes) noexcept
    {
        std::uint64_t h = 14695981039346656037ull;
        for (const std::uint8_t b : bytes) {
            h ^= static_cast<std::uint64_t>(b);
            h *= 1099511628211ull;
        }
        return h;
    }

    // Append an 8-byte checksum over everything written so far. Call this as the
    // LAST step of a serializer, after the sample payload.
    inline void append_disk_cache_checksum(std::vector<std::uint8_t>& out)
    {
        const std::uint64_t sum = disk_cache_checksum(out);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&sum);
        out.insert(out.end(), p, p + sizeof(sum));
    }

    // Verify the trailing checksum and copy the verified payload -- everything
    // before it -- into `payload`. Returns false (a cache miss) if the blob is
    // shorter than the checksum or the checksum does not match. Call this as the
    // FIRST step of a deserializer, then decode `payload` exactly as before.
    [[nodiscard]] inline bool verify_and_strip_disk_cache_checksum(
        const std::vector<std::uint8_t>& blob,
        std::vector<std::uint8_t>& payload)
    {
        if (blob.size() < kDiskCacheChecksumSize) {
            return false;
        }
        const std::size_t payload_size = blob.size() - kDiskCacheChecksumSize;
        std::uint64_t stored = 0;
        std::memcpy(&stored, blob.data() + payload_size, sizeof(stored));
        if (disk_cache_checksum({ blob.data(), payload_size }) != stored) {
            return false;
        }
        payload.assign(blob.begin(), blob.begin() + payload_size);
        return true;
    }

} // namespace wz::engine::assets
