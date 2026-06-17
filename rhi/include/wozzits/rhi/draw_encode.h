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
        args.indexed       = geometry.indexed();
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
