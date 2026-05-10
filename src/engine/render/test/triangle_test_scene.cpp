#include <engine/render/test/test_triangle_scene.h>
#include <scene/compile/legacy_classification.h>

using namespace wz::scene;
using namespace wz::core::graph;
using namespace wz::math;


namespace wz::gpu::dx12
{

    Mat4 translation_x(float c)
    {
        Mat4 m = mat4_identity();
        m.m[12] = c;
        return m;
    }
    Mat4 translation_z(float c)
    {
        Mat4 m = mat4_identity();
        m.m[14] = c;
        return m;
    }

    SceneStorage build_test_scene()
    {
        SceneBuilder b;

        TransformNode root{};
        root.local = mat4_identity();
        auto root_h = add_node(b, root);

        TransformNode node{};
        node.local = translation_z(-3.0f); //
        // node.local = translation_x(0.1f);
        node.flags = TransformNodeFlag::RenderDomain;

        auto h = add_node(b, node);
        add_edge(b, root_h, h);

        auto result = build(std::move(b));
        assert(result.has_value());

        return std::move(*result);
    }


    std::vector<RenderableDescriptor> build_descriptors()
    {
        std::vector<RenderableDescriptor> descs(2);

        descs[0] = { classify_legacy_renderable(RenderPipeline::None) };

        descs[1] = {
            classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            /*mesh=*/0,
            /*material=*/0,
            {}
        };

        return descs;
    }

    Mat4 _perspective(float fov, float aspect, float nearZ, float farZ)
    {
        float f = 1.0f / tanf(fov * 0.5f);

        // DX [0,1] depth — different A/B from OpenGL [-1,1]
        float A = farZ / (nearZ - farZ);
        float B = (nearZ * farZ) / (nearZ - farZ);

        Mat4 m = {};
        // column-major layout — matches scene graph convention
        m.m[0] = f / aspect;
        m.m[5] = f;
        m.m[10] = A;
        m.m[11] = -1.0f;   // col 2, row 3
        m.m[14] = B;        // col 3, row 2
        return m;
    }



    using namespace wz::render;

    ViewData make_camera()
    {
        ViewData v{};

        v.camera_position = Vec3{ 0.0f, 0, 0 };

        v.view = translation_x(-v.camera_position.x);


        float fov = 90.0f * 3.14159265f / 180.0f;
        float aspect = 1280.0f / 720.0f;
        float nearZ = 0.1f;
        float farZ = 100.0f;

        v.projection = _perspective(fov, aspect, nearZ, farZ);
        v.view_projection = mul(v.projection, v.view); // IMPORTANT ORDER*/

        return v;
    }

    
    TriangleTestFrame build_triangle_test_frame()
    {
        auto scene = build_test_scene();
        auto descs = build_descriptors();
        auto view = make_camera();

        propagate_all(scene.polytree);

        TriangleTestFrame out{};
        out.view = view;

        wz::scene::CompiledSceneStorage compiled_storage;
        wz::render::RenderIRStorage     ir_storage;

        auto cs = wz::scene::compile(compiled_storage, scene.polytree, descs, {}, view);
        auto ir = wz::render::build_render_ir(ir_storage, cs);
        wz::render::build_frame(out.frame_storage, ir, cs);

        return out;
    }
}