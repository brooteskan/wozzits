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
