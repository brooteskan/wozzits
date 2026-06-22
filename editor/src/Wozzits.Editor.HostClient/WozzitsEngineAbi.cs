using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Wozzits.Editor.HostClient;

internal static partial class WozzitsEngineAbi
{
    private const string LibraryName = "wozzits_abi";
    internal const uint AbiVersion = 20;

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
        EntryPoint = "wz_editor_asset_catalog")]
    internal static partial WzResult WzEditorAssetCatalog(out WzBuffer outCatalog);

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
        EntryPoint = "wz_editor_asset_graph_add_node")]
    internal static partial WzResult WzEditorAssetGraphAddNode(
        IntPtr session,
        ulong schema,
        uint type,
        out ulong outNodeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_remove_node")]
    internal static partial WzResult WzEditorAssetGraphRemoveNode(
        IntPtr session,
        ulong nodeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_session_set_node_position")]
    internal static partial WzResult WzEditorSessionSetNodePosition(
        IntPtr session,
        ulong nodeId,
        double x,
        double y);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_session_set_zoom")]
    internal static partial WzResult WzEditorSessionSetZoom(
        IntPtr session,
        double zoom);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_asset_graph_set_node_param_string",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetNodeParamString(
        IntPtr session,
        ulong nodeId,
        string nameUtf8,
        string valueUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_session_save")]
    internal static partial WzResult WzEditorSessionSave(IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_start",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr WzEditorRuntimeStart(
        string projectRootUtf8,
        string? resourceRootUtf8,
        IntPtr logCallback,
        IntPtr logUser);

    [LibraryImport(LibraryName, EntryPoint = "wz_editor_runtime_stop")]
    internal static partial void WzEditorRuntimeStop(IntPtr runtime);

    [LibraryImport(LibraryName, EntryPoint = "wz_editor_runtime_is_running")]
    internal static partial int WzEditorRuntimeIsRunning(IntPtr runtime);

    [LibraryImport(LibraryName, EntryPoint = "wz_editor_runtime_bind_draft")]
    internal static partial WzResult WzEditorRuntimeBindDraft(
        IntPtr runtime,
        IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_set_node_transform",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeTransform(
        IntPtr runtime,
        string nodeIdUtf8,
        double translationX,
        double translationY,
        double translationZ,
        double rotationXDegrees,
        double rotationYDegrees,
        double rotationZDegrees,
        double scaleX,
        double scaleY,
        double scaleZ);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_set_node_properties",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeProperties(
        IntPtr runtime,
        string nodeIdUtf8,
        string nameUtf8,
        uint visible);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_reparent_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeReparentNode(
        IntPtr runtime,
        string nodeIdUtf8,
        string newParentIdUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_remove_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeRemoveNode(
        IntPtr runtime,
        string nodeIdUtf8);

    [LibraryImport(LibraryName, EntryPoint = "wz_editor_runtime_save_scene")]
    internal static partial WzResult WzEditorRuntimeSaveScene(IntPtr runtime);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_editor_runtime_add_child_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeAddChildNode(
        IntPtr runtime,
        string parentIdUtf8,
        out WzBuffer outNewId);

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

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void WzEditorLogCallback(
    uint level,
    IntPtr timestampUtf8,
    ulong timestampSize,
    IntPtr messageUtf8,
    ulong messageSize,
    IntPtr logUser);

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

        AssertSize<WzEditorAssetGraphDiagnosticAbi>(80);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.Severity),
            0);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.Code),
            4);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.Node),
            8);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.InputPort),
            24);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.Message),
            32);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.SeverityName),
            48);
        AssertOffset<WzEditorAssetGraphDiagnosticAbi>(
            nameof(WzEditorAssetGraphDiagnosticAbi.CodeName),
            64);

        AssertSize<WzEditorAssetGraphNodeAbi>(160);
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
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.Diagnostics),
            128);
        AssertOffset<WzEditorAssetGraphNodeAbi>(
            nameof(WzEditorAssetGraphNodeAbi.Params),
            144);

        AssertSize<WzEditorAssetGraphParamAbi>(64);
        AssertOffset<WzEditorAssetGraphParamAbi>(
            nameof(WzEditorAssetGraphParamAbi.Name),
            0);
        AssertOffset<WzEditorAssetGraphParamAbi>(
            nameof(WzEditorAssetGraphParamAbi.Kind),
            16);
        AssertOffset<WzEditorAssetGraphParamAbi>(
            nameof(WzEditorAssetGraphParamAbi.Value),
            32);
        AssertOffset<WzEditorAssetGraphParamAbi>(
            nameof(WzEditorAssetGraphParamAbi.Options),
            48);

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

        AssertSize<WzEditorAssetCatalogSchemaAbi>(24);
        AssertOffset<WzEditorAssetCatalogSchemaAbi>(
            nameof(WzEditorAssetCatalogSchemaAbi.Schema),
            0);
        AssertOffset<WzEditorAssetCatalogSchemaAbi>(
            nameof(WzEditorAssetCatalogSchemaAbi.Label),
            8);

        AssertSize<WzEditorAssetCatalogEntryAbi>(56);
        AssertOffset<WzEditorAssetCatalogEntryAbi>(
            nameof(WzEditorAssetCatalogEntryAbi.Type),
            0);
        AssertOffset<WzEditorAssetCatalogEntryAbi>(
            nameof(WzEditorAssetCatalogEntryAbi.TypeName),
            8);
        AssertOffset<WzEditorAssetCatalogEntryAbi>(
            nameof(WzEditorAssetCatalogEntryAbi.Category),
            24);
        AssertOffset<WzEditorAssetCatalogEntryAbi>(
            nameof(WzEditorAssetCatalogEntryAbi.Schemas),
            40);

        AssertSize<WzEditorAssetCatalogAbi>(24);
        AssertOffset<WzEditorAssetCatalogAbi>(
            nameof(WzEditorAssetCatalogAbi.AbiVersion),
            0);
        AssertOffset<WzEditorAssetCatalogAbi>(
            nameof(WzEditorAssetCatalogAbi.Entries),
            8);
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
internal readonly struct WzEditorAssetCatalogAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly WzEditorTableSpanAbi Entries;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetCatalogEntryAbi
{
    public readonly uint Type;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi TypeName;
    public readonly WzEditorStringSpanAbi Category;
    public readonly WzEditorTableSpanAbi Schemas;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetCatalogSchemaAbi
{
    public readonly ulong Schema;
    public readonly WzEditorStringSpanAbi Label;
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
    public readonly WzEditorTableSpanAbi Diagnostics;
    public readonly WzEditorTableSpanAbi Params;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphParamAbi
{
    public readonly WzEditorStringSpanAbi Name;
    public readonly WzEditorStringSpanAbi Kind;
    public readonly WzEditorStringSpanAbi Value;
    public readonly WzEditorTableSpanAbi Options;
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
internal readonly struct WzEditorAssetGraphDiagnosticAbi
{
    public readonly uint Severity;
    public readonly uint Code;
    public readonly ulong Node;
    public readonly ulong Edge;
    public readonly uint InputPort;
    public readonly uint Reserved;
    public readonly WzEditorStringSpanAbi Message;
    public readonly WzEditorStringSpanAbi SeverityName;
    public readonly WzEditorStringSpanAbi CodeName;
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
