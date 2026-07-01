using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public sealed partial class WozzitsEngineNativeClient
{
    public const string AbiPathEnvironmentVariable = "WOZZITS_ABI";

    public IWozzitsEngineEditorSession OpenEditorSession(
        string projectDirectory,
        bool startRuntime = false,
        Action<string>? logReceived = null)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                openError: "Project directory is empty.");
        }

        try
        {
            WozzitsEngineAbi.EnsureResolverRegistered();
            var result = WozzitsEngineAbi.WzEditorOpenProjectSession(
                projectDirectory,
                resourceRootUtf8: null,
                out var session);
            return result.Code == WzResultCode.Ok && session != IntPtr.Zero
                ? new WozzitsEngineNativeEditorSession(
                    this,
                    projectDirectory,
                    session,
                    startRuntime
                        ? new WozzitsEngineRuntime(projectDirectory, logReceived)
                        : null)
                : new WozzitsEngineNativeEditorSession(
                    this,
                    projectDirectory,
                    IntPtr.Zero,
                    openError: result.Message);
        }
        catch (DllNotFoundException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                openError: ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                openError: ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                openError: ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                openError: ex.Message);
        }
    }

    public EngineProjectSnapshotResponse LoadProjectSnapshot(string projectDirectory)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidProjectSnapshot("Project directory is empty.");
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorLoadProjectSnapshot(
                projectDirectory,
                resourceRootUtf8: null,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return InvalidProjectSnapshot(result.Message);
            }

            return ReadProjectSnapshot(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return InvalidProjectSnapshot(ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return InvalidProjectSnapshot(ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return InvalidProjectSnapshot(ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return InvalidProjectSnapshot(ex.Message);
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    public EngineProjectResponse CreateProject(string projectDirectory, string? name = null)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidProject("Project directory is empty.");
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorCreateProject(
                projectDirectory,
                resourceRootUtf8: null,
                nameUtf8: name,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return InvalidProject(result.Message);
            }

            return ReadProjectCreate(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return InvalidProject(ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return InvalidProject(ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return InvalidProject(ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return InvalidProject(ex.Message);
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineMutationResponse SetSceneNodeProperties(
        string projectDirectory,
        string nodeId,
        string name,
        bool visible)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidMutation("Project directory is empty.");
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSceneSetNodeProperties(
            projectDirectory,
            resourceRootUtf8: null,
            nodeId,
            name,
            visible ? 1u : 0u));
    }

    internal EngineMutationResponse SetAssetGraphNodePosition(
        string projectDirectory,
        ulong nodeId,
        double x,
        double y)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidMutation("Project directory is empty.");
        }
        if (nodeId == 0)
        {
            return InvalidMutation("Asset graph node id is invalid.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphSetNodePosition(
            projectDirectory,
            resourceRootUtf8: null,
            nodeId,
            x,
            y));
    }

    internal EngineMutationResponse SetAssetGraphZoom(
        string projectDirectory,
        double zoom)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidMutation("Project directory is empty.");
        }
        if (!double.IsFinite(zoom))
        {
            return InvalidMutation("Asset graph zoom is invalid.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphSetZoom(
            projectDirectory,
            resourceRootUtf8: null,
            zoom));
    }

    public EngineAssetCatalogResponse LoadAssetCatalog()
    {
        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorAssetCatalog(out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return new EngineAssetCatalogResponse
                {
                    Ok = false,
                    Error = result.Message,
                };
            }

            return ReadAssetCatalog(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return new EngineAssetCatalogResponse { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineAssetCatalogResponse { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineAssetCatalogResponse { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineAssetCatalogResponse { Ok = false, Error = ex.Message };
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    public EngineGlbSceneHierarchy ImportGlbSceneHierarchy(
        string glbPath,
        string? resourceRoot,
        uint sceneIndex)
    {
        if (string.IsNullOrWhiteSpace(glbPath))
        {
            return new EngineGlbSceneHierarchy
            {
                Ok = false,
                Error = "GLB path is empty.",
            };
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            buffer = WozzitsEngineAbi.WzImportGlbSceneHierarchy(
                glbPath,
                resourceRoot,
                sceneIndex);
            if (buffer.Data == IntPtr.Zero)
            {
                return new EngineGlbSceneHierarchy
                {
                    Ok = false,
                    Error = "Engine ABI returned an empty GLB scene hierarchy buffer.",
                };
            }

            return ReadGlbSceneHierarchy(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return new EngineGlbSceneHierarchy { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineGlbSceneHierarchy { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineGlbSceneHierarchy { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineGlbSceneHierarchy { Ok = false, Error = ex.Message };
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    // Fetch the live runtime's grafted scene nodes (issue #213) as a scene
    // snapshot. The blob is the SAME project-snapshot layout LoadProjectSnapshot
    // returns, so it is decoded with the same reader; only the scene part is
    // meaningful (its roots are instance-grafted sub-trees, each carrying its host
    // id as ParentId). A null/not-running runtime, or any failure, yields an empty
    // snapshot so the caller leaves its JSON tree untouched.
    internal EngineSceneSnapshot LoadRuntimeGraftedSceneNodes(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineSceneSnapshot();
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            buffer = WozzitsEngineAbi.WzEditorRuntimeGraftedSceneSnapshot(runtime);
            if (buffer.Data == IntPtr.Zero)
            {
                return new EngineSceneSnapshot();
            }

            return ReadProjectSnapshot(buffer).Scene.Snapshot;
        }
        catch (DllNotFoundException)
        {
            return new EngineSceneSnapshot();
        }
        catch (EntryPointNotFoundException)
        {
            return new EngineSceneSnapshot();
        }
        catch (BadImageFormatException)
        {
            return new EngineSceneSnapshot();
        }
        catch (InvalidOperationException)
        {
            return new EngineSceneSnapshot();
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    // The running scene's authored nodes as a full scene snapshot (the SAME blob
    // the project snapshot uses), so the editor rebuilds its tree after OpenScene
    // swaps the working scene. Empty response when no viewport is running.
    internal EngineSceneSnapshotResponse LoadRuntimeSceneSnapshot(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineSceneSnapshotResponse();
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            buffer = WozzitsEngineAbi.WzEditorRuntimeSceneSnapshot(runtime);
            if (buffer.Data == IntPtr.Zero)
            {
                return new EngineSceneSnapshotResponse();
            }

            return ReadProjectSnapshot(buffer).Scene;
        }
        catch (DllNotFoundException)
        {
            return new EngineSceneSnapshotResponse();
        }
        catch (EntryPointNotFoundException)
        {
            return new EngineSceneSnapshotResponse();
        }
        catch (BadImageFormatException)
        {
            return new EngineSceneSnapshotResponse();
        }
        catch (InvalidOperationException)
        {
            return new EngineSceneSnapshotResponse();
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot(IntPtr session)
    {
        if (session == IntPtr.Zero)
        {
            return new EngineAssetGraphSnapshotResponse
            {
                Ok = false,
                Error = "Engine editor session is closed.",
            };
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorSessionAssetGraphSnapshot(
                session,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return new EngineAssetGraphSnapshotResponse
                {
                    Ok = false,
                    Error = result.Message,
                };
            }

            return ReadAssetGraphSnapshot(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return new EngineAssetGraphSnapshotResponse { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineAssetGraphSnapshotResponse { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineAssetGraphSnapshotResponse { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineAssetGraphSnapshotResponse { Ok = false, Error = ex.Message };
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidConnectionCheck("Engine editor session is closed.");
        }

        return InvokeConnectionCheck(() =>
        {
            var result = WozzitsEngineAbi.WzEditorAssetGraphCanConnect(
                session,
                fromNodeId,
                toNodeId,
                toInputPort,
                out var buffer);
            return (result, buffer);
        });
    }

    internal EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidConnectionCheck("Engine editor session is closed.");
        }

        return InvokeConnectionCheck(() =>
        {
            var result = WozzitsEngineAbi.WzEditorAssetGraphConnect(
                session,
                fromNodeId,
                toNodeId,
                toInputPort,
                out var buffer);
            return (result, buffer);
        });
    }

    internal EngineMutationResponse DisconnectAssetGraphEdge(IntPtr session, ulong edgeId)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        if (edgeId == 0)
        {
            return InvalidMutation("Asset graph edge id is invalid.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphDisconnectEdge(
            session,
            edgeId));
    }

    internal EngineAddNodeResponse AddAssetGraphNode(
        IntPtr session,
        ulong schema,
        uint type)
    {
        if (session == IntPtr.Zero)
        {
            return new EngineAddNodeResponse
            {
                Ok = false,
                Error = "Engine editor session is closed.",
            };
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        try
        {
            var result = WozzitsEngineAbi.WzEditorAssetGraphAddNode(
                session,
                schema,
                type,
                out var nodeId);
            if (result.Code != WzResultCode.Ok)
            {
                return new EngineAddNodeResponse { Ok = false, Error = result.Message };
            }

            return new EngineAddNodeResponse { Ok = true, NodeId = nodeId };
        }
        catch (DllNotFoundException ex)
        {
            return new EngineAddNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineAddNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineAddNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineAddNodeResponse { Ok = false, Error = ex.Message };
        }
    }

    internal EngineMutationResponse RemoveAssetGraphNode(IntPtr session, ulong nodeId)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphRemoveNode(
            session,
            nodeId));
    }

    internal EngineMutationResponse SetSessionNodePosition(
        IntPtr session,
        ulong nodeId,
        double x,
        double y)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSessionSetNodePosition(
            session,
            nodeId,
            x,
            y));
    }

    internal EngineMutationResponse SetSessionZoom(IntPtr session, double zoom)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSessionSetZoom(
            session,
            zoom));
    }

    internal EngineMutationResponse SetAssetGraphNodeParamString(
        IntPtr session,
        ulong nodeId,
        string name,
        string value)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        if (string.IsNullOrEmpty(name))
        {
            return InvalidMutation("Asset graph param name is empty.");
        }

        return InvokeMutation(() =>
            WozzitsEngineAbi.WzEditorAssetGraphSetNodeParamString(
                session,
                nodeId,
                name,
                value ?? string.Empty));
    }

    internal EngineMutationResponse SaveAssetGraph(IntPtr session)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSessionSave(session));
    }

    internal EngineMutationResponse BindDraft(IntPtr runtime, IntPtr session)
    {
        if (runtime == IntPtr.Zero)
        {
            return InvalidMutation("Engine runtime is not available.");
        }
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeBindDraft(
            runtime,
            session));
    }

    internal EngineAddSceneNodeResponse AddChildNode(IntPtr runtime, string parentId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineAddSceneNodeResponse
            {
                Ok = false,
                Error = "Engine runtime is not available.",
            };
        }

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorRuntimeAddChildNode(
                runtime,
                parentId ?? string.Empty,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return new EngineAddSceneNodeResponse
                {
                    Ok = false,
                    Error = result.Message,
                };
            }

            var newId = buffer.Data != IntPtr.Zero && buffer.Size != 0
                ? System.Runtime.InteropServices.Marshal.PtrToStringUTF8(
                    buffer.Data,
                    checked((int)buffer.Size)) ?? string.Empty
                : string.Empty;
            return new EngineAddSceneNodeResponse { Ok = true, NodeId = newId };
        }
        catch (DllNotFoundException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineAddSceneNodeResponse AddNodeBehavior(
        IntPtr runtime,
        string nodeId,
        string module)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineAddSceneNodeResponse
            {
                Ok = false,
                Error = "Engine runtime is not available.",
            };
        }

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorRuntimeAddNodeBehavior(
                runtime,
                nodeId ?? string.Empty,
                module ?? string.Empty,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return new EngineAddSceneNodeResponse
                {
                    Ok = false,
                    Error = result.Message,
                };
            }

            var bindingId = buffer.Data != IntPtr.Zero && buffer.Size != 0
                ? System.Runtime.InteropServices.Marshal.PtrToStringUTF8(
                    buffer.Data,
                    checked((int)buffer.Size)) ?? string.Empty
                : string.Empty;
            return new EngineAddSceneNodeResponse { Ok = true, NodeId = bindingId };
        }
        catch (DllNotFoundException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (EntryPointNotFoundException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (BadImageFormatException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        catch (InvalidOperationException ex)
        {
            return new EngineAddSceneNodeResponse { Ok = false, Error = ex.Message };
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineMutationResponse RemoveNodeBehavior(
        IntPtr runtime,
        string nodeId,
        string bindingId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeRemoveNodeBehavior(
            runtime,
            nodeId,
            bindingId));
    }

    internal EngineMutationResponse SetNodeBehaviorEnabled(
        IntPtr runtime,
        string nodeId,
        string bindingId,
        bool enabled)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeBehaviorEnabled(
            runtime,
            nodeId,
            bindingId,
            enabled ? 1u : 0u));
    }

    internal EngineMutationResponse SetNodeBehaviorFields(
        IntPtr runtime,
        string nodeId,
        string bindingId,
        string label,
        string module)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeBehaviorFields(
            runtime,
            nodeId,
            bindingId,
            label ?? string.Empty,
            module ?? string.Empty));
    }

    internal EngineMutationResponse SetNodeBehaviorEvents(
        IntPtr runtime,
        string nodeId,
        string bindingId,
        string events)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeBehaviorEvents(
            runtime,
            nodeId,
            bindingId,
            events ?? string.Empty));
    }

    internal EngineMutationResponse SetNodeBehaviorConfig(
        IntPtr runtime,
        string nodeId,
        string bindingId,
        string key,
        string kind,
        string value)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }
        if (string.IsNullOrWhiteSpace(key))
        {
            return InvalidMutation("Behavior config key is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeBehaviorConfig(
            runtime,
            nodeId,
            bindingId,
            key,
            kind ?? string.Empty,
            value ?? string.Empty));
    }

    internal EngineMutationResponse ClearNodeBehaviorConfig(
        IntPtr runtime,
        string nodeId,
        string bindingId,
        string key)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId)
            || string.IsNullOrWhiteSpace(bindingId))
        {
            return InvalidMutation("Scene node id or behavior binding id is empty.");
        }
        if (string.IsNullOrWhiteSpace(key))
        {
            return InvalidMutation("Behavior config key is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeClearNodeBehaviorConfig(
            runtime,
            nodeId,
            bindingId,
            key));
    }

    internal EngineMutationResponse AddNodeComponent(
        IntPtr runtime,
        string nodeId,
        string kind)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }
        if (string.IsNullOrWhiteSpace(kind))
        {
            return InvalidMutation("Component kind is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeAddNodeComponent(
            runtime,
            nodeId,
            kind));
    }

    internal EngineMutationResponse RemoveNodeComponent(
        IntPtr runtime,
        string nodeId,
        string kind)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }
        if (string.IsNullOrWhiteSpace(kind))
        {
            return InvalidMutation("Component kind is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeRemoveNodeComponent(
            runtime,
            nodeId,
            kind));
    }

    internal EngineMutationResponse SetNodeRenderableAsset(
        IntPtr runtime,
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeRenderableAsset(
            runtime,
            nodeId,
            assetGraphNodeId));
    }

    internal EngineMutationResponse SetNodeAudioRenderable(
        IntPtr runtime,
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // assetGraphNodeId 0 clears the AudioSource's renderable reference (the
        // component remains), so it is valid here.
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeAudioRenderable(
            runtime,
            nodeId,
            assetGraphNodeId));
    }

    internal EngineMutationResponse SetNodeAudioSourcePlay(
        IntPtr runtime,
        string nodeId,
        bool autoPlay,
        bool enabled)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeAudioSourcePlay(
            runtime,
            nodeId,
            autoPlay ? (byte)1 : (byte)0,
            enabled ? (byte)1 : (byte)0));
    }

    internal EngineMutationResponse SetNodeGeometryAsset(
        IntPtr runtime,
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // assetGraphNodeId 0 is the CLEAR signal (the engine drops the geometry
        // ingredient and any assembled renderable), so it is valid here.
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeGeometryAsset(
            runtime,
            nodeId,
            assetGraphNodeId));
    }

    internal EngineMutationResponse SetNodeRenderProgram(
        IntPtr runtime,
        string nodeId,
        ulong assetGraphNodeId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // assetGraphNodeId 0 clears the program ingredient; descendants then fall
        // back to their nearest ancestor's program (or stop drawing if none).
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeRenderProgram(
            runtime,
            nodeId,
            assetGraphNodeId));
    }

    internal EngineMutationResponse SetNodeCollision(
        IntPtr runtime,
        string nodeId,
        uint assetGraphNodeId,
        bool constrainMovement)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // assetGraphNodeId 0 clears the collision asset reference; the constrain
        // flag is independent and applies regardless, so both forms are valid.
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeCollision(
            runtime,
            nodeId,
            assetGraphNodeId,
            constrainMovement ? (byte)1 : (byte)0));
    }

    internal EngineMutationResponse SetNodeMotionTerrain(
        IntPtr runtime,
        string nodeId,
        bool terrainConstrained,
        float rideHeight,
        float footprintRadius,
        bool alignToSurface,
        float alignmentStrength)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeMotionTerrain(
            runtime,
            nodeId,
            terrainConstrained ? (byte)1 : (byte)0,
            rideHeight,
            footprintRadius,
            alignToSurface ? (byte)1 : (byte)0,
            alignmentStrength));
    }

    internal EngineMutationResponse SetNodeSceneSource(
        IntPtr runtime,
        string nodeId,
        ulong assetGraphNodeId,
        uint consumeMode)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // assetGraphNodeId 0 is the CLEAR signal (the engine drops the reference),
        // so it is valid here — only the node id is required.
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeSceneSource(
            runtime,
            nodeId,
            assetGraphNodeId,
            consumeMode));
    }

    internal EngineMutationResponse SetNodeGlbSceneSource(
        IntPtr runtime,
        string nodeId,
        string glbPath,
        uint sceneIndex,
        uint consumeMode)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        // An empty/whitespace path is the CLEAR signal (the engine drops the
        // descriptor), so it is valid here — only the node id is required.
        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeGlbSceneSource(
            runtime,
            nodeId,
            glbPath ?? string.Empty,
            sceneIndex,
            consumeMode));
    }

    internal EngineMutationResponse SetNodeGlbComponentStyle(
        IntPtr runtime,
        string nodeId,
        bool targetBase,
        uint meshIndex,
        bool surfaceEnabled,
        float[]? surfaceRgba,
        bool wireframeEnabled,
        float[]? wireframeRgba)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() =>
            WozzitsEngineAbi.WzEditorRuntimeSetNodeGlbComponentStyle(
                runtime,
                nodeId,
                targetBase ? 1u : 0u,
                meshIndex,
                surfaceEnabled ? 1u : 0u,
                surfaceRgba,
                wireframeEnabled ? 1u : 0u,
                wireframeRgba));
    }

    internal EngineMutationResponse ClearNodeGlbComponentStyle(
        IntPtr runtime,
        string nodeId,
        uint meshIndex)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() =>
            WozzitsEngineAbi.WzEditorRuntimeClearNodeGlbComponentStyle(
                runtime,
                nodeId,
                meshIndex));
    }

    internal EngineMutationResponse SetRuntimeSceneNodeProperties(
        IntPtr runtime,
        string nodeId,
        string name,
        bool visible)
    {
        if (runtime == IntPtr.Zero)
        {
            // No live viewport running — nothing to apply; not an error.
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeProperties(
            runtime,
            nodeId,
            name ?? string.Empty,
            visible ? 1u : 0u));
    }

    internal EngineMutationResponse ReparentNode(
        IntPtr runtime,
        string nodeId,
        string newParentId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };  // no live viewport
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeReparentNode(
            runtime,
            nodeId,
            newParentId ?? string.Empty));
    }

    internal EngineMutationResponse RemoveNode(IntPtr runtime, string nodeId)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };  // no live viewport
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(
            () => WozzitsEngineAbi.WzEditorRuntimeRemoveNode(runtime, nodeId));
    }

    internal EngineMutationResponse SaveScene(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return new EngineMutationResponse { Ok = true };  // no live viewport
        }

        return InvokeMutation(
            () => WozzitsEngineAbi.WzEditorRuntimeSaveScene(runtime));
    }

    internal EngineMutationResponse ExportSubtreeAsScene(
        IntPtr runtime,
        string rootNodeId,
        string outPath)
    {
        if (runtime == IntPtr.Zero)
        {
            return InvalidMutation("Engine viewport is not running.");
        }
        if (string.IsNullOrWhiteSpace(rootNodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }
        if (string.IsNullOrWhiteSpace(outPath))
        {
            return InvalidMutation("Export path is empty.");
        }

        return InvokeMutation(
            () => WozzitsEngineAbi.WzEditorExportSubtreeAsScene(
                runtime,
                rootNodeId,
                outPath));
    }

    internal EngineMutationResponse ReloadBehaviorModules(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return InvalidMutation("Engine viewport is not running.");
        }

        return InvokeMutation(
            () => WozzitsEngineAbi.WzEditorRuntimeReloadBehaviorModules(runtime));
    }

    internal EngineMutationResponse OpenScene(IntPtr runtime, string scenePath)
    {
        if (runtime == IntPtr.Zero)
        {
            return InvalidMutation("Engine viewport is not running.");
        }

        // An empty path is intentional: it reopens the project's main scene (switch
        // back from a scenelet). Coalesce null to "" for the marshaller.
        return InvokeMutation(
            () => WozzitsEngineAbi.WzEditorRuntimeOpenScene(
                runtime,
                scenePath ?? string.Empty));
    }

    internal IReadOnlyList<SceneletInfo> GetSceneletCatalog(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return [];
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorRuntimeSceneletCatalog(
                runtime,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return [];
            }

            var text = buffer.Data != IntPtr.Zero && buffer.Size != 0
                ? System.Runtime.InteropServices.Marshal.PtrToStringUTF8(
                    buffer.Data,
                    checked((int)buffer.Size)) ?? string.Empty
                : string.Empty;
            if (string.IsNullOrEmpty(text))
            {
                return [];
            }

            // Each line is "name\tpath" (see wz_host_runtime_scenelet_catalog).
            var lines = text.Split(
                '\n',
                StringSplitOptions.RemoveEmptyEntries
                    | StringSplitOptions.TrimEntries);
            var scenelets = new List<SceneletInfo>(lines.Length);
            foreach (var line in lines)
            {
                var tab = line.IndexOf('\t');
                if (tab <= 0 || tab >= line.Length - 1)
                {
                    continue;  // malformed / missing name or path
                }
                scenelets.Add(new SceneletInfo(
                    line[..tab],
                    line[(tab + 1)..]));
            }
            return scenelets;
        }
        catch (DllNotFoundException)
        {
            return [];
        }
        catch (EntryPointNotFoundException)
        {
            return [];
        }
        catch (BadImageFormatException)
        {
            return [];
        }
        catch (InvalidOperationException)
        {
            return [];
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal IReadOnlyList<string> GetBehaviorModuleCatalog(IntPtr runtime)
    {
        if (runtime == IntPtr.Zero)
        {
            return [];
        }

        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var result = WozzitsEngineAbi.WzEditorRuntimeBehaviorModuleCatalog(
                runtime,
                out buffer);
            if (result.Code != WzResultCode.Ok)
            {
                return [];
            }

            var text = buffer.Data != IntPtr.Zero && buffer.Size != 0
                ? System.Runtime.InteropServices.Marshal.PtrToStringUTF8(
                    buffer.Data,
                    checked((int)buffer.Size)) ?? string.Empty
                : string.Empty;
            return string.IsNullOrEmpty(text)
                ? []
                : text.Split(
                    '\n',
                    StringSplitOptions.RemoveEmptyEntries
                        | StringSplitOptions.TrimEntries);
        }
        catch (DllNotFoundException)
        {
            return [];
        }
        catch (EntryPointNotFoundException)
        {
            return [];
        }
        catch (BadImageFormatException)
        {
            return [];
        }
        catch (InvalidOperationException)
        {
            return [];
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    internal EngineMutationResponse SetRuntimeSceneNodeTransform(
        IntPtr runtime,
        string nodeId,
        EngineSceneTransformEdit edit)
    {
        if (runtime == IntPtr.Zero)
        {
            // No live viewport running — nothing to preview; not an error.
            return new EngineMutationResponse { Ok = true };
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }
        if (!TryParseLiveTransform(edit, out var t))
        {
            // A field is mid-edit / unparseable; skip this update quietly so
            // typing "-" or "1." doesn't spam errors. The next valid keystroke
            // posts the full transform.
            return new EngineMutationResponse { Ok = true };
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorRuntimeSetNodeTransform(
            runtime,
            nodeId,
            t.Tx, t.Ty, t.Tz,
            t.Rx, t.Ry, t.Rz,
            t.Sx, t.Sy, t.Sz));
    }

    private static bool TryParseLiveTransform(
        EngineSceneTransformEdit edit,
        out (double Tx, double Ty, double Tz,
             double Rx, double Ry, double Rz,
             double Sx, double Sy, double Sz) value)
    {
        value = default;
        if (TryParseComponent(edit.TranslationX, out var tx)
            && TryParseComponent(edit.TranslationY, out var ty)
            && TryParseComponent(edit.TranslationZ, out var tz)
            && TryParseComponent(edit.RotationX, out var rx)
            && TryParseComponent(edit.RotationY, out var ry)
            && TryParseComponent(edit.RotationZ, out var rz)
            && TryParseComponent(edit.ScaleX, out var sx)
            && TryParseComponent(edit.ScaleY, out var sy)
            && TryParseComponent(edit.ScaleZ, out var sz))
        {
            value = (tx, ty, tz, rx, ry, rz, sx, sy, sz);
            return true;
        }
        return false;
    }

    private static bool TryParseComponent(string text, out double value)
    {
        return double.TryParse(
            text,
            System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture,
            out value);
    }

    internal EngineMutationResponse SetSceneNodeTransform(
        string projectDirectory,
        string nodeId,
        EngineSceneTransformEdit edit)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidMutation("Project directory is empty.");
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSceneSetNodeTransform(
            projectDirectory,
            resourceRootUtf8: null,
            nodeId,
            edit.TranslationX,
            edit.TranslationY,
            edit.TranslationZ,
            edit.RotationX,
            edit.RotationY,
            edit.RotationZ,
            edit.ScaleX,
            edit.ScaleY,
            edit.ScaleZ));
    }

    internal EngineMutationResponse SetSceneNodeCamera(
        string projectDirectory,
        string nodeId,
        EngineSceneCameraEdit edit)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return InvalidMutation("Project directory is empty.");
        }
        if (string.IsNullOrWhiteSpace(nodeId))
        {
            return InvalidMutation("Scene node id is empty.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorSceneSetCamera(
            projectDirectory,
            resourceRootUtf8: null,
            nodeId,
            edit.FieldOfViewY,
            edit.NearPlane,
            edit.FarPlane,
            edit.Aspect));
    }

    public static string ResolveDefaultAbiPath()
    {
        var configured = Environment.GetEnvironmentVariable(AbiPathEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return configured;
        }

        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (string.Equals(directory.Name, "wozzits-editor", StringComparison.OrdinalIgnoreCase)
                && directory.Parent is not null)
            {
                return Path.Combine(
                    directory.Parent.FullName,
                    "wozzits-window-engine",
                    "build",
                    "clang-debug",
                    "wozzits_abi.dll");
            }

            directory = directory.Parent;
        }

        return "wozzits_abi.dll";
    }

    private static EngineMutationResponse InvokeMutation(Func<WzResult> invoke)
    {
        WozzitsEngineAbi.EnsureResolverRegistered();

        try
        {
            var result = invoke();
            return result.Code == WzResultCode.Ok
                ? new EngineMutationResponse { Ok = true }
                : InvalidMutation(result.Message);
        }
        catch (DllNotFoundException ex)
        {
            return InvalidMutation(ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return InvalidMutation(ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return InvalidMutation(ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return InvalidMutation(ex.Message);
        }
    }

    private static EngineAssetGraphConnectionCheckResponse InvokeConnectionCheck(
        Func<(WzResult Result, WzBuffer Buffer)> invoke)
    {
        WozzitsEngineAbi.EnsureResolverRegistered();

        WzBuffer buffer = default;
        try
        {
            var call = invoke();
            buffer = call.Buffer;
            if (call.Result.Code != WzResultCode.Ok)
            {
                return InvalidConnectionCheck(call.Result.Message);
            }

            return ReadConnectionCheck(buffer);
        }
        catch (DllNotFoundException ex)
        {
            return InvalidConnectionCheck(ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return InvalidConnectionCheck(ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return InvalidConnectionCheck(ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return InvalidConnectionCheck(ex.Message);
        }
        finally
        {
            if (buffer.Data != IntPtr.Zero)
            {
                WozzitsEngineAbi.WzFreeBuffer(ref buffer);
            }
        }
    }

    private static EngineProjectSnapshotResponse InvalidProjectSnapshot(string error)
    {
        return new EngineProjectSnapshotResponse
        {
            Ok = false,
            Status = EngineProjectStatus.Invalid,
            Error = error,
        };
    }

    private static EngineProjectResponse InvalidProject(string error)
    {
        return new EngineProjectResponse
        {
            Ok = false,
            Status = EngineProjectStatus.Invalid,
            Error = error,
        };
    }

    internal static EngineMutationResponse InvalidMutation(string error)
    {
        return new EngineMutationResponse
        {
            Ok = false,
            Error = error,
        };
    }

    internal static EngineAssetGraphConnectionCheckResponse InvalidConnectionCheck(
        string error)
    {
        return new EngineAssetGraphConnectionCheckResponse
        {
            Ok = false,
            Error = error,
            Check = new EngineAssetGraphConnectionCheck
            {
                Status = EngineAssetGraphConnectionStatus.TypeMismatch,
                Message = error,
            },
        };
    }
}
