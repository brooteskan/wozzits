using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public interface IWozzitsEngineEditorSession
{
    EngineAssetGraphSnapshotResponse LoadAssetGraphSnapshot();

    EngineAssetGraphConnectionCheckResponse CanConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineAssetGraphConnectionCheckResponse ConnectAssetGraphNodes(
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort);

    EngineMutationResponse DisconnectAssetGraphEdge(ulong edgeId);

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
}

public sealed class WozzitsEngineNativeEditorSession : IWozzitsEngineEditorSession, IDisposable
{
    private readonly WozzitsEngineNativeClient _client;
    private readonly string _projectDirectory;
    private readonly string _openError;
    private IntPtr _session;

    internal WozzitsEngineNativeEditorSession(
        WozzitsEngineNativeClient client,
        string projectDirectory,
        IntPtr session,
        string openError = "")
    {
        _client = client;
        _projectDirectory = projectDirectory;
        _session = session;
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

    public EngineMutationResponse CommitAssetGraph()
    {
        return HasNativeSession(out var error)
            ? _client.CommitAssetGraph(_session)
            : WozzitsEngineNativeClient.InvalidMutation(error);
    }

    public EngineMutationResponse CompileAssetGraph()
    {
        return HasNativeSession(out var error)
            ? _client.CompileAssetGraph(_session)
            : WozzitsEngineNativeClient.InvalidMutation(error);
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
        return _client.SetAssetGraphNodePosition(
            _projectDirectory,
            nodeId,
            x,
            y);
    }

    public EngineMutationResponse SetAssetGraphZoom(double zoom)
    {
        return _client.SetAssetGraphZoom(
            _projectDirectory,
            zoom);
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
        var session = _session;
        if (session == IntPtr.Zero)
        {
            GC.SuppressFinalize(this);
            return;
        }

        _session = IntPtr.Zero;
        WozzitsEngineAbi.WzEditorCloseSession(session);
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

public sealed class WozzitsEngineNativeClient
{
    public const string AbiPathEnvironmentVariable = "WOZZITS_ABI";

    public IWozzitsEngineEditorSession OpenEditorSession(string projectDirectory)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                "Project directory is empty.");
        }

        try
        {
            WozzitsEngineAbi.EnsureResolverRegistered();
            var result = WozzitsEngineAbi.WzEditorOpenProjectSession(
                projectDirectory,
                resourceRootUtf8: null,
                out var session);
            return result.Code == WzResultCode.Ok && session != IntPtr.Zero
                ? new WozzitsEngineNativeEditorSession(this, projectDirectory, session)
                : new WozzitsEngineNativeEditorSession(
                    this,
                    projectDirectory,
                    IntPtr.Zero,
                    result.Message);
        }
        catch (DllNotFoundException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                ex.Message);
        }
        catch (BadImageFormatException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                ex.Message);
        }
        catch (InvalidOperationException ex)
        {
            return new WozzitsEngineNativeEditorSession(
                this,
                projectDirectory,
                IntPtr.Zero,
                ex.Message);
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

    internal EngineMutationResponse CommitAssetGraph(IntPtr session)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphCommit(session));
    }

    internal EngineMutationResponse CompileAssetGraph(IntPtr session)
    {
        if (session == IntPtr.Zero)
        {
            return InvalidMutation("Engine editor session is closed.");
        }

        return InvokeMutation(() => WozzitsEngineAbi.WzEditorAssetGraphCompile(session));
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

    private static EngineProjectSnapshotResponse ReadProjectSnapshot(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty project snapshot buffer.");
        var snapshot = ReadStruct<WzEditorProjectSnapshotAbi>(bytes, offset: 0);
        ValidateAbiVersion(snapshot.AbiVersion);

        return new EngineProjectSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Status = ToProjectStatus(snapshot.Status),
            Error = ReadString(bytes, snapshot.Error),
            ProjectName = ReadString(bytes, snapshot.ProjectName),
            AssetGraph = ReadAssetGraphSnapshot(bytes, snapshot.AssetGraph),
            Scene = ReadSceneSnapshot(bytes, snapshot.Scene),
        };
    }

    private static EngineProjectResponse ReadProjectCreate(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty project response buffer.");
        var project = ReadStruct<WzEditorProjectCreateAbi>(bytes, offset: 0);
        ValidateAbiVersion(project.AbiVersion);

        return new EngineProjectResponse
        {
            Ok = project.Ok != 0,
            Status = ToProjectStatus(project.Status),
            Created = project.Created != 0,
            Error = ReadString(bytes, project.Error),
        };
    }

    private static EngineAssetGraphSnapshotResponse ReadAssetGraphSnapshot(WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty asset graph snapshot buffer.");
        var snapshot = ReadStruct<WzEditorAssetGraphSnapshotAbi>(bytes, offset: 0);
        return ReadAssetGraphSnapshot(bytes, snapshot);
    }

    private static EngineAssetGraphConnectionCheckResponse ReadConnectionCheck(
        WzBuffer buffer)
    {
        var bytes = ReadBufferBytes(buffer, "Engine ABI returned an empty asset graph connection check buffer.");
        var check = ReadStruct<WzEditorAssetGraphConnectionCheckAbi>(bytes, offset: 0);
        ValidateAbiVersion(check.AbiVersion);

        return new EngineAssetGraphConnectionCheckResponse
        {
            Ok = true,
            Check = new EngineAssetGraphConnectionCheck
            {
                Compatible = check.Compatible != 0,
                Status = ToConnectionStatus(check.Status),
                ReplacesExisting = check.ReplacesExisting != 0,
                From = check.From,
                To = check.To,
                ToInputPort = check.ToInputPort,
                FromType = check.FromType,
                ToType = check.ToType,
                Message = ReadString(bytes, check.Message),
                FromTypeName = ReadString(bytes, check.FromTypeName),
                ToTypeName = ReadString(bytes, check.ToTypeName),
            },
        };
    }

    private static EngineAssetGraphSnapshotResponse ReadAssetGraphSnapshot(
        byte[] bytes,
        WzEditorAssetGraphSnapshotAbi snapshot)
    {
        return new EngineAssetGraphSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Error = ReadString(bytes, snapshot.Error),
            Snapshot = new EngineAssetGraphSnapshot
            {
                Schema = ReadString(bytes, snapshot.Schema),
                Zoom = snapshot.Zoom,
                Nodes = ReadTable<WzEditorAssetGraphNodeAbi, EngineAssetGraphNode>(
                    bytes,
                    snapshot.Nodes,
                    ReadAssetGraphNode),
                Edges = ReadTable<WzEditorAssetGraphEdgeAbi, EngineAssetGraphEdge>(
                    bytes,
                    snapshot.Edges,
                    (_, edge) => new EngineAssetGraphEdge
                    {
                        Id = edge.Id,
                        From = edge.From,
                        To = edge.To,
                        ToInputPort = edge.ToInputPort,
                    }),
            },
        };
    }

    private static EngineAssetGraphNode ReadAssetGraphNode(
        byte[] bytes,
        WzEditorAssetGraphNodeAbi node)
    {
        return new EngineAssetGraphNode
        {
            Id = node.Id,
            Type = checked((int)node.Type),
            TypeName = ReadString(bytes, node.TypeName),
            Schema = ReadString(bytes, node.Schema),
            DisplayName = ReadString(bytes, node.DisplayName),
            CompileStatus = ReadString(bytes, node.CompileStatus),
            X = node.X,
            Y = node.Y,
            InputPorts = ReadTable<WzEditorAssetGraphPortAbi, EngineAssetGraphPort>(
                bytes,
                node.InputPorts,
                ReadAssetGraphPort),
            OutputPorts = ReadTable<WzEditorAssetGraphPortAbi, EngineAssetGraphPort>(
                bytes,
                node.OutputPorts,
                ReadAssetGraphPort),
        };
    }

    private static EngineAssetGraphPort ReadAssetGraphPort(
        byte[] bytes,
        WzEditorAssetGraphPortAbi port)
    {
        return new EngineAssetGraphPort
        {
            Index = port.Index,
            Type = port.Type,
            Flags = (EngineAssetGraphPortFlags)port.Flags,
            Name = ReadString(bytes, port.Name),
            Label = ReadString(bytes, port.Label),
            TypeName = ReadString(bytes, port.TypeName),
        };
    }

    private static EngineSceneSnapshotResponse ReadSceneSnapshot(
        byte[] bytes,
        WzEditorSceneSnapshotAbi snapshot)
    {
        return new EngineSceneSnapshotResponse
        {
            Ok = snapshot.Ok != 0,
            Error = ReadString(bytes, snapshot.Error),
            Snapshot = new EngineSceneSnapshot
            {
                Schema = ReadString(bytes, snapshot.Schema),
                Name = ReadString(bytes, snapshot.Name),
                Roots = ReadTable<WzEditorSceneNodeAbi, EngineSceneNode>(
                    bytes,
                    snapshot.Roots,
                    ReadSceneNode),
            },
        };
    }

    private static EngineSceneNode ReadSceneNode(
        byte[] bytes,
        WzEditorSceneNodeAbi node)
    {
        return new EngineSceneNode
        {
            Id = ReadString(bytes, node.Id),
            DisplayName = ReadString(bytes, node.DisplayName),
            ParentId = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasParent)
                ? ReadString(bytes, node.ParentId)
                : null,
            Kind = ReadString(bytes, node.Kind),
            Visible = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasVisible)
                ? HasFlag(node.Flags, WzEditorSceneNodeFlags.Visible)
                : null,
            RenderableSource = ReadSceneRenderableSource(
                bytes,
                node.RenderableSource),
            Transform = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasTransform)
                ? ReadSceneTransform(bytes, node.Transform)
                : null,
            Camera = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasCamera)
                ? ReadSceneCamera(node.Camera)
                : null,
            Renderable = HasFlag(node.Flags, WzEditorSceneNodeFlags.HasRenderable)
                ? ReadSceneRenderable(node.Flags, node.Renderable)
                : null,
            Components = ReadTable<WzEditorSceneComponentAbi, EngineSceneComponent>(
                bytes,
                node.Components,
                ReadSceneComponent),
            Children = ReadTable<WzEditorSceneNodeAbi, EngineSceneNode>(
                bytes,
                node.Children,
                ReadSceneNode),
        };
    }

    private static EngineSceneComponent ReadSceneComponent(
        byte[] bytes,
        WzEditorSceneComponentAbi component)
    {
        return new EngineSceneComponent
        {
            Kind = ReadString(bytes, component.Kind),
            DisplayName = ReadString(bytes, component.DisplayName),
        };
    }

    private static EngineSceneRenderableSource ReadSceneRenderableSource(
        byte[] bytes,
        WzEditorSceneRenderableSourceAbi source)
    {
        return new EngineSceneRenderableSource
        {
            Kind = ReadString(bytes, source.Kind),
            DisplayName = ReadString(bytes, source.DisplayName),
        };
    }

    private static EngineSceneTransform ReadSceneTransform(
        byte[] bytes,
        WzEditorSceneTransformAbi transform)
    {
        return new EngineSceneTransform
        {
            Translation =
            [
                transform.TranslationX,
                transform.TranslationY,
                transform.TranslationZ,
            ],
            RotationQuaternion =
            [
                transform.RotationQuaternionX,
                transform.RotationQuaternionY,
                transform.RotationQuaternionZ,
                transform.RotationQuaternionW,
            ],
            RotationEulerDegrees =
            [
                transform.RotationEulerDegreesX,
                transform.RotationEulerDegreesY,
                transform.RotationEulerDegreesZ,
            ],
            Scale =
            [
                transform.ScaleX,
                transform.ScaleY,
                transform.ScaleZ,
            ],
            Display = new EngineSceneTransformDisplay
            {
                TranslationX = ReadString(bytes, transform.DisplayTranslationX),
                TranslationY = ReadString(bytes, transform.DisplayTranslationY),
                TranslationZ = ReadString(bytes, transform.DisplayTranslationZ),
                RotationX = ReadString(bytes, transform.DisplayRotationX),
                RotationY = ReadString(bytes, transform.DisplayRotationY),
                RotationZ = ReadString(bytes, transform.DisplayRotationZ),
                ScaleX = ReadString(bytes, transform.DisplayScaleX),
                ScaleY = ReadString(bytes, transform.DisplayScaleY),
                ScaleZ = ReadString(bytes, transform.DisplayScaleZ),
            },
        };
    }

    private static EngineSceneCamera ReadSceneCamera(WzEditorSceneCameraAbi camera)
    {
        return new EngineSceneCamera
        {
            FieldOfViewY = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasFovY)
                ? camera.FovY
                : null,
            NearPlane = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasNearPlane)
                ? camera.NearPlane
                : null,
            FarPlane = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasFarPlane)
                ? camera.FarPlane
                : null,
            Aspect = HasFlag(camera.Flags, WzEditorSceneCameraFlags.HasAspect)
                ? camera.Aspect
                : null,
        };
    }

    private static EngineSceneRenderable ReadSceneRenderable(
        uint nodeFlags,
        WzEditorSceneRenderableAbi renderable)
    {
        return new EngineSceneRenderable
        {
            AssetGraphNodeId = HasFlag(
                nodeFlags,
                WzEditorSceneNodeFlags.RenderableHasAssetGraphNodeId)
                    ? renderable.AssetGraphNodeId
                    : null,
        };
    }

    private static List<TManaged> ReadTable<TAbi, TManaged>(
        byte[] bytes,
        WzEditorTableSpanAbi span,
        Func<byte[], TAbi, TManaged> convert)
        where TAbi : struct
    {
        if (span.Count == 0)
        {
            return [];
        }

        var itemSize = Marshal.SizeOf<TAbi>();
        var offset = CheckedBufferOffset(bytes, span.Offset, itemSize, span.Count);
        var values = new List<TManaged>(checked((int)span.Count));
        for (var index = 0; index < checked((int)span.Count); ++index)
        {
            var itemOffset = offset + index * itemSize;
            var item = MemoryMarshal.Read<TAbi>(bytes.AsSpan(itemOffset, itemSize));
            values.Add(convert(bytes, item));
        }

        return values;
    }

    private static T ReadStruct<T>(byte[] bytes, ulong offset)
        where T : struct
    {
        var size = Marshal.SizeOf<T>();
        var checkedOffset = CheckedBufferOffset(bytes, offset, size, count: 1);
        return MemoryMarshal.Read<T>(bytes.AsSpan(checkedOffset, size));
    }

    private static string ReadString(byte[] bytes, WzEditorStringSpanAbi span)
    {
        if (span.Size == 0)
        {
            return string.Empty;
        }

        var offset = CheckedBufferOffset(
            bytes,
            span.Offset,
            elementSize: 1,
            count: span.Size);
        return Encoding.UTF8.GetString(bytes, offset, checked((int)span.Size));
    }

    private static byte[] ReadBufferBytes(WzBuffer buffer, string emptyMessage)
    {
        if (buffer.Data == IntPtr.Zero)
        {
            throw new InvalidOperationException(emptyMessage);
        }

        if (buffer.Size > int.MaxValue)
        {
            throw new InvalidOperationException("Engine ABI returned a response that is too large.");
        }

        var bytes = new byte[(int)buffer.Size];
        Marshal.Copy(buffer.Data, bytes, startIndex: 0, length: bytes.Length);
        return bytes;
    }

    private static int CheckedBufferOffset(
        byte[] bytes,
        ulong offset,
        int elementSize,
        ulong count)
    {
        if (offset > int.MaxValue || count > int.MaxValue)
        {
            throw new InvalidOperationException("Engine ABI returned an offset that is too large.");
        }

        var byteCount = checked((ulong)elementSize * count);
        if (byteCount > ulong.MaxValue - offset
            || offset + byteCount > (ulong)bytes.Length)
        {
            throw new InvalidOperationException("Engine ABI returned a buffer span outside the response.");
        }

        return checked((int)offset);
    }

    private static void ValidateAbiVersion(uint version)
    {
        if (version != WozzitsEngineAbi.AbiVersion)
        {
            throw new InvalidOperationException(
                $"Engine ABI version {version} is not supported by this editor.");
        }
    }

    private static EngineProjectStatus ToProjectStatus(uint status)
    {
        return status switch
        {
            0 => EngineProjectStatus.Missing,
            1 => EngineProjectStatus.Invalid,
            2 => EngineProjectStatus.Valid,
            _ => EngineProjectStatus.Invalid,
        };
    }

    private static EngineAssetGraphConnectionStatus ToConnectionStatus(uint status)
    {
        return status switch
        {
            0 => EngineAssetGraphConnectionStatus.Compatible,
            1 => EngineAssetGraphConnectionStatus.MissingNode,
            2 => EngineAssetGraphConnectionStatus.MissingCompiler,
            3 => EngineAssetGraphConnectionStatus.InvalidInputPort,
            4 => EngineAssetGraphConnectionStatus.TypeMismatch,
            5 => EngineAssetGraphConnectionStatus.SelfDependency,
            6 => EngineAssetGraphConnectionStatus.Cycle,
            7 => EngineAssetGraphConnectionStatus.DuplicateInputPort,
            _ => EngineAssetGraphConnectionStatus.TypeMismatch,
        };
    }

    private static bool HasFlag(uint flags, uint flag)
    {
        return (flags & flag) != 0;
    }
}

