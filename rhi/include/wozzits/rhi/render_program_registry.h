#pragma once

// wozzits/rhi/render_program_registry.h
//
// Render programs as registered data, not as a BuiltinRenderProgram enum.
//
// In the old engine, a render program was an enum member, and the data that
// described it (shader paths, root-constant count, bind strategy) lived
// scattered across switch statements in submit, the pipeline factory, the
// shader-pair map, and a hand-maintained list in the editor. Adding a program
// meant editing all of them; missing one failed silently.
//
// Here a render program is one RenderProgramDesc, registered by name, addressed
// by a Tag. There is no enum to drift against. Resolving an unregistered
// program is a checkable null (nullptr / null Tag), never a silent skip.
//
// The registry logic is the generic ProgramRegistry; render and compute
// programs are the same machinery over different descriptors.

#include <wozzits/rhi/program_registry.h>
#include <wozzits/rhi/render_program.h>

#include <cstddef>

namespace wz::rhi
{
    inline constexpr size_t kMaxRenderPrograms = 256;

    using RenderProgramRegistry =
        ProgramRegistry<RenderProgramDesc, kMaxRenderPrograms>;
}
