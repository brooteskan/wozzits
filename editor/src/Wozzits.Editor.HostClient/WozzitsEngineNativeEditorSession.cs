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

    // True only while the in-process viewport runtime is actually alive (polls
    // the engine, so a closed viewport window reads false). The scene-tree edit
    // path gates on this before calling the runtime-only mutation ABIs.
    public bool IsRuntimeRunning => _runtime is { } runtime && runtime.IsRunning;

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

    public EngineActuatorCatalogResponse LoadActuatorCatalog()
    {
        // Device-free (no live session needed), but PROJECT-aware: pass the project
        // directory so the catalog also lists actuators this project's behavior DLLs
        // registered, not just the built-ins -- that's what makes a project actuator
        // bindable in a chart's `call` picker.
        return _client.LoadActuatorCatalog(_projectDirectory);
    }

    public EngineBehaviorModuleCatalogResponse LoadBehaviorModuleCatalog()
    {
        // Device-free, but PROJECT-aware: pass the project directory so the catalog also
        // lists the config params this project's own behavior DLLs declare, not just the
        // built-ins -- that's what lets the inspector render typed fields for them.
        return _client.LoadBehaviorModuleCatalog(_projectDirectory);
    }

    public EngineGlbSceneHierarchy ImportGlbSceneHierarchy(
        string glbPath,
        uint sceneIndex)
    {
        // Read-only GLB import is device-free and does not need a live native
        // session. `glbPath` is the authored source_path verbatim; the engine
        // roots it against the project directory (the editor's resource root)
        // using its file-carrier convention, so no resolution happens here.
        return _client.ImportGlbSceneHierarchy(
            glbPath,
            _projectDirectory,
            sceneIndex);
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

    public void SetFrameProfiling(bool enabled)
    {
        _runtime?.SetFrameProfiling(enabled);
    }

    public void SetSimulationPaused(bool paused)
    {
        _runtime?.SetPaused(paused);
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

    public EngineMutationResponse ReorderNode(string nodeId, string beforeNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.ReorderNode(runtime.Handle, nodeId, beforeNodeId);
    }

    public EngineMutationResponse SetNodeRenderOrder(string nodeId, int renderOrder)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeRenderOrder(runtime.Handle, nodeId, renderOrder);
    }

    public EngineMutationResponse SaveScene()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SaveScene(runtime.Handle);
    }

    public EngineMutationResponse ExportSubtreeAsScene(
        string rootNodeId,
        string outPath)
    {
        // Exporting is an explicit user action that is meaningless without a live
        // engine (the subtree lives in the running scene), so surface that rather
        // than silently succeeding — mirrors ReloadBehaviorModules.
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return WozzitsEngineNativeClient.InvalidMutation(
                "Engine viewport is not running; cannot export subtree.");
        }
        return _client.ExportSubtreeAsScene(runtime.Handle, rootNodeId, outPath);
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

    public IReadOnlyList<SceneletInfo> GetSceneletCatalog()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return [];
        }
        return _client.GetSceneletCatalog(runtime.Handle);
    }

    public EngineMutationResponse OpenScene(string scenePath)
    {
        // Opening a scenelet (or switching back to the main scene) is meaningless
        // without a live engine -- surface that, like ExportSubtreeAsScene.
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return WozzitsEngineNativeClient.InvalidMutation(
                "Engine viewport is not running; cannot open scene.");
        }
        return _client.OpenScene(runtime.Handle, scenePath);
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

    public EngineSceneSnapshot LoadGraftedSceneNodes()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineSceneSnapshot();
        }
        return _client.LoadRuntimeGraftedSceneNodes(runtime.Handle);
    }

    public EngineSceneSnapshotResponse LoadRuntimeSceneSnapshot()
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineSceneSnapshotResponse();
        }
        return _client.LoadRuntimeSceneSnapshot(runtime.Handle);
    }

    public EngineMutationResponse SetSceneNodeCamera(
        string nodeId,
        EngineSceneCameraEdit edit)
    {
        // Route through the live runtime seam like every other component edit
        // (transform, collision, motion, motion filter). The old file path
        // (_client.SetSceneNodeCamera) hit an engine stub that discarded the edit,
        // so camera changes never applied or saved.
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetRuntimeSceneNodeCamera(runtime.Handle, nodeId, edit);
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

    public EngineMutationResponse SetNodeAudioRenderable(
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeAudioRenderable(runtime.Handle, nodeId, assetGraphNodeId);
    }

    public EngineMutationResponse SetNodeAudioSourcePlay(
        string nodeId,
        bool autoPlay,
        bool enabled)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeAudioSourcePlay(
            runtime.Handle, nodeId, autoPlay, enabled);
    }

    public EngineMutationResponse SetNodeGeometryAsset(
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeGeometryAsset(runtime.Handle, nodeId, assetGraphNodeId);
    }

    public EngineMutationResponse SetNodeRenderableBinding(
        string nodeId,
        string semantic,
        ulong assetGraphNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeRenderableBinding(
            runtime.Handle, nodeId, semantic, assetGraphNodeId);
    }

    public EngineMutationResponse SetNodeRenderableParam(
        string nodeId,
        string name,
        float[]? valueXyzw)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeRenderableParam(
            runtime.Handle, nodeId, name, valueXyzw);
    }

    public EngineMutationResponse SetNodeRenderProgram(
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeRenderProgram(runtime.Handle, nodeId, assetGraphNodeId);
    }

    public EngineMutationResponse SetNodeCollision(
        string nodeId,
        uint assetGraphNodeId,
        bool constrainMovement)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeCollision(
            runtime.Handle, nodeId, assetGraphNodeId, constrainMovement);
    }

    public EngineMutationResponse SetNodeAtmosphere(
        string nodeId,
        ulong atmosphereAssetNodeId,
        bool enabled)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeAtmosphere(
            runtime.Handle, nodeId, atmosphereAssetNodeId, enabled);
    }

    public EngineMutationResponse SetNodeEnvironment(
        string nodeId,
        ulong environmentAssetNodeId,
        bool enabled)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeEnvironment(
            runtime.Handle, nodeId, environmentAssetNodeId, enabled);
    }

    public EngineMutationResponse SetNodeMotionTerrain(
        string nodeId,
        bool terrainConstrained,
        float rideHeight,
        float footprintRadius,
        bool alignToSurface,
        float alignmentStrength)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeMotionTerrain(
            runtime.Handle,
            nodeId,
            terrainConstrained,
            rideHeight,
            footprintRadius,
            alignToSurface,
            alignmentStrength);
    }

    public EngineMutationResponse SetNodeMotionFilter(
        string nodeId,
        EngineSceneNodeMotionFilter filter)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeMotionFilter(runtime.Handle, nodeId, filter);
    }

    public EngineMutationResponse SetNodeSceneSource(
        string nodeId,
        ulong assetGraphNodeId,
        uint consumeMode)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeSceneSource(
            runtime.Handle, nodeId, assetGraphNodeId, consumeMode);
    }

    public EngineMutationResponse SetNodeGlbSceneSource(
        string nodeId,
        string glbPath,
        uint sceneIndex,
        uint consumeMode)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeGlbSceneSource(
            runtime.Handle, nodeId, glbPath, sceneIndex, consumeMode);
    }

    public EngineMutationResponse SetNodeGlbComponentStyle(
        string nodeId,
        bool targetBase,
        uint meshIndex,
        bool surfaceEnabled,
        float[]? surfaceRgba,
        bool wireframeEnabled,
        float[]? wireframeRgba)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.SetNodeGlbComponentStyle(
            runtime.Handle,
            nodeId,
            targetBase,
            meshIndex,
            surfaceEnabled,
            surfaceRgba,
            wireframeEnabled,
            wireframeRgba);
    }

    public EngineMutationResponse ClearNodeGlbComponentStyle(
        string nodeId,
        uint meshIndex)
    {
        if (_runtime is not { } runtime || !runtime.IsRunning)
        {
            return new EngineMutationResponse { Ok = true };
        }
        return _client.ClearNodeGlbComponentStyle(runtime.Handle, nodeId, meshIndex);
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
