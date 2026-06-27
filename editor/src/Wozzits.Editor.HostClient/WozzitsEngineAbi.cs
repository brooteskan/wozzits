using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Wozzits.Editor.HostClient;

internal static partial class WozzitsEngineAbi
{
    private const string LibraryName = "wozzits_abi";
    internal const uint AbiVersion = 26;

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
        EntryPoint = "wz_host_load_project_snapshot",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorLoadProjectSnapshot(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out WzBuffer outJson);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_catalog")]
    internal static partial WzResult WzEditorAssetCatalog(out WzBuffer outCatalog);

    // Device-free, read-only import of a GLB scene's component hierarchy (issue
    // #213 Phase 3b-1). glbPathUtf8 is an ABSOLUTE path; the returned WzBuffer's
    // byte 0 is a WzEditorGlbSceneHierarchyAbi (ok=0 + error on failure). Free the
    // buffer with WzFreeBuffer.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_import_glb_scene_hierarchy",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzBuffer WzImportGlbSceneHierarchy(
        string glbPathUtf8,
        uint sceneIndex);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_create_project",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorCreateProject(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string? nameUtf8,
        out WzBuffer outJson);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_scene_set_node_properties",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorSceneSetNodeProperties(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string nodeIdUtf8,
        string nameUtf8,
        uint visible);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_scene_set_node_transform",
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
        EntryPoint = "wz_host_scene_set_camera",
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
        EntryPoint = "wz_host_asset_graph_set_node_position",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetNodePosition(
        string projectRootUtf8,
        string? resourceRootUtf8,
        ulong nodeId,
        double x,
        double y);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_set_zoom",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetZoom(
        string projectRootUtf8,
        string? resourceRootUtf8,
        double zoom);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_open_project_session",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorOpenProjectSession(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out IntPtr outSession);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_close_session")]
    internal static partial void WzEditorCloseSession(IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_session_asset_graph_snapshot")]
    internal static partial WzResult WzEditorSessionAssetGraphSnapshot(
        IntPtr session,
        out WzBuffer outSnapshot);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_can_connect")]
    internal static partial WzResult WzEditorAssetGraphCanConnect(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort,
        out WzBuffer outCheck);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_connect")]
    internal static partial WzResult WzEditorAssetGraphConnect(
        IntPtr session,
        ulong fromNodeId,
        ulong toNodeId,
        uint toInputPort,
        out WzBuffer outCheck);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_disconnect_edge")]
    internal static partial WzResult WzEditorAssetGraphDisconnectEdge(
        IntPtr session,
        ulong edgeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_add_node")]
    internal static partial WzResult WzEditorAssetGraphAddNode(
        IntPtr session,
        ulong schema,
        uint type,
        out ulong outNodeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_remove_node")]
    internal static partial WzResult WzEditorAssetGraphRemoveNode(
        IntPtr session,
        ulong nodeId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_session_set_node_position")]
    internal static partial WzResult WzEditorSessionSetNodePosition(
        IntPtr session,
        ulong nodeId,
        double x,
        double y);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_session_set_zoom")]
    internal static partial WzResult WzEditorSessionSetZoom(
        IntPtr session,
        double zoom);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_asset_graph_set_node_param_string",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorAssetGraphSetNodeParamString(
        IntPtr session,
        ulong nodeId,
        string nameUtf8,
        string valueUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_session_save")]
    internal static partial WzResult WzEditorSessionSave(IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_start",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial IntPtr WzEditorRuntimeStart(
        string projectRootUtf8,
        string? resourceRootUtf8,
        IntPtr logCallback,
        IntPtr logUser);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_stop")]
    internal static partial void WzEditorRuntimeStop(IntPtr runtime);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_is_running")]
    internal static partial int WzEditorRuntimeIsRunning(IntPtr runtime);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_bind_draft")]
    internal static partial WzResult WzEditorRuntimeBindDraft(
        IntPtr runtime,
        IntPtr session);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_transform",
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
        EntryPoint = "wz_host_runtime_set_node_properties",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeProperties(
        IntPtr runtime,
        string nodeIdUtf8,
        string nameUtf8,
        uint visible);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_reparent_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeReparentNode(
        IntPtr runtime,
        string nodeIdUtf8,
        string newParentIdUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_remove_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeRemoveNode(
        IntPtr runtime,
        string nodeIdUtf8);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_save_scene")]
    internal static partial WzResult WzEditorRuntimeSaveScene(IntPtr runtime);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_reload_behavior_modules")]
    internal static partial WzResult WzEditorRuntimeReloadBehaviorModules(
        IntPtr runtime);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_behavior_module_catalog")]
    internal static partial WzResult WzEditorRuntimeBehaviorModuleCatalog(
        IntPtr runtime,
        out WzBuffer outModules);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_add_child_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeAddChildNode(
        IntPtr runtime,
        string parentIdUtf8,
        out WzBuffer outNewId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_add_node_behavior",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeAddNodeBehavior(
        IntPtr runtime,
        string nodeIdUtf8,
        string moduleUtf8,
        out WzBuffer outBindingId);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_remove_node_behavior",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeRemoveNodeBehavior(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_behavior_enabled",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeBehaviorEnabled(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8,
        uint enabled);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_behavior_fields",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeBehaviorFields(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8,
        string labelUtf8,
        string moduleUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_behavior_events",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeBehaviorEvents(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8,
        string eventsUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_behavior_config",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeBehaviorConfig(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8,
        string keyUtf8,
        string kindUtf8,
        string valueUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_clear_node_behavior_config",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeClearNodeBehaviorConfig(
        IntPtr runtime,
        string nodeIdUtf8,
        string bindingIdUtf8,
        string keyUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_add_node_component",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeAddNodeComponent(
        IntPtr runtime,
        string nodeIdUtf8,
        string kindUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_remove_node_component",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeRemoveNodeComponent(
        IntPtr runtime,
        string nodeIdUtf8,
        string kindUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_renderable_asset",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeRenderableAsset(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId);

    // Author a node's GEOMETRY ingredient of its render binding — point it at a
    // Mesh / GpuSparseMesh asset-graph node (0 = clear). The engine assembles the
    // RHI renderable from this geometry plus the effective render program (#213
    // increment 2). Live + host-gated, no-op success when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_geometry_asset",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeGeometryAsset(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId);

    // Author a node's RENDER-PROGRAM ingredient of its render binding — point it
    // at a render-program asset-graph node (0 = clear). The program is INHERITED
    // down the scene tree, so this cascades to descendants without their own
    // program (#213 increment 2). Live + host-gated, no-op success when no
    // viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_render_program",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeRenderProgram(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId);

    // Point a node at a "Scene from GLB" asset-graph node so the runtime grafts
    // that GLB's hierarchy as the node's children (issue #213 piece 2). The id is
    // the asset-graph node's id (0 = clear); consumeMode uses the WZ_SCENE_SOURCE_*
    // tokens (0 = instance, 1 = flatten).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_scene_source",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeSceneSource(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId,
        uint consumeMode);

    // Author (empty/null glbPathUtf8 clears) a node's GLB scene-source descriptor
    // — the asset-graph-independent route (issue #213 Phase 3a). consumeMode uses
    // the WZ_SCENE_SOURCE_* tokens (0 = instance, 1 = flatten).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_glb_scene_source",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeGlbSceneSource(
        IntPtr runtime,
        string nodeIdUtf8,
        string glbPathUtf8,
        uint sceneIndex,
        uint consumeMode);

    // Assign a per-component render style into a node's GLB scene-source
    // descriptor (issue #213 Phase 3b-2). targetBase: 1 = base style, 0 = per-mesh
    // override for meshIndex. The style subset is surface/wireframe enabled (0/1) +
    // an RGBA float[4] each; a null array uses the engine-default color. The style
    // is written into the persisted descriptor so a headless load renders it.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_glb_component_style",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeGlbComponentStyle(
        IntPtr runtime,
        string nodeIdUtf8,
        uint targetBase,
        uint meshIndex,
        uint surfaceEnabled,
        [MarshalUsing(ConstantElementCount = 4)] float[]? surfaceRgba,
        uint wireframeEnabled,
        [MarshalUsing(ConstantElementCount = 4)] float[]? wireframeRgba);

    // Clear the per-mesh-index render-style override for meshIndex in a node's GLB
    // scene-source descriptor (issue #213 Phase 3b-2).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_clear_node_glb_component_style",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeClearNodeGlbComponentStyle(
        IntPtr runtime,
        string nodeIdUtf8,
        uint meshIndex);

    // Fetch the running runtime's grafted scene nodes (issue #213) as a project-
    // snapshot blob — the SAME WzEditorProjectSnapshotAbi layout as
    // WzEditorLoadProjectSnapshot, so the existing reader decodes it. Only the
    // scene part is meaningful (its roots are instance-grafted sub-trees, each
    // root carrying its host id as ParentId). BLOCKING (mirrors AddChildNode). A
    // null/not-running runtime yields an ok blob with an empty scene. Free the
    // returned buffer with WzFreeBuffer.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_grafted_scene_snapshot")]
    internal static partial WzBuffer WzEditorRuntimeGraftedSceneSnapshot(
        IntPtr runtime);

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

        AssertSize<WzEditorGlbStyleAbi>(40);
        AssertOffset<WzEditorGlbStyleAbi>(
            nameof(WzEditorGlbStyleAbi.SurfaceEnabled),
            0);
        AssertOffset<WzEditorGlbStyleAbi>(
            nameof(WzEditorGlbStyleAbi.SurfaceR),
            4);
        AssertOffset<WzEditorGlbStyleAbi>(
            nameof(WzEditorGlbStyleAbi.WireframeEnabled),
            20);
        AssertOffset<WzEditorGlbStyleAbi>(
            nameof(WzEditorGlbStyleAbi.WireframeR),
            24);

        AssertSize<WzEditorGlbStyleOverrideAbi>(48);
        AssertOffset<WzEditorGlbStyleOverrideAbi>(
            nameof(WzEditorGlbStyleOverrideAbi.MeshIndex),
            0);
        AssertOffset<WzEditorGlbStyleOverrideAbi>(
            nameof(WzEditorGlbStyleOverrideAbi.Reserved),
            4);
        AssertOffset<WzEditorGlbStyleOverrideAbi>(
            nameof(WzEditorGlbStyleOverrideAbi.Style),
            8);

        AssertSize<WzEditorSceneSceneSourceAbi>(120);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.Kind),
            0);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.Path),
            16);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.ConsumeMode),
            32);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.SceneIndex),
            48);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.StyleOverrideCount),
            52);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.HasBaseStyle),
            56);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.Reserved),
            60);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.BaseStyle),
            64);
        AssertOffset<WzEditorSceneSceneSourceAbi>(
            nameof(WzEditorSceneSceneSourceAbi.StyleOverrides),
            104);

        AssertSize<WzEditorSceneComponentAbi>(32);
        AssertOffset<WzEditorSceneComponentAbi>(
            nameof(WzEditorSceneComponentAbi.Kind),
            0);
        AssertOffset<WzEditorSceneComponentAbi>(
            nameof(WzEditorSceneComponentAbi.DisplayName),
            16);

        AssertSize<WzEditorSceneBehaviorAbi>(104);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Id),
            0);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Label),
            16);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Module),
            32);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Name),
            48);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Enabled),
            64);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Events),
            72);
        AssertOffset<WzEditorSceneBehaviorAbi>(
            nameof(WzEditorSceneBehaviorAbi.Config),
            88);

        AssertSize<WzEditorSceneNodeAbi>(592);
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
            nameof(WzEditorSceneNodeAbi.Behaviors),
            416);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Children),
            432);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.SceneSource),
            448);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.SceneSourceNodeId),
            568);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.GeometryNodeId),
            576);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderProgramNodeId),
            584);

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

        AssertSize<WzEditorGlbComponentAbi>(64);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.Id),
            0);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.Name),
            16);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.ParentId),
            32);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.Flags),
            48);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.MeshIndex),
            52);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.NodeIndex),
            56);
        AssertOffset<WzEditorGlbComponentAbi>(
            nameof(WzEditorGlbComponentAbi.Reserved),
            60);

        AssertSize<WzEditorGlbSceneHierarchyAbi>(64);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.AbiVersion),
            0);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.Ok),
            4);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.Error),
            8);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.SceneName),
            24);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.SceneIndex),
            40);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.Reserved),
            44);
        AssertOffset<WzEditorGlbSceneHierarchyAbi>(
            nameof(WzEditorGlbSceneHierarchyAbi.Components),
            48);
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
    public readonly WzEditorTableSpanAbi Behaviors;
    public readonly WzEditorTableSpanAbi Children;
    public readonly WzEditorSceneSceneSourceAbi SceneSource;
    // Authored render-binding refs (issue #213), each valid iff its HAS_* flag is
    // set. Appended last to mirror the native struct.
    public readonly ulong SceneSourceNodeId;
    public readonly ulong GeometryNodeId;
    public readonly ulong RenderProgramNodeId;
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

