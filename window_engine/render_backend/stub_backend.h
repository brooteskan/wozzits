#pragma once

// wz/render/stub_backend.h
//
// Stub backend — consumes RenderFrame, produces a human-readable
// submission log. No GPU types. Used for testing and debugging.

#include <render/frame/render_frame.h>
#include <sstream>
#include <string>
#include <vector>

namespace wz::render::backend {

    // ─── SubmitResult ─────────────────────────────────────────────────────────────
    //
    // The output of the stub backend.
    // log entries are in submission order — one per DrawCommand.

    struct SubmitResult {
        std::vector<std::string> log;

        uint32_t opaque_count()      const { return counts[0]; }
        uint32_t splat_count()       const { return counts[1]; }
        uint32_t transparent_count() const { return counts[2]; }
        uint32_t particle_count()    const { return counts[3]; }
        uint32_t total()             const {
            return counts[0] + counts[1] + counts[2] + counts[3];
        }

        uint32_t counts[4]{};
    };

    namespace detail {

        inline const char* stage_name(PipelineStage s)
        {
            switch (s) {
            case PipelineStage::OpaqueGeometry:      return "opaque";
            case PipelineStage::Splat:               return "splat";
            case PipelineStage::TransparentGeometry: return "transparent";
            case PipelineStage::Particle:            return "particle";
            }
            return "unknown";
        }

        inline std::string format_command(const DrawCommand& cmd, uint32_t idx)
        {
            std::ostringstream ss;
            ss << "[" << idx << "] "
                << stage_name(cmd.stage)
                << " key=0x" << std::hex << cmd.sort_key << std::dec;

            if (cmd.stage == PipelineStage::Splat) {
                ss << " pos=(" << cmd.splat_position.x
                    << "," << cmd.splat_position.y
                    << "," << cmd.splat_position.z << ")"
                    << " opacity=" << cmd.splat_opacity
                    << " depth=" << cmd.splat_depth;
            }
            else {
                ss << " mesh=" << cmd.mesh
                    << " mat=" << cmd.material;
            }

            return ss.str();
        }

    } // namespace detail


    // ─── submit() ─────────────────────────────────────────────────────────────────
    //
    // Iterates RenderFrame commands in order.
    // Logs each command. Counts by pipeline stage.
    // This is where real GPU commands would be encoded.

    SubmitResult submit(const RenderFrame& frame)
    {
        SubmitResult result;

        for (uint32_t i = 0; i < frame.commands.size(); ++i) {
            const DrawCommand& cmd = frame.commands[i];
            result.log.push_back(detail::format_command(cmd, i));

            switch (cmd.stage) {
            case PipelineStage::OpaqueGeometry:      ++result.counts[0]; break;
            case PipelineStage::Splat:               ++result.counts[1]; break;
            case PipelineStage::TransparentGeometry: ++result.counts[2]; break;
            case PipelineStage::Particle:            ++result.counts[3]; break;
            }
        }

        return result;
    }

} // namespace wz::render::backend