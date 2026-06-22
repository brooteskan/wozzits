using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public sealed class WozzitsEngineNativeEditorSession : IWozzitsEngineEditorSession, IDisposable
{
    private readonly WozzitsEngineNativeClient _client;
    private readonly string _projectDirectory;
    private readonly string _openError;
    private readonly WozzitsEngineRuntime? _runtime;
    private IntPtr _session;

    internal WozzitsEngineNativeEditorSession(
        WozzitsEngineNativeClient client,
        string projectDirectory,
        IntPtr session,
        WozzitsEngineRuntime? runtime = null,
        string openError = "")
    {
        _client = client;
        _projectDirectory = projectDirectory;
        _session = session;
        _runtime = runtime;
        _openError = openError;
    }

    ~WozzitsEngineNativeEditorSession()
    {
        Dispose();
    }

    public EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot()
    {
        return HasNativeSession(out var error)
            ? _client.LoadAssetGraphSnapshot(_session)
            : new EngineAssetGraphSnapshotResponse
            {
                Ok = false,
                Error = error,
            };
    }

    public EngineAssetCatalogResponse LoadAssetCatalog()
    {
        // Catalog is device-free and project-independent, so it does not need a
        // live native session.
        return _client.LoadAssetCatalog();
    }

    public EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort)
    {
        return HasNativeSession(out var error)
            ? _client.CanConnectAssetGraphNodes(
                _session,
                fromNodeId,
                toNodeId,
                toInputPort)
            : WozzitsEngineNativeClient.InvalidConnectionCheck(error);
    }

    public EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort)
    {
        return HasNativeSession(out var error)
            ? _client.ConnectAssetGraphNodes(
                _session,
                fromNodeId,
                toNodeId,
                toInputPort)
            : WozzitsEngineNativeClient.InvalidConnectionCheck(error);
    }

    public EngineMutationResponse DisconnectAssetGraphEdge(ulong edgeId)
    {
        return HasNativeSession(out var error)
            ? _client.DisconnectAssetGraphEdge(_session, edgeId)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineAddNodeResponse AddAssetGraphNode(ulong schema, uint type)
    {
        return HasNativeSession(out var error)
            ? _client.AddAssetGraphNode(_session, schema, type)
            : new EngineAddNodeResponse { Ok = false, Error = error };
    }

    public EngineMutationResponse RemoveAssetGraphNode(ulong nodeId)
    {
        return HasNativeSession(out var error)
            ? _client.RemoveAssetGraphNode(_session, nodeId)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse SetAssetGraphNodeParamString(
        ulong nodeId,
        string name,
        string value)
    {
        return HasNativeSession(out var error)
            ? _client.SetAssetGraphNodeParamString(_session, nodeId, name, value)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse SaveAssetGraph()
    {
        return HasNativeSession(out var error)
            ? _client.SaveAssetGraph(_session)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse CommitAssetGraph()
    {
        // Commit = persist the draft, then bind it to the running engine.
        var saved = SaveAssetGraph();
        if (!saved.Ok)
        {
            return saved;
        }
        return CompileAssetGraph();
    }

    public EngineMutationResponse CompileAssetGraph()
    {
        if (!HasNativeSession(out var error))
        {
            return WozzitsEngineNativeClient.InvalidMutation(error);
        }
        if (_runtime is null || !_runtime.IsRunning)
        {
            return WozzitsEngineNativeClient.InvalidMutation(
                "Engine runtime is not available.");
        }

        // Compile = bind the current draft to the one in-process engine.
        return _client.BindDraft(_runtime.Handle, _session);
    }

    public void RestartRuntime()
    {
        // Frees a closed/zombie runtime and starts a fresh viewport for the
        // project. No-op if this session has no runtime (started without one).
        _runtime?.Restart();
    }

    public EngineMutationResponse SetSceneNodeProperties(
        string nodeId,
        string name,
        bool visible)
    {
        return _client.SetSceneNodeProperties(
            _projectDirectory,
            nodeId,
            name,
            visible);
    }

    public EngineMutationResponse SetAssetGraphNodePosition(
        ulong nodeId,
        double x,
        double y)
    {
        return HasNativeSession(out var error)
            ? _client.SetSessionNodePosition(_session, nodeId, x, y)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse SetAssetGraphZoom(double zoom)
    {
        return HasNativeSession(out var error)
            ? _client.SetSessionZoom(_session, zoom)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse SetSceneNodeTransform(
        string nodeId,
        EngineSceneTransformEdit edit)
    {
        return _client.SetSceneNodeTransform(
            _projectDirectory,
            nodeId,
            edit);
    }

    public EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit)
    {
        return _client.SetSceneNodeCamera(
            _projectDirectory,
            nodeId,
            edit);
    }

    public void Dispose()
    {
        // Stop the engine first, then close the authoring session.
        _runtime?.Dispose();

        var session = _session;
        if (session != IntPtr.Zero)
        {
            _session = IntPtr.Zero;
            WozzitsEngineAbi.WzEditorCloseSession(session);
        }
        GC.SuppressFinalize(this);
    }

    private bool HasNativeSession(out string error)
    {
        if (_session != IntPtr.Zero)
        {
            error = string.Empty;
            return true;
        }

        error = string.IsNullOrWhiteSpace(_openError)
            ? "Engine editor session is closed."
            : _openError;
        return false;
    }
}
