using System.Runtime.InteropServices;
using System.Text;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public sealed partial class WozzitsEngineNativeClient
{
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
            Diagnostics = ReadTable<WzEditorAssetGraphDiagnosticAbi, EngineAssetGraphDiagnostic>(
                bytes,
                node.Diagnostics,
                ReadAssetGraphDiagnostic),
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

    private static EngineAssetGraphDiagnostic ReadAssetGraphDiagnostic(
        byte[] bytes,
        WzEditorAssetGraphDiagnosticAbi diagnostic)
    {
        return new EngineAssetGraphDiagnostic
        {
            Severity = diagnostic.Severity,
            Code = diagnostic.Code,
            Node = diagnostic.Node,
            Edge = diagnostic.Edge,
            InputPort = diagnostic.InputPort,
            Message = ReadString(bytes, diagnostic.Message),
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