// The high-impact subset of a MeshRenderStyleData packed for read-back +
// re-authoring of a GLB component's style (issue #213 Phase 3b-2): surface +
// wireframe enabled flags (0/1) and RGBA colors. Mirrors WzEditorGlbStyle.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorGlbStyleAbi
{
    public readonly uint SurfaceEnabled;     // 0/1
    public readonly float SurfaceR;
    public readonly float SurfaceG;
    public readonly float SurfaceB;
    public readonly float SurfaceA;
    public readonly uint WireframeEnabled;   // 0/1
    public readonly float WireframeR;
    public readonly float WireframeG;
    public readonly float WireframeB;
    public readonly float WireframeA;
}

// One per-mesh-index style override (issue #213 Phase 3b-2). Mirrors
// WzEditorGlbStyleOverride.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorGlbStyleOverrideAbi
{
    public readonly uint MeshIndex;
    public readonly uint Reserved;
    public readonly WzEditorGlbStyleAbi Style;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneSceneSourceAbi
{
    public readonly WzEditorStringSpanAbi Kind;          // "glb"
    public readonly WzEditorStringSpanAbi Path;
    public readonly WzEditorStringSpanAbi ConsumeMode;   // "instance" | "flatten"
    public readonly uint SceneIndex;
    public readonly uint StyleOverrideCount;
    public readonly uint HasBaseStyle;                   // 0/1
    public readonly uint Reserved;
    public readonly WzEditorGlbStyleAbi BaseStyle;       // valid iff HasBaseStyle
    public readonly WzEditorTableSpanAbi StyleOverrides; // WzEditorGlbStyleOverrideAbi[]
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneComponentAbi
{
    public readonly WzEditorStringSpanAbi Kind;
    public readonly WzEditorStringSpanAbi DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneBehaviorAbi
{
    public readonly WzEditorStringSpanAbi Id;
    public readonly WzEditorStringSpanAbi Label;
    public readonly WzEditorStringSpanAbi Module;
    public readonly WzEditorStringSpanAbi Name;
    public readonly uint Enabled;
    public readonly uint Reserved;
    public readonly WzEditorTableSpanAbi Events;   // WzEditorStringSpanAbi[]
    public readonly WzEditorTableSpanAbi Config;   // WzEditorAssetGraphParamAbi[]
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorGlbComponentAbi
{
    public readonly WzEditorStringSpanAbi Id;
    public readonly WzEditorStringSpanAbi Name;
    public readonly WzEditorStringSpanAbi ParentId;   // valid iff HasParent
    public readonly uint Flags;
    public readonly uint MeshIndex;                    // valid iff HasMesh
    public readonly uint NodeIndex;
    public readonly uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorGlbSceneHierarchyAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly WzEditorStringSpanAbi Error;
    public readonly WzEditorStringSpanAbi SceneName;
    public readonly uint SceneIndex;
    public readonly uint Reserved;
    public readonly WzEditorTableSpanAbi Components;   // WzEditorGlbComponentAbi[]
}

internal static class WzEditorGlbComponentFlags
{
    public const uint HasParent = 1u << 0;
    public const uint HasMesh = 1u << 1;
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
    public const uint HasSceneSource = 1u << 7;
    // Authored render-binding refs (issue #213). HasSceneSourceRef is the "Subtree
    // from asset" node ref, distinct from HasSceneSource (the GLB descriptor).
    public const uint HasSceneSourceRef = 1u << 8;
    public const uint HasGeometry = 1u << 9;
    public const uint HasRenderProgram = 1u << 10;
}

internal static class WzEditorSceneCameraFlags
{
    public const uint HasFovY = 1u << 0;
    public const uint HasNearPlane = 1u << 1;
    public const uint HasFarPlane = 1u << 2;
    public const uint HasAspect = 1u << 3;
}
