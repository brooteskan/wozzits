#pragma once
// engine/frame_types.h

namespace wz::engine
{
    // Chooses the cheapest scene-to-render-frame update path for a frame.
    enum class RenderPrepPath
    {
        FullCompile,
        ViewOnly,
        TransformOnly,
        TransformAndView,
    };
}
