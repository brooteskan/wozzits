#pragma once

// engine/assets/audio/wav_decoder.h
//
// Minimal, dependency-free RIFF/WAVE decoder. Pure function over a byte span ->
// interleaved-float32 AudioClipData, so it can be unit tested without the asset
// system, the file system, or an audio device.
//
// Supported formats (the common cases produced by every authoring tool):
//   • WAVE_FORMAT_PCM (1):        8-bit unsigned, 16/24/32-bit signed integer
//   • WAVE_FORMAT_IEEE_FLOAT (3): 32-bit and 64-bit float
//   • WAVE_FORMAT_EXTENSIBLE (0xFFFE): resolved via the SubFormat GUID to one of
//     the above.
//
// Anything else (compressed/ADPCM/etc.) is rejected with an error string. The
// decoder converts all integer formats to float32 normalised to [-1, 1].

#include <engine/assets/audio/audio_clip.h>

#include <cstdint>
#include <span>
#include <string>

namespace wz::engine::assets {

    // Decode WAV bytes into out. On success returns true and fills out (with
    // source == AudioClipSource::Wav). On failure returns false, leaves out
    // unspecified, and writes a human-readable reason into error.
    [[nodiscard]] bool decode_wav(
        std::span<const uint8_t> bytes,
        AudioClipData& out,
        std::string& error);

} // namespace wz::engine::assets
