#pragma once

// wozzits/rhi/draw_encode.h
//
// The draw-encode seam: decompose one selected DrawPacket pass into backend
// recorder verbs without knowing whether the pipeline uses IA or vertex pull.

#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/frame_graph.h>

#include <cstdint>
#include <span>

namespace wz::rhi
{
    [[nodiscard]] inline DrawArgs make_draw_args(const GeometryView& geometry)
    {
        DrawArgs args;
        // An index buffer being BOUND is what makes this an indexed draw; the
        // count says how much work there is. Those are different questions, and
        // conflating them (args.indexed = geometry.indexed(), which is
        // `index_buffer.valid() && index_count != 0`) meant a bound index
        // buffer with zero indices silently became a NON-indexed draw of
        // vertex_count instead. Measured: a cube with index_count zeroed drew
        // 8 vertices instead of 36, recorder ready() still true, no
        // diagnostic -- a different, wrong draw rather than a refusal. Asking
        // only about the buffer sends it to the backend's existing zero-count
        // rejection ("draw: index_count is 0") instead. See #317.
        args.indexed       = geometry.index_buffer.valid();
        args.index_count   = geometry.index_count;
        args.vertex_count  = geometry.vertex_count;
        args.first_index   = geometry.first_index;
        args.vertex_offset = geometry.vertex_offset;
        return args;
    }

    inline void record_packet(const DrawPacket& packet,
                              DrawListTag pass,
                              CommandRecorder& recorder)
    {
        const DrawItem* item = packet.get_draw_item(pass);
        if (!item) {
            return;
        }

        recorder.set_pipeline(item->program);
        if (!packet.root_constants.empty()) {
            recorder.set_root_constants(std::span<const uint8_t>{
                packet.root_constants.data(),
                packet.root_constants.size() });
        }
        for (const ShaderResourceGroup* srg : packet.shared_srgs) {
            if (srg) {
                recorder.bind_resource_group(srg->binding_slot(), *srg);
            }
        }
        if (item->unique_srg) {
            recorder.bind_resource_group(
                item->unique_srg->binding_slot(),
                *item->unique_srg);
        }
        recorder.set_geometry(packet.geometry, item->streams);
        recorder.draw(make_draw_args(packet.geometry));
    }
}
