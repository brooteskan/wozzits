#include "wz_test.h"

#include <wozzits/rhi/draw_item.h>
#include <wozzits/rhi/geometry_view.h>

using namespace wz::rhi;

namespace
{
    GeometryView make_mesh_geometry()
    {
        GeometryView geometry;
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 10, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 11, 1 },
            /*offset*/ 0,
            /*stride*/ 12 });
        geometry.streams.push_back(StreamBufferView{
            GpuResourceHandle{ 12, 1 },
            /*offset*/ 0,
            /*stride*/ 8 });

        geometry.index_buffer = GpuResourceHandle{ 20, 1 };
        geometry.index_count = 36;
        geometry.vertex_count = 24;
        return geometry;
    }
}

static void geometry_view_carries_streams_and_index_range()
{
    const GeometryView geometry = make_mesh_geometry();

    WZ_CHECK_EQ(geometry.streams.size(), static_cast<size_t>(3));
    WZ_CHECK(geometry.streams[0].valid());
    WZ_CHECK(geometry.indexed());
    WZ_CHECK_EQ(geometry.index_count, 36u);
    WZ_CHECK_EQ(geometry.vertex_count, 24u);
}

static void stream_indices_select_per_pass_subsets()
{
    const GeometryView geometry = make_mesh_geometry();

    StreamBufferIndices depth;
    WZ_CHECK(depth.add(0));        // position only

    StreamBufferIndices forward;
    WZ_CHECK(forward.add(0));      // position
    WZ_CHECK(forward.add(1));      // normal
    WZ_CHECK(forward.add(2));      // uv

    WZ_CHECK(depth.valid_for(geometry));
    WZ_CHECK(forward.valid_for(geometry));
    WZ_CHECK_EQ(depth.indices.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(forward.indices.size(), static_cast<size_t>(3));
}

static void invalid_and_duplicate_stream_indices_are_checkable()
{
    const GeometryView geometry = make_mesh_geometry();

    StreamBufferIndices indices;
    WZ_CHECK(indices.add(0));
    WZ_CHECK_FALSE(indices.add(0));
    WZ_CHECK(indices.add(99));
    WZ_CHECK_FALSE(indices.valid_for(geometry));
}

static void draw_item_carries_stream_selection()
{
    const GeometryView geometry = make_mesh_geometry();

    DrawItem depth_item;
    WZ_CHECK(depth_item.streams.add(0));
    WZ_CHECK(depth_item.streams.valid_for(geometry));

    DrawItem forward_item;
    WZ_CHECK(forward_item.streams.add(0));
    WZ_CHECK(forward_item.streams.add(1));
    WZ_CHECK(forward_item.streams.add(2));
    WZ_CHECK(forward_item.streams.valid_for(geometry));
}

int main()
{
    WZ_RUN(geometry_view_carries_streams_and_index_range);
    WZ_RUN(stream_indices_select_per_pass_subsets);
    WZ_RUN(invalid_and_duplicate_stream_indices_are_checkable);
    WZ_RUN(draw_item_carries_stream_selection);
    WZ_TEST_RETURN();
}
