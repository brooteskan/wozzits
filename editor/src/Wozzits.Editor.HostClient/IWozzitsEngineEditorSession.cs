using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public interface IWozzitsEngineEditorSession
{
    EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot();

    // Device-free authoring catalog of asset-graph node types (project- and
    // session-independent), excluding types not yet migrated to wozzits-rhi.
    EngineAssetCatalogResponse LoadAssetCatalog();

    // Device-free, READ-ONLY import of a GLB scene's component hierarchy (issue
    // #213 Phase 3b-1), so the inspector can show what a node's glb_scene_source
    // descriptor grafts as children. absoluteGlbPath is a resolved filesystem
    // path; returns Ok=false (with Error) when the GLB cannot be read/imported.
    EngineGlbSceneHierarchy ImportGlbSceneHierarchy(
        string absoluteGlbPath,
        uint sceneIndex);

    EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineMutationResponse DisconnectAssetGraphEdge(ulong edgeId);

    // Add a new authored node for (schema, type) to the draft; returns its id.
    EngineAddNodeResponse AddAssetGraphNode(ulong schema, uint type);

    // Remove a node (and edges touching it) from the draft.
    EngineMutationResponse RemoveAssetGraphNode(ulong nodeId);

    EngineMutationResponse SetAssetGraphNodeParamString(
        ulong nodeId,
        string name,
        string value);

    EngineMutationResponse SaveAssetGraph();

    EngineMutationResponse CommitAssetGraph();

    EngineMutationResponse CompileAssetGraph();

    EngineMutationResponse SetAssetGraphNodePosition(
        ulong nodeId,
        double x,
        double y);

    EngineMutationResponse SetAssetGraphZoom(double zoom);

    EngineMutationResponse SetSceneNodeProperties(
        string nodeId,
        string name,
        bool visible);

    EngineMutationResponse SetSceneNodeTransform(
        string nodeId,
        EngineSceneTransformEdit edit);

    // Live transform preview: push a node transform to the running viewport
    // engine (no disk write). No-op success when no viewport is running.
    EngineMutationResponse SetSceneNodeTransformLive(
        string nodeId,
        EngineSceneTransformEdit edit);

    // Live node label/visibility edit to the running scene; no-op success when
    // no viewport is running.
    EngineMutationResponse SetSceneNodePropertiesLive(
        string nodeId,
        string name,
        bool visible);

    // Reparent a node under newParentId (empty => top level) in the running
    // scene; no-op when no viewport is running. The engine re-validates.
    EngineMutationResponse ReparentNode(string nodeId, string newParentId);

    // Remove a node and its subtree from the running scene; no-op when no
    // viewport is running.
    EngineMutationResponse RemoveNode(string nodeId);

    // Persist the running scene to its source file (no-op when no viewport is
    // running). Also happens automatically when the runtime exits.
    EngineMutationResponse SaveScene();

    // Reload the project's (freshly recompiled) behavior-module DLLs into the
    // running engine without restarting it: the engine clears its behavior
    // registry, re-registers built-ins, reloads the project DLLs, and rebuilds
    // the behavior scene on its next frame. Errors when no viewport is running
    // (there is nothing to reload into). Compiling the DLLs is a separate
    // editor-side step (BehaviorModuleBuilder); this only reloads them.
    EngineMutationResponse ReloadBehaviorModules();

    // Names of the behavior modules currently registered in the running engine
    // (built-ins + loaded project DLLs) — the set a node behavior binding may
    // reference. Empty when no viewport is running. Used to offer "add a
    // behavior" from the imported modules.
    IReadOnlyList<string> GetBehaviorModuleCatalog();

    // Add a child node under parentId (empty => top level) in the running scene
    // and return the engine-minted id. Errors if no viewport is running.
    EngineAddSceneNodeResponse AddChildNode(string parentId);

    EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit);

    // Live behavior-binding authoring on the running scene (deferred, host-gated
    // engine-side; no-op success when no viewport is running). Add returns the
    // engine-minted binding id.
    EngineAddSceneNodeResponse AddNodeBehavior(string nodeId, string module);

    EngineMutationResponse RemoveNodeBehavior(string nodeId, string bindingId);

    EngineMutationResponse SetNodeBehaviorEnabled(
        string nodeId,
        string bindingId,
        bool enabled);

    EngineMutationResponse SetNodeBehaviorFields(
        string nodeId,
        string bindingId,
        string label,
        string module);

    EngineMutationResponse SetNodeBehaviorEvents(
        string nodeId,
        string bindingId,
        string events);

    EngineMutationResponse SetNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key,
        string kind,
        string value);

    EngineMutationResponse ClearNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key);

    // Live add/remove of an optional node component by kind ("camera",
    // "renderable", "proximity", "collision", "motion") on the running scene;
    // deferred + host-gated engine-side, no-op success when no viewport is
    // running.
    EngineMutationResponse AddNodeComponent(string nodeId, string kind);

    EngineMutationResponse RemoveNodeComponent(string nodeId, string kind);

    // Set (0 clears) the node's preferred asset-graph renderable by the
    // asset-graph node id it points at; live + host-gated, no-op success when no
    // viewport is running. Never touches the legacy embedded renderable slot.
    EngineMutationResponse SetNodeRenderableAsset(
        string nodeId,
        ulong assetGraphNodeId);

    // Author (empty glbPath clears) the node's GLB scene-source descriptor — the
    // asset-graph-independent route (issue #213 Phase 3a): a resource-relative GLB
    // path + scene index + consume mode (0 instance / 1 flatten). The engine
    // re-resolves it into a Scene and grafts the GLB hierarchy under the node.
    // Live + host-gated, no-op success when no viewport is running.
    EngineMutationResponse SetNodeGlbSceneSource(
        string nodeId,
        string glbPath,
        uint sceneIndex,
        uint consumeMode);

    // Stop the engine's viewport runtime (if any) and start a fresh one for the
    // current project - used to reopen the viewport after its window was closed.
    void RestartRuntime();
}
