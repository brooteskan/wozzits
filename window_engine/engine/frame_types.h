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

    struct FrameDirtyState
    {
        bool render_scene_dirty = true;
        bool render_transforms_dirty = false;
        bool render_view_dirty = true;
        bool collision_world_dirty = false;

        void mark_render_full_compile()
        {
            render_scene_dirty = true;
            render_transforms_dirty = false;
            render_view_dirty = true;
        }

        void mark_render_view_only()
        {
            render_scene_dirty = false;
            render_transforms_dirty = false;
            render_view_dirty = true;
        }

        void mark_render_transform_only()
        {
            render_scene_dirty = false;
            render_transforms_dirty = true;
            render_view_dirty = false;
        }

        void mark_render_transform_and_view()
        {
            render_scene_dirty = false;
            render_transforms_dirty = true;
            render_view_dirty = true;
        }

        RenderPrepPath render_prep_path() const
        {
            if (render_scene_dirty)
                return RenderPrepPath::FullCompile;

            if (render_transforms_dirty && render_view_dirty)
                return RenderPrepPath::TransformAndView;

            if (render_transforms_dirty)
                return RenderPrepPath::TransformOnly;

            return RenderPrepPath::ViewOnly;
        }
    };
}