internal static partial class WozzitsEngineAbi
{
    private const string LibraryName = "wozzits_abi";
    internal const uint AbiVersion = 10;

    private static int _resolverRegistered;

    internal static void EnsureResolverRegistered()
    {
        WozzitsEngineAbiLayout.AssertCurrent();

        if (Interlocked.Exchange(ref _resolverRegistered, 1) == 0)
        {
            NativeLibrary.SetDllImportResolver(
                typeof(WozzitsEngineAbi).Assembly,
                ResolveImport);
        }
    }

    private static IntPtr ResolveImport(
        string libraryName,
        Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, LibraryName, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        var path = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        return File.Exists(path)
            ? NativeLibrary.Load(path, assembly, searchPath)
            : IntPtr.Zero;
    }

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_load_project_snapshot",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorLoadProjectSnapshot(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out WzBuffer outJson);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_create_project",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorCreateProject(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string? nameUtf8,
        out WzBuffer outJson);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_scene_set_node_properties",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorSceneSetNodeProperties(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string nodeIdUtf8,
        string nameUtf8,
        uint visible);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_scene_set_node_transform",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorSceneSetNodeTransform(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string nodeIdUtf8,
        string translationXUtf8,
        string translationYUtf8,
        string translationZUtf8,
        string rotationXUtf8,
        string rotationYUtf8,
        string rotationZUtf8,
        string scaleXUtf8,
        string scaleYUtf8,
        string scaleZUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_scene_set_camera",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorSceneSetCamera(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string nodeIdUtf8,
        string fovYUtf8,
        string nearPlaneUtf8,
        string farPlaneUtf8,
        string aspectUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_set_node_position",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetNodePosition(
        string projectRootUtf8,
        string? resourceRootUtf8,
        ulong nodeId,
        double x,
        double y);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_set_zoom",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetZoom(
        string projectRootUtf8,
        string? resourceRootUtf8,
        double zoom);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_open_project_session",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorOpenProjectSession(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out IntPtr outSession);

    [LibraryImport(LibraryName, EntryPoint = "wz_editor_close_session")]
    internal static partial void WzEditorCloseSession(IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_session_asset_graph_snapshot")]
    internal static partial WzResult WzEditorSessionAssetGraphSnapshot(
        IntPtr session,
        out WzBuffer outSnapshot);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_can_connect")]
    internal static partial WzResult WzEditorAssetGraphCanConnect(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort,
        out WzBuffer outCheck);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_connect")]
    internal static partial WzResult WzEditorAssetGraphConnect(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort,
        out WzBuffer outCheck);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_disconnect_edge")]
    internal static partial WzResult WzEditorAssetGraphDisconnectEdge(
        IntPtr session,
        ulong edgeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_commit")]
    internal static partial WzResult WzEditorAssetGraphCommit(IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_compile")]
    internal static partial WzResult WzEditorAssetGraphCompile(IntPtr session);

    [LibraryImport(LibraryName, EntryPoint = "wz_free_buffer")]
    internal static partial void WzFreeBuffer(ref WzBuffer buffer);
}

internal enum WzResultCode : uint
{
    Ok = 0,
    InvalidArgument = 1,
    InternalError = 2,
    OutOfMemory = 3,
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzResult
{
    private readonly WzResultCode _code;
    private readonly IntPtr _message;

    public WzResultCode Code => _code;

    public string Message => Marshal.PtrToStringUTF8(_message) ?? string.Empty;
}

[StructLayout(LayoutKind.Sequential)]
internal struct WzBuffer
{
    public IntPtr Data;
    public ulong Size;
}

internal static class WozzitsEngineAbiLayout
{
    [Conditional("DEBUG")]
    internal static void AssertCurrent()
    {
        AssertSize<WzEditorStringSpanAbi>(16);
        AssertOffset<WzEditorStringSpanAbi>(
            nameof(WzEditorStringSpanAbi.Offset),
            0);
        AssertOffset<WzEditorStringSpanAbi>(
            nameof(WzEditorStringSpanAbi.Size),
            8);

        AssertSize<WzEditorTableSpanAbi>(16);
        AssertOffset<WzEditorTableSpanAbi>(
            nameof(WzEditorTableSpanAbi.Offset),
            0);
        AssertOffset<WzEditorTableSpanAbi>(
            nameof(WzEditorTableSpanAbi.Count),
            8);

        AssertSize<WzEditorAssetGraphPortAbi>(64);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.Index),
            0);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.Type),
            4);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.Flags),
            8);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.Name),
            16);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.Label),
            32);
        AssertOffset<WzEditorAssetGraphPortAbi>(
            nameof(WzEditorAssetGraphPortAbi.TypeName),
            48);

        AssertSize<WzEditorAssetGraphNodeAbi>(128);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.Id),
            0);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.Type),
            8);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.TypeName),
            16);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.X),
            80);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.InputPorts),
            96);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.OutputPorts),
            112);

        AssertSize<WzEditorAssetGraphEdgeAbi>(32);
        AssertOffset<WzEditorAssetGraphEdgeAbi>(
            nameof(WzEditorAssetGraphEdgeAbi.Id),
            0);
        AssertOffset<WzEditorAssetGraphEdgeAbi>(
            nameof(WzEditorAssetGraphEdgeAbi.From),
            8);
        AssertOffset<WzEditorAssetGraphEdgeAbi>(
            nameof(WzEditorAssetGraphEdgeAbi.To),
            16);
        AssertOffset<WzEditorAssetGraphEdgeAbi>(
            nameof(WzEditorAssetGraphEdgeAbi.ToInputPort),
            24);

        AssertSize<WzEditorAssetGraphConnectionCheckAbi>(96);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.AbiVersion),
            0);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.From),
            16);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.ToInputPort),
            32);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.FromType),
            40);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.Message),
            48);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.FromTypeName),
            64);
        AssertOffset<WzEditorAssetGraphConnectionCheckAbi>(
            nameof(WzEditorAssetGraphConnectionCheckAbi.ToTypeName),
            80);

        AssertSize<WzEditorAssetGraphSnapshotAbi>(80);
        AssertOffset<WzEditorAssetGraphSnapshotAbi>(
            nameof(WzEditorAssetGraphSnapshotAbi.Ok),
            0);
        AssertOffset<WzEditorAssetGraphSnapshotAbi>(
            nameof(WzEditorAssetGraphSnapshotAbi.Error),
            8);
        AssertOffset<WzEditorAssetGraphSnapshotAbi>(
            nameof(WzEditorAssetGraphSnapshotAbi.Nodes),
            40);
        AssertOffset<WzEditorAssetGraphSnapshotAbi>(
            nameof(WzEditorAssetGraphSnapshotAbi.Edges),
            56);
        AssertOffset<WzEditorAssetGraphSnapshotAbi>(
            nameof(WzEditorAssetGraphSnapshotAbi.Zoom),
            72);

        AssertSize<WzEditorSceneTransformAbi>(248);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.TranslationX),
            0);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.RotationQuaternionX),
            24);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.RotationEulerDegreesX),
            56);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.ScaleX),
            80);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayTranslationX),
            104);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayTranslationY),
            120);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayTranslationZ),
            136);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayRotationX),
            152);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayRotationY),
            168);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayRotationZ),
            184);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayScaleX),
            200);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayScaleY),
            216);
        AssertOffset<WzEditorSceneTransformAbi>(
            nameof(WzEditorSceneTransformAbi.DisplayScaleZ),
            232);

        AssertSize<WzEditorSceneCameraAbi>(40);
        AssertOffset<WzEditorSceneCameraAbi>(
            nameof(WzEditorSceneCameraAbi.FovY),
            0);
        AssertOffset<WzEditorSceneCameraAbi>(
            nameof(WzEditorSceneCameraAbi.Flags),
            32);

        AssertSize<WzEditorSceneRenderableAbi>(8);
        AssertOffset<WzEditorSceneRenderableAbi>(
            nameof(WzEditorSceneRenderableAbi.AssetGraphNodeId),
            0);

        AssertSize<WzEditorSceneRenderableSourceAbi>(32);
        AssertOffset<WzEditorSceneRenderableSourceAbi>(
            nameof(WzEditorSceneRenderableSourceAbi.Kind),
            0);
        AssertOffset<WzEditorSceneRenderableSourceAbi>(
            nameof(WzEditorSceneRenderableSourceAbi.DisplayName),
            16);

        AssertSize<WzEditorSceneComponentAbi>(32);
        AssertOffset<WzEditorSceneComponentAbi>(
            nameof(WzEditorSceneComponentAbi.Kind),
            0);
        AssertOffset<WzEditorSceneComponentAbi>(
            nameof(WzEditorSceneComponentAbi.DisplayName),
            16);

        AssertSize<WzEditorSceneNodeAbi>(432);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Id),
            0);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.DisplayName),
            16);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.ParentId),
            32);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Kind),
            48);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Flags),
            64);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Transform),
            72);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Camera),
            320);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Renderable),
            360);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderableSource),
            368);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Components),
            400);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Children),
            416);

        AssertSize<WzEditorSceneSnapshotAbi>(72);
        AssertOffset<WzEditorSceneSnapshotAbi>(
            nameof(WzEditorSceneSnapshotAbi.Ok),
            0);
        AssertOffset<WzEditorSceneSnapshotAbi>(
            nameof(WzEditorSceneSnapshotAbi.Error),
            8);
        AssertOffset<WzEditorSceneSnapshotAbi>(
            nameof(WzEditorSceneSnapshotAbi.Schema),
            24);
        AssertOffset<WzEditorSceneSnapshotAbi>(
            nameof(WzEditorSceneSnapshotAbi.Roots),
            56);

        AssertSize<WzEditorProjectSnapshotAbi>(200);
        AssertOffset<WzEditorProjectSnapshotAbi>(
            nameof(WzEditorProjectSnapshotAbi.AbiVersion),
            0);
        AssertOffset<WzEditorProjectSnapshotAbi>(
            nameof(WzEditorProjectSnapshotAbi.Error),
            16);
        AssertOffset<WzEditorProjectSnapshotAbi>(
            nameof(WzEditorProjectSnapshotAbi.ProjectName),
            32);
        AssertOffset<WzEditorProjectSnapshotAbi>(
            nameof(WzEditorProjectSnapshotAbi.AssetGraph),
            48);
        AssertOffset<WzEditorProjectSnapshotAbi>(
            nameof(WzEditorProjectSnapshotAbi.Scene),
            128);

        AssertSize<WzEditorProjectCreateAbi>(32);
        AssertOffset<WzEditorProjectCreateAbi>(
            nameof(WzEditorProjectCreateAbi.AbiVersion),
            0);
        AssertOffset<WzEditorProjectCreateAbi>(
            nameof(WzEditorProjectCreateAbi.Status),
            8);
        AssertOffset<WzEditorProjectCreateAbi>(
            nameof(WzEditorProjectCreateAbi.Error),
            16);
    }

    private static void AssertSize<T>(int expected)
        where T : struct
    {
        var actual = Marshal.SizeOf<T>();
        if (actual != expected)
        {
            throw new InvalidOperationException(
                $"{typeof(T).Name} ABI size is {actual}; expected {expected}.");
        }
    }

    private static void AssertOffset<T>(string fieldName, int expected)
    {
        var actual = Marshal.OffsetOf<T>(fieldName).ToInt32();
        if (actual != expected)
        {
            throw new InvalidOperationException(
                $"{typeof(T).Name}.{fieldName} ABI offset is {actual}; expected {expected}.");
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorStringSpanAbi
{
    public readonly ulong Offset;
    public readonly ulong Size;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorTableSpanAbi
{
    public readonly ulong Offset;
    public readonly ulong Count;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorProjectSnapshotAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly uint Status;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi Error;
    public readonly WzEditorStringSpanAbi ProjectName;
    public readonly WzEditorAssetGraphSnapshotAbi AssetGraph;
    public readonly WzEditorSceneSnapshotAbi Scene;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorProjectCreateAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly uint Status;
    public readonly uint Created;
    public readonly WzEditorStringSpanAbi Error;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphSnapshotAbi
{
    public readonly uint Ok;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi Error;
    public readonly WzEditorStringSpanAbi Schema;
    public readonly WzEditorTableSpanAbi Nodes;
    public readonly WzEditorTableSpanAbi Edges;
    public readonly double Zoom;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphNodeAbi
{
    public readonly ulong Id;
    public readonly uint Type;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi TypeName;
    public readonly WzEditorStringSpanAbi Schema;
    public readonly WzEditorStringSpanAbi DisplayName;
    public readonly WzEditorStringSpanAbi CompileStatus;
    public readonly double X;
    public readonly double Y;
    public readonly WzEditorTableSpanAbi InputPorts;
    public readonly WzEditorTableSpanAbi OutputPorts;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphPortAbi
{
    public readonly uint Index;
    public readonly uint Type;
    public readonly uint Flags;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi Name;
    public readonly WzEditorStringSpanAbi Label;
    public readonly WzEditorStringSpanAbi TypeName;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphEdgeAbi
{
    public readonly ulong Id;
    public readonly ulong From;
    public readonly ulong To;
    public readonly uint ToInputPort;
    public readonly uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphConnectionCheckAbi
{
    public readonly uint AbiVersion;
    public readonly uint Compatible;
    public readonly uint Status;
    public readonly uint ReplacesExisting;
    public readonly ulong From;
    public readonly ulong To;
    public readonly uint ToInputPort;
    public readonly uint Reserved;
    public readonly uint FromType;
    public readonly uint ToType;
    public readonly WzEditorStringSpanAbi Message;
    public readonly WzEditorStringSpanAbi FromTypeName;
    public readonly WzEditorStringSpanAbi ToTypeName;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneSnapshotAbi
{
    public readonly uint Ok;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi Error;
    public readonly WzEditorStringSpanAbi Schema;
    public readonly WzEditorStringSpanAbi Name;
    public readonly WzEditorTableSpanAbi Roots;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneNodeAbi
{
    public readonly WzEditorStringSpanAbi Id;
    public readonly WzEditorStringSpanAbi DisplayName;
    public readonly WzEditorStringSpanAbi ParentId;
    public readonly WzEditorStringSpanAbi Kind;
    public readonly uint Flags;
    public readonly uint Reserved;
    public readonly WzEditorSceneTransformAbi Transform;
    public readonly WzEditorSceneCameraAbi Camera;
    public readonly WzEditorSceneRenderableAbi Renderable;
    public readonly WzEditorSceneRenderableSourceAbi RenderableSource;
    public readonly WzEditorTableSpanAbi Components;
    public readonly WzEditorTableSpanAbi Children;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneTransformAbi
{
    public readonly double TranslationX;
    public readonly double TranslationY;
    public readonly double TranslationZ;
    public readonly double RotationQuaternionX;
    public readonly double RotationQuaternionY;
    public readonly double RotationQuaternionZ;
    public readonly double RotationQuaternionW;
    public readonly double RotationEulerDegreesX;
    public readonly double RotationEulerDegreesY;
    public readonly double RotationEulerDegreesZ;
    public readonly double ScaleX;
    public readonly double ScaleY;
    public readonly double ScaleZ;
    public readonly WzEditorStringSpanAbi DisplayTranslationX;
    public readonly WzEditorStringSpanAbi DisplayTranslationY;
    public readonly WzEditorStringSpanAbi DisplayTranslationZ;
    public readonly WzEditorStringSpanAbi DisplayRotationX;
    public readonly WzEditorStringSpanAbi DisplayRotationY;
    public readonly WzEditorStringSpanAbi DisplayRotationZ;
    public readonly WzEditorStringSpanAbi DisplayScaleX;
    public readonly WzEditorStringSpanAbi DisplayScaleY;
    public readonly WzEditorStringSpanAbi DisplayScaleZ;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneCameraAbi
{
    public readonly double FovY;
    public readonly double NearPlane;
    public readonly double FarPlane;
    public readonly double Aspect;
    public readonly uint Flags;
    public readonly uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneRenderableAbi
{
    public readonly ulong AssetGraphNodeId;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneRenderableSourceAbi
{
    public readonly WzEditorStringSpanAbi Kind;
    public readonly WzEditorStringSpanAbi DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneComponentAbi
{
    public readonly WzEditorStringSpanAbi Kind;
    public readonly WzEditorStringSpanAbi DisplayName;
}

internal static class WzEditorSceneNodeFlags
{
    public const uint HasParent = 1u << 0;
    public const uint HasVisible = 1u << 1;
    public const uint Visible = 1u << 2;
    public const uint HasTransform = 1u << 3;
    public const uint HasCamera = 1u << 4;
    public const uint HasRenderable = 1u << 5;
    public const uint RenderableHasAssetGraphNodeId = 1u << 6;
}

internal static class WzEditorSceneCameraFlags
{
    public const uint HasFovY = 1u << 0;
    public const uint HasNearPlane = 1u << 1;
    public const uint HasFarPlane = 1u << 2;
    public const uint HasAspect = 1u << 3;
}
