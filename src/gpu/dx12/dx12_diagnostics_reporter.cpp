#include <gpu/gpu_diagnostics.h>

#include <gpu/dx12/dx12_internal.h>  // kDebugPassOffscreen (pass convention)
#include <diagnostics/logger_service.h>
#include <logging/logger.h>

#include <d3d12.h>

#include <string>

// The D3D12 backend's debug-layer reporter (#291): the concrete formatter behind
// wz::gpu::make_debug_layer_reporter. Runs on the LoggerService lane -- all the
// string-building the render thread no longer does happens here, off the hot lane.

namespace wz::gpu
{
    namespace
    {
        // D3D12_MESSAGE_CATEGORY -> name. The record carries the category as an
        // opaque uint8 (the producer casts the enum); resolving it to a name is the
        // cold lane's job. A small, fully-known enum -- unlike the ~1300 message
        // ids, which stay numeric until a table of observed ones is worth adding.
        const char* d3d12_category_name(uint8_t category)
        {
            switch (static_cast<D3D12_MESSAGE_CATEGORY>(category)) {
                case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:
                    return "APPLICATION_DEFINED";
                case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS:
                    return "MISCELLANEOUS";
                case D3D12_MESSAGE_CATEGORY_INITIALIZATION:
                    return "INITIALIZATION";
                case D3D12_MESSAGE_CATEGORY_CLEANUP:
                    return "CLEANUP";
                case D3D12_MESSAGE_CATEGORY_COMPILATION:
                    return "COMPILATION";
                case D3D12_MESSAGE_CATEGORY_STATE_CREATION:
                    return "STATE_CREATION";
                case D3D12_MESSAGE_CATEGORY_STATE_SETTING:
                    return "STATE_SETTING";
                case D3D12_MESSAGE_CATEGORY_STATE_GETTING:
                    return "STATE_GETTING";
                case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:
                    return "RESOURCE_MANIPULATION";
                case D3D12_MESSAGE_CATEGORY_EXECUTION:
                    return "EXECUTION";
                case D3D12_MESSAGE_CATEGORY_SHADER:
                    return "SHADER";
            }
            return "UNKNOWN";
        }

        std::string format_entry(const wz::diag::DiagnosticAggregate::Entry& e)
        {
            // D3D12 records get the category name + the pass label the producer
            // stamped (kDebugPassMain / kDebugPassOffscreen). A record from another
            // source sharing this lane (future engine-detected GPU errors) falls
            // back to a plain form rather than mislabelling it as D3D12.
            std::string line;
            if (e.source == wz::diag::DiagnosticSource::D3D12) {
                line = "D3D12 ";
                line += d3d12_category_name(e.category);
                line += " #";
                line += std::to_string(e.id);
                line += " x";
                line += std::to_string(e.count);  // the EXACT running total (#291)
                line +=
                    e.last_pass == wz::gpu::dx12::internal::kDebugPassOffscreen
                        ? " [offscreen pass]"
                        : " [main pass]";
            }
            else {
                line = "gpu diagnostic #";
                line += std::to_string(e.id);
                line += " x";
                line += std::to_string(e.count);
                line += " [pass ";
                line += std::to_string(e.last_pass);
                line += "]";
            }
            return line;
        }
    }

    wz::diag::DiagnosticReporter make_debug_layer_reporter(wz::Logger& logger)
    {
        return [&logger, last_dropped = uint64_t{0}](
                   wz::diag::DiagnosticAggregate& state) mutable {
            wz::diag::report_diagnostics(state, logger, format_entry, last_dropped);
        };
    }
}
