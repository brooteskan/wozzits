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

    EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit);

    // Stop the engine's viewport runtime (if any) and start a fresh one for the
    // current project - used to reopen the viewport after its window was closed.
    void RestartRuntime();
}
