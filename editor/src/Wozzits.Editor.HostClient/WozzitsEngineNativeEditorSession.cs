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

    public EngineMutationResponse SetSceneNodeTransformLive(
        string nodeId,
        EngineSceneTransformEdit edit)
    {
        // Live preview only: post to the running in-process viewport engine
        // (no disk write). No-op success when there is no live viewport.
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetRuntimeSceneNodeTransform(runtime.Handle, nodeId, edit);
    }

    public EngineMutationResponse SetSceneNodePropertiesLive(
        string nodeId,
        string name,
        bool visible)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetRuntimeSceneNodeProperties(
            runtime.Handle, nodeId, name, visible);
    }

    public EngineMutationResponse ReparentNode(string nodeId, string newParentId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.ReparentNode(runtime.Handle, nodeId, newParentId);
    }

    public EngineMutationResponse RemoveNode(string nodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.RemoveNode(runtime.Handle, nodeId);
    }

    public EngineMutationResponse SaveScene()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SaveScene(runtime.Handle);
    }

    public EngineMutationResponse ReloadBehaviorModules()
    {
        // Unlike the coalesced live edits, reloading is an explicit user action
        // that is meaningless without a live engine, so surface that rather than
        // silently succeeding.
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return WozzitsEngineNativeClient.InvalidMutation(
                "Engine viewport is not running; cannot reload behavior modules.");
        }
        return _client.ReloadBehaviorModules(runtime.Handle);
    }

    public IReadOnlyList<string> GetBehaviorModuleCatalog()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return [];
        }
        return _client.GetBehaviorModuleCatalog(runtime.Handle);
    }

    public EngineAddSceneNodeResponse AddChildNode(string parentId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineAddSceneNodeResponse
            {
                Ok = false,
                Error = "Engine viewport is not running.",
            };
        }
        return _client.AddChildNode(runtime.Handle, parentId);
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

    public EngineAddSceneNodeResponse AddNodeBehavior(string nodeId, string module)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineAddSceneNodeResponse
            {
                Ok = false,
                Error = "Engine viewport is not running.",
            };
        }
        return _client.AddNodeBehavior(runtime.Handle, nodeId, module);
    }

    public EngineMutationResponse RemoveNodeBehavior(string nodeId, string bindingId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.RemoveNodeBehavior(runtime.Handle, nodeId, bindingId);
    }

    public EngineMutationResponse SetNodeBehaviorEnabled(
        string nodeId,
        string bindingId,
        bool enabled)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeBehaviorEnabled(
            runtime.Handle, nodeId, bindingId, enabled);
    }

    public EngineMutationResponse SetNodeBehaviorFields(
        string nodeId,
        string bindingId,
        string label,
        string module)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeBehaviorFields(
            runtime.Handle, nodeId, bindingId, label, module);
    }

    public EngineMutationResponse SetNodeBehaviorEvents(
        string nodeId,
        string bindingId,
        string events)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeBehaviorEvents(
            runtime.Handle, nodeId, bindingId, events);
    }

    public EngineMutationResponse SetNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key,
        string kind,
        string value)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeBehaviorConfig(
            runtime.Handle, nodeId, bindingId, key, kind, value);
    }

    public EngineMutationResponse ClearNodeBehaviorConfig(
        string nodeId,
        string bindingId,
        string key)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.ClearNodeBehaviorConfig(
            runtime.Handle, nodeId, bindingId, key);
    }

    public EngineMutationResponse AddNodeComponent(string nodeId, string kind)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.AddNodeComponent(runtime.Handle, nodeId, kind);
    }

    public EngineMutationResponse RemoveNodeComponent(string nodeId, string kind)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.RemoveNodeComponent(runtime.Handle, nodeId, kind);
    }

    public EngineMutationResponse SetNodeRenderableAsset(
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeRenderableAsset(runtime.Handle, nodeId, assetGraphNodeId);
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
