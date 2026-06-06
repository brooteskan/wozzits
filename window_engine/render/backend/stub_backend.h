#pragma once

// wz/render/stub_backend.h
//
// Stub backend — consumes RenderFrame, produces a human-readable
// submission log. No GPU types. Used for testing and debugging.

#include <render/frame/render_frame.h>
#include <algo/next.h>
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

        uint32_t sky_count()         const { return counts[0]; }
        uint32_t opaque_count()      const { return counts[1]; }
        uint32_t splat_count()       const { return counts[2]; }
        uint32_t transparent_count() const { return counts[3]; }
        uint32_t particle_count()    const { return counts[4]; }
        uint32_t total()             const {
            return counts[0] + counts[1] + counts[2] + counts[3] + counts[4];
        }

        uint32_t counts[5]{};
    };

    namespace detail {

        inline const char* stage_name(PipelineStage s)
        {
            switch (s) {
            case PipelineStage::Sky:                 return "sky";
            case PipelineStage::OpaqueGeometry:      return "opaque";
            case PipelineStage::Splat:               return "splat";
            case PipelineStage::TransparentGeometry: return "transparent";
            case PipelineStage::Particle:            return "particle";
            }
            return "unknown";
        }

        inline std::string format_sky_command(
            const SkyDrawCommand& cmd,
            uint32_t idx)
        {
            std::ostringstream ss;
            ss << "[" << idx << "] "
                << stage_name(cmd.stage)
                << " visual=" << static_cast<uint32_t>(cmd.visual_kind)
                << " projection=" << static_cast<uint32_t>(cmd.projection);
            return ss.str();
        }

        inline std::string format_command(const DrawCommand& cmd, uint32_t idx)
        {
            std::ostringstream ss;
            ss << "[" << idx << "] "
                << stage_name(cmd.stage)
                << " key=0x" << std::hex << cmd.sort_key << std::dec;

            if (cmd.kind == DrawCommandKind::GaussianSplats) {
                ss << " splats=" << cmd.splats_buffer
                    << " instances=" << cmd.splat_instance_count;
            }
            else if (cmd.stage == PipelineStage::Splat) {
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

        struct SubmitSink {
            SubmitResult& result;
            uint32_t&     idx;

            bool push(const DrawCommand& cmd)
            {
                result.log.push_back(format_command(cmd, idx++));
                switch (cmd.stage) {
                case PipelineStage::Sky:                 ++result.counts[0]; break;
                case PipelineStage::OpaqueGeometry:      ++result.counts[1]; break;
                case PipelineStage::Splat:               ++result.counts[2]; break;
                case PipelineStage::TransparentGeometry: ++result.counts[3]; break;
                case PipelineStage::Particle:            ++result.counts[4]; break;
                }
                return true;
            }

            bool push(const SkyDrawCommand& cmd)
            {
                result.log.push_back(format_sky_command(cmd, idx++));
                ++result.counts[0];
                return true;
            }
        };

    } // namespace detail


    // ─── submit() ─────────────────────────────────────────────────────────────────
    //
    // Iterates all four RenderFrame sections in submission order:
    //   opaque → splats → transparent → particles
    // Logs each command. Counts by pipeline stage.
    // This is where real GPU commands would be encoded.

    SubmitResult submit(const RenderFrameView& frame)
    {
        SubmitResult result;
        uint32_t idx = 0;
        detail::SubmitSink sink{ result, idx };

        auto process_sky = [&](std::span<const SkyDrawCommand> cmds) {
            wz::core::algo::next::transform(
                cmds,
                sink,
                [](const SkyDrawCommand& cmd) -> const SkyDrawCommand& { return cmd; });
        };

        auto process = [&](std::span<const DrawCommand> cmds) {
            wz::core::algo::next::transform(
                cmds,
                sink,
                [](const DrawCommand& cmd) -> const DrawCommand& { return cmd; });
        };

        process_sky(frame.sky);
        process(frame.opaque);
        process(frame.splats);
        process(frame.transparent);
        process(frame.particles);

        return result;
    }

} // namespace wz::render::backend
