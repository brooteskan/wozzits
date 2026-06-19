#pragma once

// engine/assets/scene/asset_graph_json.h
//
// Loader for the "wozzits.scene_editor.assets.graph.v2" JSON format into an
// AssetGraphDraft. Lifted out of the scene editor so the runtime app
// (WozzitsApp_v1) and the editor share ONE parser rather than duplicating it.

#include <asset/draft.h>

#include <external/json/json_document.h>

#include <string>

namespace wz::engine::assets
{
    // Parse the graph document's "nodes" (with their "deps" + "meta") into the
    // draft via wz::asset::load_asset_graph_draft_from_authored. Returns false
    // and sets `error` on malformed input. Does not compile/resolve — that is
    // the caller's bind step.
    bool load_asset_graph_draft_from_v2_json(
        const wz::json::JSONValue& root,
        wz::asset::AssetGraphDraft& draft,
        std::string& error);
}
