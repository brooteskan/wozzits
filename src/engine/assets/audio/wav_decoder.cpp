// engine/assets/audio/wav_decoder.cpp

#include <engine/assets/audio/wav_decoder.h>

#include <cstring>
#include <limits>

namespace wz::engine::assets {

    namespace {

        // WAVE format tags.
        constexpr uint16_t kWaveFormatPcm = 0x0001;
        constexpr uint16_t kWaveFormatIeeeFloat = 0x0003;
        constexpr uint16_t kWaveFormatExtensible = 0xFFFE;

        // Little-endian readers. Bounds are validated by the caller before use.
        uint16_t read_u16(const uint8_t* p) noexcept
        {
            return static_cast<uint16_t>(p[0]) |
                   (static_cast<uint16_t>(p[1]) << 8);
        }

        uint32_t read_u32(const uint8_t* p) noexcept
        {
            return static_cast<uint32_t>(p[0]) |
                   (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24);
        }

        bool tag_equals(const uint8_t* p, const char (&tag)[5]) noexcept
        {
            return p[0] == static_cast<uint8_t>(tag[0])
                && p[1] == static_cast<uint8_t>(tag[1])
                && p[2] == static_cast<uint8_t>(tag[2])
                && p[3] == static_cast<uint8_t>(tag[3]);
        }

        // Sign-extend a 24-bit two's-complement value held in the low 24 bits.
        int32_t sign_extend_24(uint32_t v) noexcept
        {
            if (v & 0x00800000u)
                v |= 0xFF000000u;
            return static_cast<int32_t>(v);
        }

    } // namespace


