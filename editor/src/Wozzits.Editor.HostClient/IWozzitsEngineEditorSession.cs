using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public interface IWozzitsEngineEditorSession
{
    EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot();

    // Device-free authoring catalog of asset-graph node types (project- and
    // session-independent), excluding types not yet migrated to wozzits-rhi.
    EngineAssetCatalogResponse LoadAssetCatalog();

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

    // Add a child node under parentId (empty => top level) in the running scene
    // and return the engine-minted id. Errors if no viewport is running.
    EngineAddSceneNodeResponse AddChildNode(string parentId);

    EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit);

    // Stop the engine's viewport runtime (if any) and start a fresh one for the
    // current project - used to reopen the viewport after its window was closed.
    void RestartRuntime();
}
