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

        // Names for the D3D12 message ids we actually see (#291): the benign per-
        // frame repeaters this channel exists to summarize, plus the common draw /
        // resource-state validation ids. Matched by ENUM CONSTANT, not raw number,
        // so it is robust across SDK versions -- the record carries message->ID's
        // numeric value, which equals the constant's value. There are ~1300 ids;
        // this is the useful subset. Unknown ids stay numeric (the category still
        // classifies them) -- add a case as more show up in real sessions.
        const char* d3d12_message_name(uint32_t id)
        {
            switch (static_cast<D3D12_MESSAGE_ID>(id)) {
                // The benign per-frame repeaters (a clear whose colour differs from
                // the resource's optimized clear value) -- #291's 1584-line spam.
                case D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE:
                    return "ClearRenderTargetView mismatching clear value";
                case D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE:
                    return "ClearDepthStencilView mismatching clear value";
                // Draw-time validation -- a misconfigured draw (the shape of #615,
                // the null-DSV EXECUTION error this channel was built to catch).
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET:
                    return "Draw: vertex buffer not set";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_INDEX_BUFFER_NOT_SET:
                    return "Draw: index buffer not set";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_ROOT_SIGNATURE_NOT_SET:
                    return "Draw: root signature not set";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_ROOT_SIGNATURE_MISMATCH:
                    return "Draw: root signature mismatch";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_RENDER_TARGET_DELETED:
                    return "Draw: render target deleted";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_INVALID_PRIMITIVETOPOLOGY:
                    return "Draw: invalid primitive topology";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_VERTEX_BUFFER_TOO_SMALL:
                    return "Draw: vertex buffer too small";
                case D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_INDEX_BUFFER_TOO_SMALL:
                    return "Draw: index buffer too small";
                // Resource state / barriers -- the common state-tracking mistakes.
                case D3D12_MESSAGE_ID_INVALID_SUBRESOURCE_STATE:
                    return "Invalid subresource state";
                case D3D12_MESSAGE_ID_RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH:
                    return "Resource barrier before/after state mismatch";
                case D3D12_MESSAGE_ID_RESOURCE_BARRIER_MATCHING_STATES:
                    return "Resource barrier with matching before/after states";
                default:
                    return nullptr;  // unknown id -- the category classifies it
            }
        }

        std::string format_entry(const wz::diag::DiagnosticAggregate::Entry& e)
        {
            // D3D12 records get the message NAME if we know the id, else the message
            // category, plus the pass label the producer stamped (kDebugPassMain /
            // kDebugPassOffscreen). A record from another source sharing this lane
            // (future engine-detected GPU errors) falls back to a plain form rather
            // than mislabelling it as D3D12.
            std::string line;
            if (e.source == wz::diag::DiagnosticSource::D3D12) {
                line = "D3D12 ";
                if (const char* name = d3d12_message_name(e.id)) {
                    line += name;
                }
                else {
                    line += d3d12_category_name(e.category);
                }
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
        return [&logger, last_reported = wz::diag::ReportedLosses{}](
                   wz::diag::DiagnosticAggregate& state) mutable {
            wz::diag::report_diagnostics(state, logger, format_entry, last_reported);
        };
    }
}
