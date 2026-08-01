using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Wozzits.Editor.HostClient;

internal static partial class WozzitsEngineAbi
{
    private const string LibraryName = "wozzits_abi";
    internal const uint AbiVersion = 35;

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

        // A DllImportResolver is invoked once per DISTINCT EXTERN METHOD, not once
        // per library, so this runs ~78 times over a session. Resolve the load path
        // exactly ONCE (D3-C2): on the second call TryShadowCopy's File.Copy targets
        // our own, now-loaded shadow DLL, fails, and the fallback below loaded THE
        // BUILD OUTPUT -- locking the file the shadow copy exists to keep free and
        // mapping a second engine image with its own C++ statics. Measured: after
        // extern #1 the build output was unlocked and one image was mapped; after
        // extern #2 it was locked and two were.
        var loadPath = _resolvedLoadPath;
        if (loadPath is null)
        {
            lock (ResolveGate)
            {
                _resolvedLoadPath = loadPath = ResolveLoadPathOnce();
            }
        }

        if (loadPath.Length == 0)
        {
            // Nothing at the resolved path -> let the default runtime search
            // (app dir / PATH) try, exactly as before.
            return IntPtr.Zero;
        }

        return NativeLibrary.Load(loadPath, assembly, searchPath);
    }

    private static readonly object ResolveGate = new();

    // "" means "nothing there, fall back to the runtime's own search".
    private static string? _resolvedLoadPath;

    private static string ResolveLoadPathOnce()
    {
        var source = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        if (!File.Exists(source))
        {
            WozzitsEngineNativeClient.RecordAbiLoadPath(source);
            return string.Empty;
        }

        // Load a private SHADOW COPY, never the build output itself, so a running
        // editor never locks wozzits_abi.dll. That means you can rebuild the engine
        // (any target/config) while the editor is open without a "permission
        // denied" relink, and the freshest build is picked up on the next launch.
        var loadPath = TryShadowCopy(source) ?? source;
        WozzitsEngineNativeClient.RecordAbiLoadPath(loadPath);
        return loadPath;
    }

    // Copy the resolved DLL (+ its .pdb, best-effort for stack traces) into a
    // per-process temp dir and return the copy's path, so the original build
    // output stays unlocked. Returns null on any failure -> caller loads the
    // source directly (degrading to the old locking behavior rather than failing).
    private static string? TryShadowCopy(string source)
    {
        try
        {
            CleanupStaleShadowDirs();

            var shadowDir = Path.Combine(
                Path.GetTempPath(),
                ShadowRootName,
                Environment.ProcessId.ToString());
            Directory.CreateDirectory(shadowDir);

            var shadowDll = Path.Combine(shadowDir, Path.GetFileName(source));
            File.Copy(source, shadowDll, overwrite: true);

            var pdb = Path.ChangeExtension(source, ".pdb");
            if (File.Exists(pdb))
            {
                try
                {
                    File.Copy(pdb, Path.ChangeExtension(shadowDll, ".pdb"), overwrite: true);
                }
                catch
                {
                    // Symbols are a nicety; a failure here must not block loading.
                }
            }

            return shadowDll;
        }
        catch
        {
            return null;
        }
    }

    // Reclaim shadow dirs left by DEAD editor processes.
    //
    // The locked DLL is NOT enough to protect a live owner (D3-C3): .NET's
    // recursive delete records the first error and KEEPS GOING, so it strips
    // every unlocked file in that directory -- notably the ~85 MB
    // wozzits_abi.pdb that TryShadowCopy copies for stack traces -- and leaves
    // only the .dll behind. Measured: [dll, pdb, other] -> [dll].
    //
    // So skip any directory whose owning pid is still alive, including our own.
    // A recycled pid means we skip a genuinely orphaned directory until the next
    // launch; that is a bounded leak, and the alternative is deleting a running
    // editor's symbols.
    private static void CleanupStaleShadowDirs()
    {
        try
        {
            var root = Path.Combine(Path.GetTempPath(), ShadowRootName);
            if (!Directory.Exists(root))
            {
                return;
            }

            foreach (var dir in Directory.EnumerateDirectories(root))
            {
                if (IsOwnedByLiveProcess(dir))
                {
                    continue;
                }

                try
                {
                    Directory.Delete(dir, recursive: true);
                }
                catch
                {
                    // Raced another editor's cleanup, or the owner started
                    // holding it between the check and here. Leave it be.
                }
            }
        }
        catch
        {
            // Cleanup is best-effort; never let it break loading.
        }
    }

    private static bool IsOwnedByLiveProcess(string dir)
    {
        if (!int.TryParse(Path.GetFileName(dir), out var pid))
        {
            return false;   // not one of ours; the delete below will fail harmlessly
        }

        if (pid == Environment.ProcessId)
        {
            return true;
        }

        try
        {
            using var owner = Process.GetProcessById(pid);
            return !owner.HasExited;
        }
        catch (ArgumentException)
        {
            return false;   // no such process -> genuinely orphaned
        }
        catch (InvalidOperationException)
        {
            return false;
        }
    }

    private const string ShadowRootName = "wozzits-editor-abi";

    [LibraryImport(LibraryName, EntryPoint = "wz_abi_version")]
    internal static partial uint WzAbiVersion();

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

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_behavior_actuator_catalog")]
    internal static partial WzResult WzEditorBehaviorActuatorCatalog(out WzBuffer outCatalog);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_project_behavior_actuator_catalog",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorProjectBehaviorActuatorCatalog(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out WzBuffer outCatalog);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_behavior_module_catalog")]
    internal static partial WzResult WzEditorBehaviorModuleCatalog(out WzBuffer outCatalog);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_project_behavior_module_catalog",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorProjectBehaviorModuleCatalog(
        string projectRootUtf8,
        string? resourceRootUtf8,
        out WzBuffer outCatalog);

    // Device-free, read-only import of a GLB scene's component hierarchy (issue
    // #213 Phase 3b-1). glbPathUtf8 is the authored source_path verbatim (relative
    // or absolute, optionally quote-wrapped); the engine roots it against
    // resourceRootUtf8 using its file-carrier convention, so no path resolution is
    // done editor-side. The returned WzBuffer's byte 0 is a
    // WzEditorGlbSceneHierarchyAbi (ok=0 + error on failure). Free the buffer with
    // WzFreeBuffer.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_import_glb_scene_hierarchy",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzBuffer WzImportGlbSceneHierarchy(
        string glbPathUtf8,
        string? resourceRootUtf8,
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

    // Mint a minimal scenelet engine-side and get back its resource-relative
    // path. Device-free and runtime-free, like CreateProject.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_create_scenelet",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorCreateScenelet(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string nameUtf8,
        out WzBuffer outPath);

    // Set one config entry on matching behaviour bindings in a scene FILE -- the
    // file-target twin of SetNodeBehaviorConfig, which only reaches the running
    // scene. Device-free; idempotent (0 updates => the file was not rewritten).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_set_scene_file_behavior_config",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorSetSceneFileBehaviorConfig(
        string projectRootUtf8,
        string? resourceRootUtf8,
        string sceneRelPathUtf8,
        string moduleUtf8,
        string matchKeyUtf8,
        string matchValueUtf8,
        string configKeyUtf8,
        string valueUtf8,
        out uint outUpdatedCount);

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
        EntryPoint = "wz_host_asset_graph_add_inochi_shared_assets")]
    internal static partial WzResult WzEditorAssetGraphAddInochiSharedAssets(
        IntPtr session,
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

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_set_frame_profiling")]
    internal static partial void WzEditorRuntimeSetFrameProfiling(
        IntPtr runtime,
        int enabled);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_set_paused")]
    internal static partial void WzEditorRuntimeSetPaused(
        IntPtr runtime,
        int paused);

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

    // Move nodeId to just BEFORE beforeNodeId in the flat draw list (empty
    // before-id => move to the end / drawn last). Draw order only; nesting stays
    // parent_id-based. New export, no struct change => WZ_ABI_VERSION unchanged.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_reorder_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeReorderNode(
        IntPtr runtime,
        string nodeIdUtf8,
        string beforeNodeIdUtf8);

    // Set a node's render_order (draw-order LAYER); the engine re-bakes draw
    // order so the node moves to its layer. New export; the v30 bump is the
    // snapshot struct's render_order field, not this function.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_render_order",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeRenderOrder(
        IntPtr runtime,
        string nodeIdUtf8,
        int renderOrder);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_remove_node",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeRemoveNode(
        IntPtr runtime,
        string nodeIdUtf8);

    [LibraryImport(LibraryName, EntryPoint = "wz_host_runtime_save_scene")]
    internal static partial WzResult WzEditorRuntimeSaveScene(IntPtr runtime);

    // Carve the selected scene node + its descendants out of the running scene
    // and write them as a standalone scene.json (a reusable prefab / "scenelet").
    // rootNodeIdUtf8 is the node's authored string id; outPathUtf8 is an absolute
    // file path. Errors when no viewport is running (there is nothing to export).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_export_subtree_as_scene",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorExportSubtreeAsScene(
        IntPtr runtime,
        string rootNodeIdUtf8,
        string outPathUtf8);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_reload_behavior_modules")]
    internal static partial WzResult WzEditorRuntimeReloadBehaviorModules(
        IntPtr runtime);

    // The edits the engine thread could not apply because the node was gone
    // (#308, A1-C6). The mutation verbs are fire-and-forget, so this is the only
    // channel carrying the outcome back. Newline-delimited; TAKE semantics, so
    // whatever is drained must be surfaced -- a second call returns nothing.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_take_dropped_edits")]
    internal static partial WzResult WzEditorRuntimeTakeDroppedEdits(
        IntPtr runtime,
        out WzBuffer outEdits);

    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_behavior_module_catalog")]
    internal static partial WzResult WzEditorRuntimeBehaviorModuleCatalog(
        IntPtr runtime,
        out WzBuffer outModules);

    // The project's scenelets (prefabs) for the scenelet menu. Newline-delimited
    // UTF-8 "name\tpath" lines in outScenelets (free with WzFreeBuffer).
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_scenelet_catalog")]
    internal static partial WzResult WzEditorRuntimeSceneletCatalog(
        IntPtr runtime,
        out WzBuffer outScenelets);

    // Swap the working scene to scenePathUtf8 (open a scenelet for editing, or
    // switch back to the main scene). Blocks until the engine thread loads it.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_open_scene",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeOpenScene(
        IntPtr runtime,
        string scenePathUtf8);

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

    // Author a node's AUDIO SOURCE renderable reference (audio-track item 10):
    // point its AudioSource at an audio-renderable asset-graph node (0 = clear),
    // creating the component on a non-zero pick. Live + host-gated, no-op success
    // when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_audio_renderable",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeAudioRenderable(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId);

    // Set an AudioSource's per-entity play policy (auto_play / enabled). Live +
    // host-gated; no-op on the engine thread if the node has no AudioSource.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_audio_source_play",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeAudioSourcePlay(
        IntPtr runtime,
        string nodeIdUtf8,
        byte autoPlay,
        byte enabled);

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

    // Author ONE semantic resource binding of a node's custom-renderable
    // ingredients (issue #229/#230): upsert the row for the layout semantic
    // pointing at an asset-graph source node (0 = remove the row). A binding
    // present makes the assembled renderable the CUSTOM (0x70A) form. Live +
    // host-gated, no-op success when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_renderable_binding",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeRenderableBinding(
        IntPtr runtime,
        string nodeIdUtf8,
        string semanticUtf8,
        ulong assetGraphNodeId);

    // Author ONE per-instance constant override of a node's custom-renderable
    // ingredients (issue #229/#230): upsert the named layout constant with 4
    // floats (null = remove the override). Overrides merge at PACK time — no
    // recompile, no re-key; the next rendered frame reflects the edit. Live +
    // host-gated, no-op success when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_renderable_param",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeRenderableParam(
        IntPtr runtime,
        string nodeIdUtf8,
        string nameUtf8,
        [MarshalUsing(ConstantElementCount = 4)] float[]? valueXyzw);

    // Author a node's COLLISION component (terrain-stick track): point it at a
    // Collision asset-graph node (0 = clear the reference) and toggle whether the
    // node's movement is constrained by that collision data. Live + host-gated,
    // no-op success when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_collision",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeCollision(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong assetGraphNodeId,
        byte constrainMovement);

    // Author a node's ATMOSPHERE component: the Atmosphere asset-graph node the
    // frame's fog reads (0 clears the ref) + enabled. One combined seam (ref + a
    // single bool), like collision. Live + host-gated, no-op success when no
    // viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_atmosphere",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeAtmosphere(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong atmosphereAssetNodeId,
        byte enabled);

    // Author a node's FRAME ENVIRONMENT component: the FrameEnvironment asset-graph
    // node the frame's environment reads (0 clears the ref) + enabled. One combined
    // seam (ref + a single bool), like atmosphere. Live + host-gated, no-op success
    // when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_environment",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeEnvironment(
        IntPtr runtime,
        string nodeIdUtf8,
        ulong environmentAssetNodeId,
        byte enabled);

    // Author a node's MOTION component terrain-constraint fields (terrain-stick
    // track): whether the node sticks to the terrain surface, plus ride height,
    // footprint radius, surface-alignment toggle, and alignment strength. Live +
    // host-gated, no-op success when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_motion_terrain",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeMotionTerrain(
        IntPtr runtime,
        string nodeIdUtf8,
        byte terrainConstrained,
        float rideHeight,
        float footprintRadius,
        byte alignToSurface,
        float alignmentStrength);

    // Author a node's MOTION FILTER component: set the whole component (the
    // editor sends it all on any change). Live + host-gated, no-op success when
    // no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_motion_filter",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeMotionFilter(
        IntPtr runtime,
        string nodeIdUtf8,
        in WzEditorSceneMotionFilterAbi filter);

    // Author a node's CAMERA component field values (fov_y / near / far / aspect;
    // the editor sends all four on any change). Live + host-gated, no-op success
    // when no viewport is running.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_set_node_camera",
        StringMarshalling = StringMarshalling.Utf8)]
    internal static partial WzResult WzEditorRuntimeSetNodeCamera(
        IntPtr runtime,
        string nodeIdUtf8,
        double fovY,
        double nearPlane,
        double farPlane,
        double aspect);

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

    // Snapshot of the RUNNING scene's authored nodes (same blob layout), to rebuild
    // the tree after OpenScene swaps the working scene. BLOCKING; free with
    // WzFreeBuffer.
    [LibraryImport(
        LibraryName,
        EntryPoint = "wz_host_runtime_scene_snapshot")]
    internal static partial WzBuffer WzEditorRuntimeSceneSnapshot(
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
            nameof(WzEditorAssetGraphSnapshotAbi.AbiVersion),
            4);
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

        AssertSize<WzEditorSceneCollisionAbi>(8);
        AssertOffset<WzEditorSceneCollisionAbi>(
            nameof(WzEditorSceneCollisionAbi.CollisionAssetNodeId),
            0);
        AssertOffset<WzEditorSceneCollisionAbi>(
            nameof(WzEditorSceneCollisionAbi.HasCollisionRef),
            4);
        AssertOffset<WzEditorSceneCollisionAbi>(
            nameof(WzEditorSceneCollisionAbi.ConstrainMovement),
            5);

        AssertSize<WzEditorSceneMotionAbi>(16);
        AssertOffset<WzEditorSceneMotionAbi>(
            nameof(WzEditorSceneMotionAbi.TerrainConstrained),
            0);
        AssertOffset<WzEditorSceneMotionAbi>(
            nameof(WzEditorSceneMotionAbi.AlignToSurface),
            1);
        AssertOffset<WzEditorSceneMotionAbi>(
            nameof(WzEditorSceneMotionAbi.RideHeight),
            4);
        AssertOffset<WzEditorSceneMotionAbi>(
            nameof(WzEditorSceneMotionAbi.FootprintRadius),
            8);
        AssertOffset<WzEditorSceneMotionAbi>(
            nameof(WzEditorSceneMotionAbi.AlignmentStrength),
            12);

        AssertSize<WzEditorSceneMotionFilterRotationAxisAbi>(16);
        AssertOffset<WzEditorSceneMotionFilterRotationAxisAbi>(
            nameof(WzEditorSceneMotionFilterRotationAxisAbi.SmoothingTime), 0);
        AssertOffset<WzEditorSceneMotionFilterRotationAxisAbi>(
            nameof(WzEditorSceneMotionFilterRotationAxisAbi.Level), 4);
        AssertOffset<WzEditorSceneMotionFilterRotationAxisAbi>(
            nameof(WzEditorSceneMotionFilterRotationAxisAbi.Limit), 5);
        AssertOffset<WzEditorSceneMotionFilterRotationAxisAbi>(
            nameof(WzEditorSceneMotionFilterRotationAxisAbi.LimitMinDegrees), 8);
        AssertOffset<WzEditorSceneMotionFilterRotationAxisAbi>(
            nameof(WzEditorSceneMotionFilterRotationAxisAbi.LimitMaxDegrees), 12);

        AssertSize<WzEditorSceneMotionFilterAbi>(68);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.TranslationSmoothing0), 0);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.TerrainFloor), 12);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.Enabled), 13);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.TerrainFloorOffset), 16);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.Roll), 20);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.Pitch), 36);
        AssertOffset<WzEditorSceneMotionFilterAbi>(
            nameof(WzEditorSceneMotionFilterAbi.Yaw), 52);

        AssertSize<WzEditorSceneAtmosphereAbi>(16);
        AssertOffset<WzEditorSceneAtmosphereAbi>(
            nameof(WzEditorSceneAtmosphereAbi.AtmosphereAssetNodeId),
            0);
        AssertOffset<WzEditorSceneAtmosphereAbi>(
            nameof(WzEditorSceneAtmosphereAbi.HasAtmosphereRef),
            8);
        AssertOffset<WzEditorSceneAtmosphereAbi>(
            nameof(WzEditorSceneAtmosphereAbi.Enabled),
            9);

        AssertSize<WzEditorSceneEnvironmentAbi>(16);
        AssertOffset<WzEditorSceneEnvironmentAbi>(
            nameof(WzEditorSceneEnvironmentAbi.EnvironmentAssetNodeId),
            0);
        AssertOffset<WzEditorSceneEnvironmentAbi>(
            nameof(WzEditorSceneEnvironmentAbi.HasEnvironmentRef),
            8);
        AssertOffset<WzEditorSceneEnvironmentAbi>(
            nameof(WzEditorSceneEnvironmentAbi.Enabled),
            9);

        AssertSize<WzEditorSceneRenderToTextureAbi>(16);
        AssertOffset<WzEditorSceneRenderToTextureAbi>(
            nameof(WzEditorSceneRenderToTextureAbi.TargetAssetNodeId),
            0);
        AssertOffset<WzEditorSceneRenderToTextureAbi>(
            nameof(WzEditorSceneRenderToTextureAbi.HasTargetRef),
            8);
        AssertOffset<WzEditorSceneRenderToTextureAbi>(
            nameof(WzEditorSceneRenderToTextureAbi.IncludeDescendants),
            9);
        AssertOffset<WzEditorSceneRenderToTextureAbi>(
            nameof(WzEditorSceneRenderToTextureAbi.AlsoDrawInScene),
            10);
        AssertOffset<WzEditorSceneRenderToTextureAbi>(
            nameof(WzEditorSceneRenderToTextureAbi.Enabled),
            11);

        AssertSize<WzEditorSceneAudioSourceAbi>(16);
        AssertOffset<WzEditorSceneAudioSourceAbi>(
            nameof(WzEditorSceneAudioSourceAbi.AudioRenderableNodeId),
            0);
        AssertOffset<WzEditorSceneAudioSourceAbi>(
            nameof(WzEditorSceneAudioSourceAbi.HasRenderableRef),
            8);
        AssertOffset<WzEditorSceneAudioSourceAbi>(
            nameof(WzEditorSceneAudioSourceAbi.AutoPlay),
            9);
        AssertOffset<WzEditorSceneAudioSourceAbi>(
            nameof(WzEditorSceneAudioSourceAbi.Enabled),
            10);

        AssertSize<WzEditorSceneRenderableBindingAbi>(32);
        AssertOffset<WzEditorSceneRenderableBindingAbi>(
            nameof(WzEditorSceneRenderableBindingAbi.Semantic),
            0);
        AssertOffset<WzEditorSceneRenderableBindingAbi>(
            nameof(WzEditorSceneRenderableBindingAbi.AssetGraphNodeId),
            16);
        AssertOffset<WzEditorSceneRenderableBindingAbi>(
            nameof(WzEditorSceneRenderableBindingAbi.HasSourceRef),
            24);

        AssertSize<WzEditorSceneRenderableConstantAbi>(32);
        AssertOffset<WzEditorSceneRenderableConstantAbi>(
            nameof(WzEditorSceneRenderableConstantAbi.Name),
            0);
        AssertOffset<WzEditorSceneRenderableConstantAbi>(
            nameof(WzEditorSceneRenderableConstantAbi.Value0),
            16);

        AssertSize<WzEditorSceneNodeAbi>(784);
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
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Collision),
            592);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Motion),
            600);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.AudioSource),
            616);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderableBindings),
            632);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderableConstants),
            648);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderOrder),
            664);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.MotionFilter),
            668);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Atmosphere),
            736);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.Environment),
            752);
        AssertOffset<WzEditorSceneNodeAbi>(
            nameof(WzEditorSceneNodeAbi.RenderToTexture),
            768);

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

        AssertSize<WzEditorBehaviorActuatorParamAbi>(32);
        AssertOffset<WzEditorBehaviorActuatorParamAbi>(
            nameof(WzEditorBehaviorActuatorParamAbi.Name),
            0);
        AssertOffset<WzEditorBehaviorActuatorParamAbi>(
            nameof(WzEditorBehaviorActuatorParamAbi.Kind),
            16);
        AssertOffset<WzEditorBehaviorActuatorParamAbi>(
            nameof(WzEditorBehaviorActuatorParamAbi.Reserved),
            20);
        AssertOffset<WzEditorBehaviorActuatorParamAbi>(
            nameof(WzEditorBehaviorActuatorParamAbi.DefaultValue),
            24);

        AssertSize<WzEditorBehaviorActuatorAbi>(48);
        AssertOffset<WzEditorBehaviorActuatorAbi>(
            nameof(WzEditorBehaviorActuatorAbi.Name),
            0);
        AssertOffset<WzEditorBehaviorActuatorAbi>(
            nameof(WzEditorBehaviorActuatorAbi.Label),
            16);
        AssertOffset<WzEditorBehaviorActuatorAbi>(
            nameof(WzEditorBehaviorActuatorAbi.Params),
            32);

        AssertSize<WzEditorBehaviorActuatorCatalogAbi>(24);
        AssertOffset<WzEditorBehaviorActuatorCatalogAbi>(
            nameof(WzEditorBehaviorActuatorCatalogAbi.AbiVersion),
            0);
        AssertOffset<WzEditorBehaviorActuatorCatalogAbi>(
            nameof(WzEditorBehaviorActuatorCatalogAbi.Actuators),
            8);

        AssertSize<WzEditorBehaviorModuleParamAbi>(64);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.Key),
            0);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.Label),
            16);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.Type),
            32);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.Reserved),
            36);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.DefaultNumber),
            40);
        AssertOffset<WzEditorBehaviorModuleParamAbi>(
            nameof(WzEditorBehaviorModuleParamAbi.DefaultString),
            48);

        AssertSize<WzEditorBehaviorModuleAbi>(32);
        AssertOffset<WzEditorBehaviorModuleAbi>(
            nameof(WzEditorBehaviorModuleAbi.Module),
            0);
        AssertOffset<WzEditorBehaviorModuleAbi>(
            nameof(WzEditorBehaviorModuleAbi.Params),
            16);

        AssertSize<WzEditorBehaviorModuleCatalogAbi>(24);
        AssertOffset<WzEditorBehaviorModuleCatalogAbi>(
            nameof(WzEditorBehaviorModuleCatalogAbi.AbiVersion),
            0);
        AssertOffset<WzEditorBehaviorModuleCatalogAbi>(
            nameof(WzEditorBehaviorModuleCatalogAbi.Modules),
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
internal readonly struct WzEditorBehaviorActuatorCatalogAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly WzEditorTableSpanAbi Actuators;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorBehaviorActuatorAbi
{
    public readonly WzEditorStringSpanAbi Name;
    public readonly WzEditorStringSpanAbi Label;
    public readonly WzEditorTableSpanAbi Params;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorBehaviorActuatorParamAbi
{
    public readonly WzEditorStringSpanAbi Name;
    public readonly uint Kind;
    public readonly uint Reserved;
    public readonly double DefaultValue;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorBehaviorModuleCatalogAbi
{
    public readonly uint AbiVersion;
    public readonly uint Ok;
    public readonly WzEditorTableSpanAbi Modules;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorBehaviorModuleAbi
{
    public readonly WzEditorStringSpanAbi Module;
    public readonly WzEditorTableSpanAbi Params;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorBehaviorModuleParamAbi
{
    public readonly WzEditorStringSpanAbi Key;
    public readonly WzEditorStringSpanAbi Label;
    public readonly uint Type;
    public readonly uint Reserved;
    public readonly double DefaultNumber;
    public readonly WzEditorStringSpanAbi DefaultString;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorAssetGraphSnapshotAbi
{
    public readonly uint Ok;
    // Was `Reserved`; the engine now stamps its ABI version here, so the graph
    // editor is no longer the one decoder without a version gate. Same offset and
    // size, so the layout asserts below are unchanged.
    public readonly uint AbiVersion;
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
    // Persisted Collision/Motion component field values (read-back gap fix), each
    // valid iff its HAS_* flag is set. Appended last to mirror the native struct.
    public readonly WzEditorSceneCollisionAbi Collision;
    public readonly WzEditorSceneMotionAbi Motion;
    // Persisted AudioSource component field values (read-back). Appended last to
    // mirror the native struct. Present iff HasAudioSource.
    public readonly WzEditorSceneAudioSourceAbi AudioSource;
    // Custom-renderable ingredients (issue #229/#230): always-valid tables,
    // possibly empty — presence is the count, no HAS_* flag. Appended last to
    // mirror the native struct (the node STRIDE change is the ABI v29 bump).
    public readonly WzEditorTableSpanAbi RenderableBindings;
    public readonly WzEditorTableSpanAbi RenderableConstants;
    // Draw-order layer key (render_layer); always valid, default 0 = World.
    // Appended last to mirror the native struct (the added int is the ABI v30
    // bump). Read by the inspector's layer dropdown.
    public readonly int RenderOrder;
    // Motion Filter component field values, valid iff HasMotionFilter. Appended
    // last to mirror the native struct (the added struct is the ABI v31 bump).
    public readonly WzEditorSceneMotionFilterAbi MotionFilter;
    // Atmosphere component field values, valid iff HasAtmosphere. Appended last
    // to mirror the native struct (the added struct is the ABI v32 bump).
    public readonly WzEditorSceneAtmosphereAbi Atmosphere;
    // FrameEnvironment component field values, valid iff HasEnvironment. Appended
    // last to mirror the native struct (the added struct is the ABI v33 bump).
    public readonly WzEditorSceneEnvironmentAbi Environment;
    // RenderToTexture component field values, valid iff HasRenderToTexture.
    // Appended last to mirror the native struct (the added struct is the ABI v34
    // bump). Omitting it short-strides every node read past the first.
    public readonly WzEditorSceneRenderToTextureAbi RenderToTexture;
}

// One authored semantic resource binding of a node's custom-renderable
// ingredients (issue #229/#230). Mirrors WzEditorSceneRenderableBinding;
// AssetGraphNodeId is valid iff HasSourceRef.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneRenderableBindingAbi
{
    public readonly WzEditorStringSpanAbi Semantic;
    public readonly ulong AssetGraphNodeId;
    public readonly uint HasSourceRef;
    public readonly uint Reserved;
}

// One per-instance constant override of a node's custom-renderable ingredients
// (issue #229/#230). Mirrors WzEditorSceneRenderableConstant (float value[4]).
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneRenderableConstantAbi
{
    public readonly WzEditorStringSpanAbi Name;
    public readonly float Value0;
    public readonly float Value1;
    public readonly float Value2;
    public readonly float Value3;
}

// Authored AudioSource-component field values surfaced read-back. Mirrors
// WzEditorSceneAudioSource. Present iff HasAudioSource.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneAudioSourceAbi
{
    public readonly ulong AudioRenderableNodeId;
    public readonly byte HasRenderableRef;
    public readonly byte AutoPlay;
    public readonly byte Enabled;
    public readonly byte Reserved0;
    public readonly uint Reserved1;
}

// Authored Atmosphere-component field values surfaced read-back. Mirrors
// WzEditorSceneAtmosphere. Present iff HasAtmosphere.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneAtmosphereAbi
{
    public readonly ulong AtmosphereAssetNodeId;
    public readonly byte HasAtmosphereRef;
    public readonly byte Enabled;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
    public readonly uint Reserved2;
}

// Authored FrameEnvironment-component field values surfaced read-back. Mirrors
// WzEditorSceneEnvironment. Present iff HasEnvironment.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneEnvironmentAbi
{
    public readonly ulong EnvironmentAssetNodeId;
    public readonly byte HasEnvironmentRef;
    public readonly byte Enabled;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
    public readonly uint Reserved2;
}

// Authored RenderToTexture-component field values surfaced read-back (issue
// #287). Mirrors WzEditorSceneRenderToTexture. Present iff HasRenderToTexture;
// TargetAssetNodeId is valid iff HasTargetRef. Unlike Atmosphere/Environment
// this is per-node — any number of nodes may each drive their own target.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneRenderToTextureAbi
{
    public readonly ulong TargetAssetNodeId;
    public readonly byte HasTargetRef;
    public readonly byte IncludeDescendants;
    public readonly byte AlsoDrawInScene;
    public readonly byte Enabled;
    public readonly uint Reserved0;
}

// Authored Collision-component field values surfaced read-back (read-back gap
// fix). Mirrors WzEditorSceneCollision. Present iff HasCollision.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneCollisionAbi
{
    public readonly uint CollisionAssetNodeId;
    public readonly byte HasCollisionRef;
    public readonly byte ConstrainMovement;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
}

// Authored Motion-component field values surfaced read-back (read-back gap fix).
// Mirrors WzEditorSceneMotion. Present iff HasMotion.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneMotionAbi
{
    public readonly byte TerrainConstrained;
    public readonly byte AlignToSurface;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
    public readonly float RideHeight;
    public readonly float FootprintRadius;
    public readonly float AlignmentStrength;
}

// One local-axis rotation channel (roll/pitch/yaw) of a node's Motion Filter.
// Mirrors WzEditorSceneMotionFilterRotationAxis. Also the shape carried by the
// live-edit verb. Level/Limit are 0/1.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneMotionFilterRotationAxisAbi
{
    public readonly float SmoothingTime;
    public readonly byte Level;
    public readonly byte Limit;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
    public readonly float LimitMinDegrees;
    public readonly float LimitMaxDegrees;

    public WzEditorSceneMotionFilterRotationAxisAbi(
        float smoothingTime,
        byte level,
        byte limit,
        float limitMinDegrees,
        float limitMaxDegrees)
    {
        SmoothingTime = smoothingTime;
        Level = level;
        Limit = limit;
        Reserved0 = 0;
        Reserved1 = 0;
        LimitMinDegrees = limitMinDegrees;
        LimitMaxDegrees = limitMaxDegrees;
    }
}

// Mirrors WzEditorSceneMotionFilter. Present iff HasMotionFilter. The SAME
// struct is the payload of wz_host_runtime_set_node_motion_filter. TerrainFloor
// /Enabled are 0/1; TranslationSmoothing0/1/2 are world X/Y/Z.
[StructLayout(LayoutKind.Sequential)]
internal readonly struct WzEditorSceneMotionFilterAbi
{
    public readonly float TranslationSmoothing0;
    public readonly float TranslationSmoothing1;
    public readonly float TranslationSmoothing2;
    public readonly byte TerrainFloor;
    public readonly byte Enabled;
    public readonly byte Reserved0;
    public readonly byte Reserved1;
    public readonly float TerrainFloorOffset;
    public readonly WzEditorSceneMotionFilterRotationAxisAbi Roll;
    public readonly WzEditorSceneMotionFilterRotationAxisAbi Pitch;
    public readonly WzEditorSceneMotionFilterRotationAxisAbi Yaw;

    public WzEditorSceneMotionFilterAbi(
        float translationSmoothing0,
        float translationSmoothing1,
        float translationSmoothing2,
        byte terrainFloor,
        byte enabled,
        float terrainFloorOffset,
        WzEditorSceneMotionFilterRotationAxisAbi roll,
        WzEditorSceneMotionFilterRotationAxisAbi pitch,
        WzEditorSceneMotionFilterRotationAxisAbi yaw)
    {
        TranslationSmoothing0 = translationSmoothing0;
        TranslationSmoothing1 = translationSmoothing1;
        TranslationSmoothing2 = translationSmoothing2;
        TerrainFloor = terrainFloor;
        Enabled = enabled;
        Reserved0 = 0;
        Reserved1 = 0;
        TerrainFloorOffset = terrainFloorOffset;
        Roll = roll;
        Pitch = pitch;
        Yaw = yaw;
    }
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
    // Persisted Collision/Motion component field values (read-back gap fix): when
    // set, the node's Collision/Motion struct carries authored field values so the
    // inspector restores them on select + after reload.
    public const uint HasCollision = 1u << 11;
    public const uint HasMotion = 1u << 12;
    public const uint HasAudioSource = 1u << 13;
    // Motion Filter component present: the node's MotionFilter struct carries
    // the authored per-DOF fields (read-back).
    public const uint HasMotionFilter = 1u << 14;
    // Atmosphere component present: the node's Atmosphere struct carries the
    // authored asset-graph ref + enabled (read-back).
    public const uint HasAtmosphere = 1u << 15;
    // FrameEnvironment component present: the node's Environment struct carries the
    // authored asset-graph ref + enabled (read-back).
    public const uint HasEnvironment = 1u << 16;
    // RenderToTexture component present (issue #287): the node's RenderToTexture
    // struct carries the authored Texture ref + composition switches + enabled
    // (read-back).
    public const uint HasRenderToTexture = 1u << 17;
}

internal static class WzEditorSceneCameraFlags
{
    public const uint HasFovY = 1u << 0;
    public const uint HasNearPlane = 1u << 1;
    public const uint HasFarPlane = 1u << 2;
    public const uint HasAspect = 1u << 3;
}
