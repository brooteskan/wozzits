#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

// The tinyexr statics this build never reaches.
//
// TINYEXR_IMPLEMENTATION compiles the whole library into this TU, including
// compressors for codecs our EXR write path does not select. Each is `static`,
// so each is a -Wunused-function. Naming them here rather than passing
// -Wno-unused-function for the file means the list stays honest: start writing
// PXR24 or B44 and you delete a line, and a tinyexr update that adds another
// dead static warns until someone decides.
//
// Note this cannot be solved with a SYSTEM include dir -- this file sits beside
// tinyexr.h and includes it as "tinyexr.h", a quoted same-directory include that
// never consults the -I search path, so clang never marks the header as a system
// header. This shim is ours, though, so the list belongs here rather than in
// vendored source.
//
// Nothing references the function below, so the linker still drops all of it.
[[maybe_unused]] static void tinyexr_statics_this_build_does_not_reach()
{
    // Lossy compressors, LEGACY overload only. tinyexr ships each of these
    // twice -- once taking `const EXRChannelInfo*` and once taking
    // `const std::vector<ChannelInfo>&` -- and calls only the vector form
    // (tinyexr.h:8759, :8807). The pointer form is what is dead, so the
    // signature has to be spelled out to pick it out of the overload set. If a
    // tinyexr update changes either signature this stops compiling, which is
    // the right failure: someone looks again.
    (void)static_cast<bool (*)(std::vector<unsigned char>&,
                               const unsigned char*, size_t, int, int, size_t,
                               const EXRChannelInfo*)>(
        &tinyexr::CompressPxr24);
    (void)static_cast<bool (*)(std::vector<unsigned char>&,
                               const unsigned char*, size_t, int, int, size_t,
                               const EXRChannelInfo*, bool)>(
        &tinyexr::CompressB44);

    // Custom-attribute helper. The engine writes no custom EXR attributes.
    (void)&AddIntAttribute;
}