    bool decode_wav(
        std::span<const uint8_t> bytes,
        AudioClipData& out,
        std::string& error)
    {
        const uint8_t* data = bytes.data();
        const size_t size = bytes.size();

        // ── 1. RIFF/WAVE container header ─────────────────────────────────────
        if (size < 12) {
            error = "WAV too small for RIFF header";
            return false;
        }
        if (!tag_equals(data, "RIFF")) {
            error = "WAV missing RIFF magic";
            return false;
        }
        if (!tag_equals(data + 8, "WAVE")) {
            error = "WAV missing WAVE form type";
            return false;
        }

        // ── 2. Walk chunks, capturing fmt and data ────────────────────────────
        bool have_fmt = false;
        uint16_t format_tag = 0;
        uint16_t channels = 0;
        uint32_t sample_rate = 0;
        uint16_t bits_per_sample = 0;

        const uint8_t* pcm = nullptr;
        size_t pcm_size = 0;

        size_t offset = 12;
        while (offset + 8 <= size) {
            const uint8_t* chunk_id = data + offset;
            const uint32_t chunk_size = read_u32(data + offset + 4);
            const size_t body = offset + 8;

            if (body + chunk_size > size) {
                // Truncated chunk: clamp the data chunk to what is present rather
                // than rejecting a file with a slightly oversized declared size.
                if (tag_equals(chunk_id, "data")) {
                    pcm = data + body;
                    pcm_size = size - body;
                    break;
                }
                error = "WAV chunk extends past end of file";
                return false;
            }

            if (tag_equals(chunk_id, "fmt ")) {
                if (chunk_size < 16) {
                    error = "WAV fmt chunk too small";
                    return false;
                }
                format_tag = read_u16(data + body + 0);
                channels = read_u16(data + body + 2);
                sample_rate = read_u32(data + body + 4);
                bits_per_sample = read_u16(data + body + 14);

                if (format_tag == kWaveFormatExtensible) {
                    // Resolve the real format from the SubFormat GUID's first two
                    // bytes (which carry the underlying WAVE_FORMAT_* tag).
                    if (chunk_size < 40) {
                        error = "WAV extensible fmt chunk too small";
                        return false;
                    }
                    format_tag = read_u16(data + body + 24);
                }
                have_fmt = true;
            }
            else if (tag_equals(chunk_id, "data")) {
                pcm = data + body;
                pcm_size = chunk_size;
            }

            // Chunks are word-aligned: an odd size is followed by a pad byte.
            offset = body + chunk_size + (chunk_size & 1u);
        }

        // ── 3. Validate header ────────────────────────────────────────────────
        if (!have_fmt) {
            error = "WAV missing fmt chunk";
            return false;
        }
        if (pcm == nullptr) {
            error = "WAV missing data chunk";
            return false;
        }
        if (channels == 0) {
            error = "WAV has zero channels";
            return false;
        }
        if (sample_rate == 0) {
            error = "WAV has zero sample rate";
            return false;
        }

        const bool is_float = (format_tag == kWaveFormatIeeeFloat);
        const bool is_pcm = (format_tag == kWaveFormatPcm);
        if (!is_float && !is_pcm) {
            error = "WAV has unsupported format tag "
                + std::to_string(format_tag)
                + " (only PCM and IEEE float are supported)";
            return false;
        }

        if (is_pcm) {
            if (bits_per_sample != 8 && bits_per_sample != 16
                && bits_per_sample != 24 && bits_per_sample != 32) {
                error = "WAV PCM bit depth " + std::to_string(bits_per_sample)
                    + " is unsupported (8/16/24/32 only)";
                return false;
            }
        }
        else { // float
            if (bits_per_sample != 32 && bits_per_sample != 64) {
                error = "WAV float bit depth " + std::to_string(bits_per_sample)
                    + " is unsupported (32/64 only)";
                return false;
            }
        }

        const uint32_t bytes_per_sample = bits_per_sample / 8u;
        const uint32_t frame_bytes = bytes_per_sample * channels;
        if (frame_bytes == 0) {
            error = "WAV has zero frame size";
            return false;
        }

        const uint64_t frame_count = pcm_size / frame_bytes;
        if (frame_count == 0) {
            error = "WAV data chunk holds no complete frames";
            return false;
        }

        // ── 4. Convert to interleaved float32 in [-1, 1] ──────────────────────
        const uint64_t total_samples =
            frame_count * static_cast<uint64_t>(channels);

        AudioClipData clip;
        clip.sample_rate = sample_rate;
        clip.channels = channels;
        clip.frame_count = frame_count;
        clip.source = AudioClipSource::Wav;
        clip.samples.resize(static_cast<size_t>(total_samples));

        const uint8_t* sp = pcm;
        for (uint64_t i = 0; i < total_samples; ++i) {
            float value = 0.0f;

            if (is_float) {
                if (bits_per_sample == 32) {
                    uint32_t bits = read_u32(sp);
                    std::memcpy(&value, &bits, sizeof(float));
                }
                else { // 64-bit double
                    uint32_t lo = read_u32(sp);
                    uint32_t hi = read_u32(sp + 4);
                    uint64_t bits = static_cast<uint64_t>(lo)
                        | (static_cast<uint64_t>(hi) << 32);
                    double d = 0.0;
                    std::memcpy(&d, &bits, sizeof(double));
                    value = static_cast<float>(d);
                }
            }
            else { // PCM integer
                switch (bits_per_sample) {
                case 8:
                    // 8-bit WAV PCM is unsigned, centred at 128.
                    value = (static_cast<float>(sp[0]) - 128.0f) / 128.0f;
                    break;
                case 16: {
                    int16_t s = static_cast<int16_t>(read_u16(sp));
                    value = static_cast<float>(s) / 32768.0f;
                    break;
                }
                case 24: {
                    uint32_t raw = static_cast<uint32_t>(sp[0])
                        | (static_cast<uint32_t>(sp[1]) << 8)
                        | (static_cast<uint32_t>(sp[2]) << 16);
                    value = static_cast<float>(sign_extend_24(raw)) / 8388608.0f;
                    break;
                }
                case 32: {
                    int32_t s = static_cast<int32_t>(read_u32(sp));
                    value = static_cast<float>(
                        static_cast<double>(s) / 2147483648.0);
                    break;
                }
                default:
                    break;
                }
            }

            clip.samples[static_cast<size_t>(i)] = value;
            sp += bytes_per_sample;
        }

        out = std::move(clip);
        return true;
    }

} // namespace wz::engine::assets
