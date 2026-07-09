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

namespace wz::app
{
    enum class SceneChangeKind
    {
        // Pure document edit: the renderer and behavior runtime read the authored
        // fields fresh next frame, so no reaction is needed.
        None,

        // The entity set or hierarchy changed (add / remove / reparent / reorder /
        // behavior binding). The behavior runtime's entity ids are invalidated, so
        // it must be re-materialized when one is live.
        Structural,
    };

    struct SceneChange
    {
        SceneChangeKind kind = SceneChangeKind::None;

        static SceneChange none()       { return { SceneChangeKind::None }; }
        static SceneChange structural() { return { SceneChangeKind::Structural }; }
    };
}
