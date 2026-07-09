#pragma once

// engine/app/scene_change.h
//
// SceneChange — the descriptor that decouples a scene-document MUTATION from the
// host REACTION it requires (#258 avenue 2, the #183 "editing model into the
// engine" linchpin). Every edit verb splits into two halves: it mutates the
// authored document (scene_nodes_ today, a SceneDocument later) and returns a
// SceneChange saying WHAT changed; the host then maps that to the right reaction
// (rebuild the behavior runtime, rematerialize render bindings, patch a live
// component record, or nothing). Getting this seam right is what lets the pure
// document logic move into the engine while the reactions — which touch the
// behavior runtime / renderer / bound graph the host owns — stay with the host.
//
// The kinds are being introduced incrementally as verbs are converted; a verb
// still doing its reaction inline simply does not emit a SceneChange yet.

#include <source_location>
#include <string>
#include <utility>

namespace wz::app
{
    enum class SceneChangeKind
    {
        // Pure document edit: the renderer and behavior runtime read the authored
        // fields fresh next frame, so no reaction is needed.
        None,

        // The entity set or hierarchy changed (add / remove / reparent / reorder /
        // render_order). The behavior runtime's entity ids are invalidated, so it
        // must be re-materialized WHEN ONE IS LIVE (a behavior-less scene has no
        // runtime to rebuild).
        Structural,

        // A binding or sim component that must be materialized into the runtime
        // changed (a behavior binding add/remove/enable/fields/events/config, or a
        // Motion component being ADDED). Like Structural, but re-materializes
        // UNCONDITIONALLY: adding the first such binding/component to a scene that
        // had no runtime must CREATE it. (The natural hook for #257 incremental
        // behavior rebuild.)
        RuntimeRebuild,

        // A renderable's assembly recipe changed (geometry / render-program /
        // semantic binding). The render bindings must be re-materialized so the
        // renderer draws the new form.
        RenderBinding,

        // ONE node's renderable recipe changed (the custom-form flip on the first
        // / last renderable-constant override). Re-assemble just that node's
        // binding, not the whole scene (#253). Carries the node id.
        RenderBindingNode,

        // A collision reference changed: re-bridge the scene's collision keys
        // against the bound graph, then re-materialize the runtime (unconditional,
        // so the runtime's collision world picks up the constraint surface).
        Collision,
    };

    struct SceneChange
    {
        SceneChangeKind kind = SceneChangeKind::None;

        // For RenderBinding: the edit-verb call site, threaded to the
        // rematerialize's #252 caller log so the per-frame profile still names
        // WHICH edit forced the re-materialize (unused for the other kinds).
        std::source_location caller = std::source_location::current();

        // For RenderBindingNode: the single node whose recipe changed.
        std::string node_id;

        static SceneChange none()        { return { SceneChangeKind::None }; }
        static SceneChange structural()  { return { SceneChangeKind::Structural }; }
        static SceneChange runtime_rebuild()
        {
            return { SceneChangeKind::RuntimeRebuild };
        }
        static SceneChange collision() { return { SceneChangeKind::Collision }; }
        // Default arg captures the VERB's call site (default args evaluate at the
        // caller), so the #252 log names the verb, not this factory.
        static SceneChange render_binding(
            std::source_location caller = std::source_location::current())
        {
            return { SceneChangeKind::RenderBinding, caller };
        }
        static SceneChange render_binding_node(std::string node_id)
        {
            SceneChange change;
            change.kind = SceneChangeKind::RenderBindingNode;
            change.node_id = std::move(node_id);
            return change;
        }
    };
}
