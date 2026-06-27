using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using Wozzits.Editor.Core.Behaviors;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ViewModelBase
{
    private readonly SynchronizationContext? _syncContext = SynchronizationContext.Current;
    private readonly IDisposable? _editorSessionLifetime;
    private readonly IDisposable? _editorLogSubscription;
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private readonly Action<Action>? _dispatch;
    private readonly string _projectDirectory;
    private readonly BehaviorModuleBuilder _behaviorBuilder = new();
    private bool _shutdown;

    public MainWindowViewModel()
        : this(projectSnapshot: null)
    {
    }

    public MainWindowViewModel(
        EngineProjectSnapshotResponse? projectSnapshot = null,
        IWozzitsEngineEditorSession? editorSession = null,
        EditorLogBuffer? editorLog = null,
        Action<Action>? dispatch = null,
        string? projectDirectory = null)
    {
        _editorSession = editorSession;
        _editorSessionLifetime = editorSession as IDisposable;
        _dispatch = dispatch;
        _projectDirectory = projectDirectory ?? string.Empty;
        SaveAllCommand = new RelayCommand(SaveAll);
        RestartViewportCommand = new RelayCommand(RestartViewport, () => _editorSession is not null);
        RebuildBehaviorsCommand = new AsyncRelayCommand(
            RebuildBehaviorsAsync,
            () => _editorSession is not null);
        AssetGraph = new AssetGraphEditorPaneViewModel(editorSession);
        AssetBrowser = new AssetBrowserPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(
            editorSession, AppendEditorLog, _projectDirectory);
        SceneTree = new SceneTreeEditorPaneViewModel(editorSession);
        InitializeDockLayout();
        _editorLogSubscription = editorLog?.Subscribe(AppendEditorLog);

        ProjectName = projectSnapshot?.ProjectName ?? string.Empty;
        WindowTitle = string.IsNullOrWhiteSpace(ProjectName)
            ? "Wozzits"
            : ProjectName;
        var projectAssetGraph = projectSnapshot?.AssetGraph;
        var sessionAssetGraph = editorSession?.LoadAssetGraphSnapshot();
        AssetGraph.LoadSnapshot(ChooseAssetGraphSnapshot(
            projectAssetGraph,
            sessionAssetGraph));
        SceneTree.LoadSnapshot(projectSnapshot?.Scene);

        // Merge the runtime's grafted "Subtree from asset" children under their
        // hosts (issue #213). Deferred off the constructor: the query blocks on the
        // engine thread until the runtime has loaded + grafted (seconds during a
        // cold start), so posting it lets the window paint first. Falls back to a
        // direct call when there is no dispatcher (design-time / tests).
        if (_dispatch is not null)
        {
            _dispatch(() => SceneTree.MergeGraftedNodes());
        }
        else
        {
            SceneTree.MergeGraftedNodes();
        }
    }

    public string WindowTitle { get; } = "Wozzits";
    public string ProjectName { get; } = string.Empty;
    public AssetGraphEditorPaneViewModel AssetGraph { get; }
    public AssetBrowserPaneViewModel AssetBrowser { get; }
    public SceneTreeEditorPaneViewModel SceneTree { get; }
    public InspectorPaneViewModel Inspector { get; }
    public ConsolePaneViewModel Console { get; private set; } = null!;
    public IFactory DockFactory { get; private set; } = null!;
    public IRootDock EditorLayout { get; private set; } = null!;
    public IRelayCommand SaveAllCommand { get; }
    public IRelayCommand RestartViewportCommand { get; }
    public IAsyncRelayCommand RebuildBehaviorsCommand { get; }

    public string EngineLogText => Console.LogText;

    public void Shutdown()
    {
        if (_shutdown)
        {
            return;
        }

        _shutdown = true;
        _editorSessionLifetime?.Dispose();
        _editorLogSubscription?.Dispose();
    }

    private void SaveAll()
    {
        _editorSession?.SaveAssetGraph();
        _editorSession?.SaveScene();
    }

    // Reopen the in-process engine viewport. Stops the current runtime if one is
    // still alive (or frees a closed/zombie one) and starts a fresh viewport for
    // the project - the way back after the viewport window has been closed.
    private void RestartViewport()
    {
        _editorSession?.RestartRuntime();
    }

    // Recompile the project's behavior-module DLLs (cmake, streamed to the
    // console) and, on success, hot-reload them into the running engine without
    // restarting the viewport. Mirrors the imgui toolhost editor's Rebuild step.
    // The command disables itself while running (AsyncRelayCommand default).
    private async Task RebuildBehaviorsAsync()
    {
        if (_editorSession is null)
        {
            return;
        }

        AppendEditorLog("[editor] Rebuilding behavior modules...");

        BehaviorBuildOutcome outcome;
        try
        {
            outcome = await _behaviorBuilder.RebuildAsync(
                _projectDirectory,
                AppendEditorLog);
        }
        catch (Exception ex)
        {
            AppendEditorLog($"[editor] Behavior rebuild error: {ex.Message}");
            return;
        }

        switch (outcome)
        {
            case BehaviorBuildOutcome.Failed:
                AppendEditorLog(
                    "[editor] Behavior rebuild failed; modules not reloaded.");
                return;
            case BehaviorBuildOutcome.Skipped:
                // Nothing was built, so there is nothing to hot-reload.
                return;
        }

        var reload = _editorSession.ReloadBehaviorModules();
        AppendEditorLog(reload.Ok
            ? "[editor] Behavior modules reloaded."
            : $"[editor] Behavior reload skipped: {reload.Error}");
    }

    private void AppendEditorLog(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        if (_dispatch is not null)
        {
            _dispatch(() => AddEngineLogLine(line));
            return;
        }

        if (_syncContext is null || SynchronizationContext.Current == _syncContext)
        {
            AddEngineLogLine(line);
            return;
        }

        _syncContext.Post(_ => AddEngineLogLine(line), null);
    }

    private void AddEngineLogLine(string line)
    {
        Console.AppendLogLine(line);
        OnPropertyChanged(nameof(EngineLogText));
    }

    private static EngineAssetGraphSnapshotResponse? ChooseAssetGraphSnapshot(
        EngineAssetGraphSnapshotResponse? projectAssetGraph,
        EngineAssetGraphSnapshotResponse? sessionAssetGraph)
    {
        if (sessionAssetGraph?.Ok == true)
        {
            var projectNodeCount =
                projectAssetGraph?.Snapshot.Nodes.Count ?? 0;
            if (sessionAssetGraph.Snapshot.Nodes.Count > 0 || projectNodeCount == 0)
            {
                return sessionAssetGraph;
            }
        }

        return projectAssetGraph ?? sessionAssetGraph;
    }

    private void InitializeDockLayout()
    {
        Console = new ConsolePaneViewModel();
        // The scene tree and asset graph share one inspector and must show a
        // single active selection: selecting in one pane clears the other (and
        // its highlight) so the inspector tracks the highlighted node; an empty
        // selection shows no inspector.
        SceneTree.SelectedNodeChanged += OnSceneNodeSelected;
        AssetGraph.SelectedNodeChanged += OnAssetGraphNodeSelected;
        // A "Subtree from asset" assign/clear changes the runtime's grafted
        // children (issue #213); re-merge them into the scene tree under the host.
        Inspector.SceneSourceChanged += OnInspectorSceneSourceChanged;

        var layoutFactory = new EditorDockLayoutFactory(this);
        DockFactory = layoutFactory.Factory;
        EditorLayout = layoutFactory.CreateLayout();
    }

    // Guards against re-entrancy: clearing one pane raises its
    // SelectedNodeChanged(null), which must not recurse back through here.
    private bool _syncingSelection;

    private void OnSceneNodeSelected(SceneTreeNodeViewModel? node)
    {
        if (_syncingSelection)
        {
            return;
        }
        if (node is not null)
        {
            _syncingSelection = true;
            try
            {
                AssetGraph.ClearSelection();
            }
            finally
            {
                _syncingSelection = false;
            }
        }
        RefreshInspectorSceneSources();
        Inspector.Inspect(node);
    }

    private void OnAssetGraphNodeSelected(AssetGraphNodeCardViewModel? node)
    {
        if (_syncingSelection)
        {
            return;
        }
        if (node is not null)
        {
            _syncingSelection = true;
            try
            {
                SceneTree.ClearSelection();
            }
            finally
            {
                _syncingSelection = false;
            }
        }
        RefreshInspectorSceneSources();
        Inspector.Inspect(node);
    }

    // Thread the asset graph's "Scene from GLB" nodes into the inspector's "Subtree
    // from asset" picker (issue #213 piece 2). Refreshed on each selection from the
    // loaded snapshot — a snapshot-time list is sufficient for piece 2; it does not
    // track live graph edits. The inspector takes plain option data, so it never
    // depends on the asset-graph pane.
    private void RefreshInspectorSceneSources()
    {
        Inspector.SetAvailableSceneSources(
            AssetGraph.Nodes
                .Where(IsSceneFromGlbNode)
                .Select(node => new InspectorSceneSourceOptionViewModel(
                    node.Id,
                    node.DisplayName)));

        // Thread the "Render program" candidates (issue #213) the same way: the
        // inspector takes plain option data.
        RefreshInspectorRenderProgramSources();

        // Thread the live asset-graph topology so the inspector's "GLB node" tree
        // picker (issue #213) can walk the selected "Mesh from GLB scene" node's
        // `scene` → `source_file` edges to the connected GLB file's source_path.
        // Built from the live pane VMs (kept current across graph edits), projected
        // back to the plain protocol shape the inspector consumes — only the fields
        // the traversal reads (node id, input ports, params; edge endpoints + port).
        RefreshInspectorAssetGraphTopology();
    }

    // Project the asset-graph pane's live node/edge VMs back to the minimal protocol
    // records the inspector's GLB-node picker traversal needs, and hand them over.
    private void RefreshInspectorAssetGraphTopology()
    {
        var nodes = AssetGraph.Nodes
            .Select(node => new EngineAssetGraphNode
            {
                Id = node.Id,
                Schema = node.SchemaLabel,
                DisplayName = node.DisplayName,
                InputPorts = node.InputPorts
                    .Select(port => new EngineAssetGraphPort
                    {
                        Index = port.Index,
                        Name = port.Name,
                    })
                    .ToList(),
                Params = node.Params.ToList(),
            })
            .ToList();

        var edges = AssetGraph.Edges
            .Select(edge => new EngineAssetGraphEdge
            {
                Id = edge.Id,
                From = edge.FromNodeId,
                To = edge.ToNodeId,
                ToInputPort = edge.ToInputPort,
            })
            .ToList();

        Inspector.SetAssetGraphTopology(nodes, edges);
    }

    // Thread the "Render program" picker (issue #213) with the asset-graph nodes
    // whose OUTPUT asset type is RenderProgram (1049). Filtering on the output
    // port's asset type (not schema label) covers every schema that yields the type
    // and matches what the engine routes on when assembling the renderable.
    // Refreshed per selection from the snapshot.
    private void RefreshInspectorRenderProgramSources()
    {
        Inspector.SetAvailableRenderPrograms(
            AssetGraph.Nodes
                .Where(IsRenderProgramNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));

        // Thread the "Collision" picker (terrain-stick track) the same way: the
        // inspector takes plain option data, filtered to Collision outputs.
        RefreshInspectorCollisionSources();
    }

    // Thread the "Collision" picker (terrain-stick track) with the asset-graph
    // nodes whose OUTPUT asset type is Collision (150). Filtering on the output
    // port's asset type (not schema label) covers every schema that yields the type
    // ("Collision from mesh"/"Collision from terrain"), exactly as the render-
    // program picker does. Refreshed per selection from the snapshot.
    private void RefreshInspectorCollisionSources()
    {
        Inspector.SetAvailableCollisionSources(
            AssetGraph.Nodes
                .Where(IsCollisionNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));
    }

    // True when a node produces a render program the render-program component can
    // consume (RenderProgram = 1049 in type_extensions.h, the value the engine's
    // assemble routes on).
    private static bool IsRenderProgramNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == RenderProgramAssetTypeId);

    private const uint RenderProgramAssetTypeId = 1049;

    // True when a node produces a Collision asset the Collision component can
    // reference (kAssetTypeCollisionAsset = 150 in type_extensions.h).
    private static bool IsCollisionNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == CollisionAssetTypeId);

    private const uint CollisionAssetTypeId = 150;

    // True for a "Scene from GLB" asset-graph node — the only graftable subtree
    // source the picker offers (issue #213 piece 2).
    //
    // Matching by node.TypeName is WRONG: TypeName is the asset *type* display name
    // ("Scene"), which a Scene-from-JSON node shares, so it never identifies the GLB
    // schema and would also wrongly include scene-from-JSON. The stable per-schema
    // discriminator is node.SchemaLabel (the engine's schema_tail — the low 32 bits
    // of the SchemaID as hex; "e7000711" for kSceneFromGLBSchema 0xF11ECA55E7000711,
    // distinct from scene-from-JSON's "e7000710"). We match on that first so a node
    // renamed via its `name` param still resolves. The Scene-from-GLB schema declares
    // no name/source_path param, so its DisplayName is deterministically the schema
    // label "Scene from GLB" (engine display_name fallback) — a reliable secondary
    // match that also survives a schema-id renumber on the engine side.
    private static bool IsSceneFromGlbNode(AssetGraphNodeCardViewModel node)
    {
        return string.Equals(
                node.SchemaLabel,
                SceneFromGlbSchemaLabel,
                System.StringComparison.Ordinal)
            || string.Equals(
                node.DisplayName,
                SceneFromGlbDisplayName,
                System.StringComparison.Ordinal);
    }

    // A scene-source reference/descriptor was assigned or cleared in the inspector
    // (issue #213): the runtime re-grafted, so re-merge its grafted children into
    // the scene tree under their hosts. The merge re-queries the runtime and
    // de-dupes its own previous grafts, so calling it after every change is safe.
    private void OnInspectorSceneSourceChanged()
    {
        SceneTree.MergeGraftedNodes();
    }

    // The stable schema discriminator for the "Scene from GLB" asset-graph node
    // (issue #213): the engine serializes node.schema_label = schema_tail(schema) =
    // low 32 bits of the SchemaID as 8 hex digits. kSceneFromGLBSchema is
    // 0xF11ECA55E7000711, so its tail is "e7000711". Primary, rename-proof match.
    private const string SceneFromGlbSchemaLabel = "e7000711";

    // The engine's schema_display_name_view of the Scene-from-GLB schema. Because
    // that schema declares no name/source_path param, a node's DisplayName falls
    // back to this label deterministically — the secondary match that survives a
    // schema-id renumber. Also the name shown on the node card.
    private const string SceneFromGlbDisplayName = "Scene from GLB";
}
